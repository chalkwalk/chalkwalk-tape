#pragma once

// Windowed-sinc FIR design, shared by the record chain's rate conversion
// (DESIGN.md section 4.2).
//
// KAISER, BECAUSE THE FILTER IS SPECIFIED BY WHAT IT MUST ACHIEVE. beta follows
// from the stopband attenuation by Kaiser's own relation, so a caller states
// decibels rather than choosing a window by taste.
//
// AND THE DESIGN VERIFIES ITSELF, which is the part that matters. Kaiser's tap
// estimate rounds, and beta and the tap count pull against each other: raising
// the target raises beta, which WIDENS the transition, so if the rounded tap
// count does not also rise the filter comes out worse. Measured on the
// decimator, asking for 96 dB realised 83 where asking for 93 realised 93 -- a
// design that got worse as it was asked for more. `designLowpass` therefore
// grows the tap count until the realised stopband meets the specification
// rather than trusting the formula that started it.
//
// Design time only: this allocates and sweeps a response. On the audio thread a
// speed change means designing off thread and handing over.
//
// JUCE-free by design. Promotion target: chalkwalk-dsp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chalkwalk::tape::fir
{
    // Kaiser's own relation between stopband attenuation and beta.
    [[nodiscard]] inline double kaiserBeta(double stopbandDb) noexcept
    {
        if (stopbandDb > 50.0) return 0.1102 * (stopbandDb - 8.7);
        if (stopbandDb >= 21.0)
            return 0.5842 * std::pow(stopbandDb - 21.0, 0.4)
                 + 0.07886 * (stopbandDb - 21.0);
        return 0.0;
    }

    // Magnitude response of a real FIR at a normalised frequency (cycles per
    // sample, so 0.5 is Nyquist).
    [[nodiscard]] inline double magnitude(const std::vector<double>& h,
                                          double normalisedHz) noexcept
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i)
        {
            const double a = -2.0 * M_PI * normalisedHz * static_cast<double>(i);
            re += h[i] * std::cos(a);
            im += h[i] * std::sin(a);
        }
        return std::sqrt(re * re + im * im);
    }

    // Worst response anywhere in the stopband, in dB. Sampled finely enough to
    // land on the ripple peaks rather than between them.
    [[nodiscard]] inline double worstStopband(const std::vector<double>& h,
                                              double stopbandNormalised) noexcept
    {
        double worst = 0.0;
        constexpr int kPoints = 20000;
        for (int i = 0; i <= kPoints; ++i)
        {
            const double f = stopbandNormalised
                           + (0.5 - stopbandNormalised) * i / kPoints;
            worst = std::max(worst, magnitude(h, f));
        }
        return 20.0 * std::log10(std::max(worst, 1.0e-30));
    }

    // A lowpass meeting `stopbandDb` between `passband` and `stopband`, both
    // normalised. `gain` scales the passband: 1 for a decimator, the
    // interpolation factor for an upsampler recovering what zero-stuffing lost.
    [[nodiscard]] inline std::vector<double> designLowpass(double passband,
                                                           double stopband,
                                                           double stopbandDb,
                                                           double gain = 1.0)
    {
        const double transition = stopband - passband;
        int taps = (transition > 0.0)
            ? static_cast<int>(std::ceil((stopbandDb - 8.0)
                                         / (2.285 * 2.0 * M_PI * transition)))
            : 8;
        taps = std::max(taps, 8);
        taps |= 1;  // odd, so there is a centre tap and a whole-sample delay

        const double beta = kaiserBeta(stopbandDb);
        const double cutoff = 0.5 * (passband + stopband);

        std::vector<double> h;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const int half = taps / 2;
            h.assign(static_cast<std::size_t>(taps), 0.0);
            double sum = 0.0;
            for (int n = -half; n <= half; ++n)
            {
                const double x = static_cast<double>(n);
                const double sinc = (n == 0)
                    ? 2.0 * cutoff
                    : std::sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
                const double r = x / static_cast<double>(half);
                const double w =
                    std::cyl_bessel_i(0.0, beta * std::sqrt(std::max(0.0, 1.0 - r * r)))
                    / std::cyl_bessel_i(0.0, beta);
                h[static_cast<std::size_t>(n + half)] = sinc * w;
                sum += sinc * w;
            }
            // Unity at DC before the gain, so a stage that changed the level
            // would read as the tape being quiet rather than the filter being
            // wrong.
            for (auto& c : h)
                c *= gain / sum;

            if (worstStopband(h, stopband) <= -stopbandDb + 20.0 * std::log10(gain))
                break;
            taps += 2;
        }
        return h;
    }

    // The taps worth multiplying, as (index, value).
    //
    // RELATIVE TO THE PEAK, NOT ABSOLUTE, and the difference decides whether a
    // halfband is one. A halfband's even taps are sin(k*pi)/(k*pi): exactly
    // zero in algebra and about 1e-17 in floating point. An absolute threshold
    // of 1e-18 keeps every one of them, so the structure is a halfband and the
    // arithmetic is not.
    struct Tap { std::size_t index; double value; };

    [[nodiscard]] inline std::vector<Tap> liveTaps(const std::vector<double>& h)
    {
        double peak = 0.0;
        for (const double c : h)
            peak = std::max(peak, std::abs(c));

        std::vector<Tap> live;
        for (std::size_t i = 0; i < h.size(); ++i)
            if (std::abs(h[i]) > 1.0e-12 * peak)
                live.push_back({i, h[i]});
        return live;
    }
}
