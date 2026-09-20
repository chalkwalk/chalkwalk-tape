#pragma once

// Minimum-phase FIR design from a magnitude response (SOURCES section 15).
//
// DESIGN TIME ONLY. Nothing here runs on the audio thread; it turns a magnitude
// specification into an impulse response once, in prepare().
//
// WHY THIS EXISTS. A symmetric FIR is linear phase, which is exactly what the
// recorded crosstalk path wants: a constant delay can be cancelled by reading
// the tape that much earlier, so the length can be chosen for accuracy alone
// (DESIGN.md section 4.5). The LIVE path has no such option -- a track being
// recorded has no future to read -- so every tap of a symmetric filter is
// lateness, and at 15 ips that was 11 ms of the live path's 30.
//
// A minimum-phase filter has the same magnitude and the least group delay of
// any causal filter with that magnitude, because its energy is packed as far
// towards the front of the impulse response as the magnitude permits. Its phase
// is not flat, and for a bleed term nobody hears that; its arrival time is
// something they would.
//
// THE METHOD is cepstral folding, from Smith's Spectral Audio Signal
// Processing: a frequency response is minimum phase if and only if its cepstrum
// is causal, so computing the cepstrum and folding the anticausal half onto the
// causal half produces the minimum-phase response with the same magnitude.
//
// The FFT here is forty lines of radix-2, and the ecosystem's rule is to adopt
// an FFT rather than write one. That rule is about the SIGNAL PATH; this is a
// design-time transform small enough to test exhaustively, and it is tested
// against a naive DFT, which shares none of its machinery -- the same exemption
// dsp/Spectrum.h takes for the same reason. It is not reused from there because
// that file's promotion target is a different library (PRINCIPLES section 14),
// and it needs a complex forward transform, which that one does not offer.
//
// JUCE-free by design. Promotion target: chalkwalk-tape.

#include <cmath>
#include <complex>
#include <cstddef>
#include <functional>
#include <vector>

namespace chalkwalk::tape
{
    // In-place radix-2 decimation-in-time. `inverse` conjugates the twiddle and
    // scales by 1/n, so a forward followed by an inverse is the identity -- and
    // there is a test that says so, because a sign error here conjugates the
    // result and leaves every magnitude untouched.
    inline void fft(std::vector<std::complex<double>>& data, bool inverse)
    {
        const std::size_t n = data.size();
        if (n < 2 || (n & (n - 1)) != 0)
            return;

        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(data[i], data[j]);
        }

        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = (inverse ? 2.0 : -2.0) * M_PI
                               / static_cast<double>(len);
            const std::complex<double> step{ std::cos(angle), std::sin(angle) };
            for (std::size_t i = 0; i < n; i += len)
            {
                std::complex<double> w{ 1.0, 0.0 };
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = data[i + k];
                    const auto v = data[i + k + len / 2] * w;
                    data[i + k] = u + v;
                    data[i + k + len / 2] = u - v;
                    w *= step;
                }
            }
        }

        if (inverse)
            for (auto& x : data)
                x /= static_cast<double>(n);
    }

    // The floor applied to the magnitude before taking its logarithm, relative
    // to the response's peak.
    //
    // NOT a tuning constant: the responses this is used for decay like a
    // negative exponential, so they reach zero to within floating point well
    // inside the band and log(0) is negative infinity. Every value below the
    // floor is one the filter is not being asked to reproduce anyway -- 120 dB
    // down is beneath the medium's noise, beneath the record chain's alias
    // floor, and beneath the -80 dB at which a path stops being mixed at all.
    //
    // Flooring too HARD is the failure mode to know about: it flattens the
    // response's tail, which the cepstrum reads as a filter with less rolloff,
    // and the design comes back too bright. Too soft and the cepstrum is
    // dominated by numerical noise from the region nobody hears.
    inline constexpr double kLogFloorDb = -120.0;

    // Design a minimum-phase FIR of `taps` samples whose magnitude follows
    // `response(hz)`, sampled at `sampleRate`.
    //
    // `fftSize` must be a power of two and comfortably longer than `taps`: the
    // cepstrum is computed on a circular grid, so a short transform wraps the
    // impulse response's tail onto its head. Sixteen times the length is the
    // default and is measured rather than assumed -- see the tests, which check
    // the design against the specification and against a longer transform.
    [[nodiscard]] inline std::vector<double> designMinimumPhase(
        int taps,
        double sampleRate,
        const std::function<double(double)>& response,
        std::size_t fftSize = 0)
    {
        if (taps < 1 || sampleRate <= 0.0)
            return {};

        std::size_t n = (fftSize != 0) ? fftSize : 16;
        if (fftSize == 0)
        {
            n = 1;
            while (n < static_cast<std::size_t>(taps) * 16)
                n <<= 1;
        }
        if (n < 2 || (n & (n - 1)) != 0)
            return {};

        // The magnitude on the full circle. Real and even, which is what makes
        // its transform real: the cepstrum of a magnitude has no phase in it,
        // which is the whole reason the folding trick works.
        std::vector<std::complex<double>> spectrum(n);
        double peak = 0.0;
        for (std::size_t k = 0; k <= n / 2; ++k)
        {
            const double hz = static_cast<double>(k) * sampleRate
                            / static_cast<double>(n);
            const double m = std::abs(response(hz));
            peak = std::max(peak, m);
            spectrum[k] = m;
            if (k > 0 && k < n / 2)
                spectrum[n - k] = m;
        }
        if (peak <= 0.0)
            return std::vector<double>(static_cast<std::size_t>(taps), 0.0);

        const double floorValue = peak * std::pow(10.0, kLogFloorDb / 20.0);
        for (auto& x : spectrum)
            x = std::log(std::max(x.real(), floorValue));

        // The cepstrum, and the fold. Smith: "c(n) <- c(n) + c(-n), for
        // n=1,2,..., and c(n) <- 0 for n<0" -- which for a real even cepstrum
        // is doubling the first half and zeroing the second, with the two
        // self-conjugate bins left alone.
        fft(spectrum, true);
        for (std::size_t k = 1; k < n / 2; ++k)
        {
            spectrum[k] *= 2.0;
            spectrum[n - k] = 0.0;
        }

        // Back to a log spectrum, exponentiate, and back to time. exp() of a
        // complex number is where the phase appears: the imaginary part of the
        // folded cepstrum's transform IS the minimum phase, and it is the
        // Hilbert transform of the log magnitude.
        fft(spectrum, false);
        for (auto& x : spectrum)
            x = std::exp(x);
        fft(spectrum, true);

        std::vector<double> h(static_cast<std::size_t>(taps));
        for (std::size_t i = 0; i < h.size(); ++i)
            h[i] = (i < n) ? spectrum[i].real() : 0.0;
        return h;
    }

    // Where a causal impulse response's energy actually is, in samples: the
    // index at which the cumulative energy passes half the total.
    //
    // The honest delay figure for a minimum-phase filter, which has no single
    // group delay -- its phase is not linear, so different frequencies arrive at
    // different times. A median beats a mean here because the tail is long and
    // quiet, and it beats a peak because a two-sided response would report zero.
    [[nodiscard]] inline double medianEnergyDelay(const std::vector<double>& h) noexcept
    {
        double total = 0.0;
        for (const double v : h)
            total += v * v;
        if (total <= 0.0)
            return 0.0;

        double running = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i)
        {
            running += h[i] * h[i];
            if (running >= 0.5 * total)
                return static_cast<double>(i);
        }
        return static_cast<double>(h.size() - 1);
    }
}
