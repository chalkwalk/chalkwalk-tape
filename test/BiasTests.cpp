// Bias (SOURCES section 7, DESIGN.md section 4.1).
//
// RECORD SIDE: what bias does is written into the tape.
//
// A high-frequency current added to the signal before the record head, so that
// recording happens on the linear part of the magnetisation curve instead of
// across the deadzone around zero. Camras's patent, which is the primary source
// here and is public domain, states the mechanism directly: with HF bias
// superimposed the residual magnetisation curve becomes "substantially a true
// straight line that passes through the zero position".

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/Bias.h>
#include <chalkwalk/dsp/Spectrum.h>
#include <chalkwalk/tape/Hysteresis.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

TEST_CASE("the bias oscillator has the amplitude and frequency asked for", "[bias]")
{
    tape::BiasOscillator bias;
    bias.prepare(48000.0 * 16.0, 55000.0, 6.0e4);

    double peak = 0.0;
    for (int i = 0; i < 100000; ++i)
        peak = std::max(peak, std::abs(bias.next()));

    REQUIRE(peak == Approx(6.0e4).epsilon(1.0e-3));
}

TEST_CASE("the bias frequency is what was requested", "[bias]")
{
    // Counting zero crossings is a different method from the one that
    // generates it, which is the point.
    constexpr double rate = 48000.0 * 16.0;
    constexpr double hz = 55000.0;
    tape::BiasOscillator bias;
    bias.prepare(rate, hz, 1.0);

    int crossings = 0;
    double previous = bias.next();
    constexpr int n = 768000;
    for (int i = 1; i < n; ++i)
    {
        const double value = bias.next();
        if ((previous < 0.0) != (value < 0.0))
            ++crossings;
        previous = value;
    }

    const double seconds = n / rate;
    REQUIRE(crossings / (2.0 * seconds) == Approx(hz).epsilon(1.0e-3));
}

TEST_CASE("bias needs a sample rate that can represent it", "[bias]")
{
    // 55 kHz cannot exist at 48 kHz. The oscillator must refuse rather than
    // alias, because an aliased bias carrier is a low-frequency tone written
    // permanently onto the tape and it would be blamed on the hysteresis model.
    tape::BiasOscillator bias;
    REQUIRE(!bias.prepare(48000.0, 55000.0, 6.0e4));
    REQUIRE(bias.prepare(48000.0 * 4.0, 55000.0, 6.0e4));
}

TEST_CASE("the oscillator is deterministic and resettable", "[bias]")
{
    tape::BiasOscillator a, b;
    a.prepare(768000.0, 55000.0, 6.0e4);
    b.prepare(768000.0, 55000.0, 6.0e4);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(a.next() == Approx(b.next()).epsilon(1.0e-12));

    a.reset();
    b.reset();
    REQUIRE(a.next() == Approx(b.next()).epsilon(1.0e-12));
}

TEST_CASE("bias amplitude is calibrated against coercivity", "[bias]")
{
    // SOURCES section 7. The paper describes the amplitude only as "about one
    // order of magnitude larger than the input", which is not a calibration.
    // Camras specifies it against the COERCIVE FORCE, and our hysteresis
    // parameter k is approximately Hc -- so the default is a real number in
    // terms of a constant we already have, rather than a guess.
    const auto stock = tape::TapeStock::ferricOxide();
    const double nominal = tape::nominalBiasAmplitude(stock);

    REQUIRE(nominal == Approx(2.25 * stock.coercivity).epsilon(1.0e-9));
    REQUIRE(nominal > 2.0 * stock.coercivity);
    REQUIRE(nominal < 2.5 * stock.coercivity);
}

namespace
{
    // Harmonic distortion of a recorded tone: energy in harmonics 2..20
    // against energy in the fundamental, in dB.
    //
    // Measured on the magnetisation directly, WITHOUT filtering out the bias
    // carrier, because at 55 kHz it shares no bins with the harmonics of a
    // 500 Hz tone. In a real machine the carrier never reaches playback anyway
    // -- the reproduce losses annihilate it (DESIGN.md section 4.11).
    // Energy in the fundamental alone, in dB.
    //
    // NOT total RMS, which is the trap this file fell into twice. The bias
    // carrier is many times the signal, so RMS of the magnetisation measures
    // the carrier and rises with bias no matter what happens to the recording.
    double fundamentalEnergyDb(const std::vector<double>& signal, double hz, double rate)
    {
        std::vector<double> windowed(signal.size());
        const auto n = signal.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const double phase = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
            windowed[i] = signal[i] * (0.35875 - 0.48829 * std::cos(phase)
                                       + 0.14128 * std::cos(2.0 * phase)
                                       - 0.01168 * std::cos(3.0 * phase));
        }
        const auto spectrum = chalkwalk::dsp::spectrum::forwardFft(windowed);
        if (spectrum.empty())
            return -400.0;

        const double binHz = rate / static_cast<double>(spectrum.size());
        const auto centre = static_cast<long long>(std::round(hz / binHz));
        double total = 0.0;
        for (long long k = centre - 6; k <= centre + 6; ++k)
            if (k > 0 && k < static_cast<long long>(spectrum.size() / 2))
                total += std::norm(spectrum[static_cast<std::size_t>(k)]);
        return 10.0 * std::log10(std::max(1.0e-30, total));
    }

    double harmonicDistortionDb(const std::vector<double>& signal, double hz, double rate)
    {
        std::vector<double> windowed(signal.size());
        const auto n = signal.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const double phase = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
            windowed[i] = signal[i] * (0.35875 - 0.48829 * std::cos(phase)
                                       + 0.14128 * std::cos(2.0 * phase)
                                       - 0.01168 * std::cos(3.0 * phase));
        }
        const auto spectrum = chalkwalk::dsp::spectrum::forwardFft(windowed);
        if (spectrum.empty())
            return 0.0;

        const double binHz = rate / static_cast<double>(spectrum.size());
        auto energyNear = [&](double target)
        {
            double total = 0.0;
            const auto centre = static_cast<long long>(std::round(target / binHz));
            for (long long k = centre - 6; k <= centre + 6; ++k)
                if (k > 0 && k < static_cast<long long>(spectrum.size() / 2))
                    total += std::norm(spectrum[static_cast<std::size_t>(k)]);
            return total;
        };

        const double fundamental = energyNear(hz);
        double harmonics = 0.0;
        for (int h = 2; h <= 20; ++h)
            harmonics += energyNear(hz * h);

        if (fundamental <= 0.0)
            return 0.0;
        return 10.0 * std::log10(std::max(1.0e-30, harmonics) / fundamental);
    }
}

TEST_CASE("bias linearises the recording", "[bias]")
{
    // THE reason bias exists, and what would catch it being applied in the
    // wrong place or at the wrong level.
    //
    // Camras: with HF bias superimposed the residual magnetisation curve
    // becomes "substantially a true straight line through zero". So the
    // assertion is about DISTORTION, which is what that buys.
    //
    // An earlier version of this test compared correlation with the input
    // instead, and failed for a reason worth recording: bias is 60 kA/m against
    // a 4 kA/m signal, so the magnetisation is dominated by the CARRIER and
    // correlation with the tone is meaningless. Distortion is the measurement
    // that survives the carrier being present.
    const auto stock = tape::TapeStock::ferricOxide();
    constexpr double rate = 48000.0 * 16.0;
    constexpr double hz = 500.0;
    constexpr int n = 1 << 16;

    auto record = [&](double signalAmplitude, double biasAmplitude)
    {
        tape::Hysteresis h;
        h.prepare(rate, stock, tape::Solver::RungeKutta4InField, tape::Evaluation::Exact);
        tape::BiasOscillator bias;
        const bool wanted = biasAmplitude > 0.0;
        if (wanted)
            REQUIRE(bias.prepare(rate, 55000.0, biasAmplitude));

        std::vector<double> out;
        out.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const double signal = signalAmplitude * std::sin(2.0 * M_PI * hz * i / rate);
            out.push_back(h.process(wanted ? signal + bias.next() : signal));
        }
        return out;
    };

    const double nominal = tape::nominalBiasAmplitude(stock);

    SECTION("a small signal records with less distortion when biased")
    {
        // Small enough to sit where an unbiased tape is worst.
        constexpr double amplitude = 4.0e3;
        const double unbiased = harmonicDistortionDb(record(amplitude, 0.0), hz, rate);
        const double biased = harmonicDistortionDb(record(amplitude, nominal), hz, rate);

        INFO("distortion unbiased " << unbiased << " dB, biased " << biased << " dB");
        REQUIRE(biased < unbiased);
    }

    SECTION("gross overbias breaks down")
    {
        // The other end, and it is unambiguous: at six times coercivity the
        // carrier is saturating the tape on its own and distortion collapses to
        // roughly the unbiased figure. A real machine badly overbiased sounds
        // broken, and so does this one.
        constexpr double amplitude = 4.0e3;
        const double correct = harmonicDistortionDb(record(amplitude, nominal), hz, rate);
        const double over = harmonicDistortionDb(record(amplitude, nominal * 2.7), hz, rate);

        INFO("correct " << correct << " dB, overbiased " << over << " dB");
        REQUIRE(over > correct + 20.0);
    }

    SECTION("sensitivity peaks at low bias and then falls away")
    {
        // The curve a technician actually aligns against: bias increases the
        // tape's sensitivity to a maximum and then reduces it, which is why the
        // procedure is "set for peak output, then overbias by a stated amount".
        // Monotonic decline past the peak is the robust part and is what is
        // asserted; the exact peak is a property of the stock.
        constexpr double amplitude = 4.0e3;

        auto outputAt = [&](double biasAmplitude)
        {
            return fundamentalEnergyDb(record(amplitude, biasAmplitude), hz, rate);
        };

        const double atPeak = outputAt(nominal * 0.25);
        const double atNominal = outputAt(nominal);
        const double wayOver = outputAt(nominal * 2.7);

        INFO("output " << atPeak << " -> " << atNominal << " -> " << wayOver);
        REQUIRE(atNominal < atPeak);
        REQUIRE(wayOver < atNominal);
    }

    SECTION("any bias is far better than none")
    {
        // The headline, and the most robust thing here by a wide margin:
        // measured across the whole usable range, bias buys 20 to 30 dB of
        // distortion. Everything else about the level is a refinement on top of
        // that.
        constexpr double amplitude = 4.0e3;
        const double unbiased = harmonicDistortionDb(record(amplitude, 0.0), hz, rate);

        for (const double ratio : {0.25, 0.5, 1.0, 2.25})
        {
            const double biased = harmonicDistortionDb(record(amplitude, nominal * ratio),
                                                       hz, rate);
            INFO("ratio " << ratio << ": " << biased << " dB against " << unbiased);
            // Measured improvements run 14 to 23 dB across the usable range;
            // the bound is set below the smallest of them rather than at a
            // round number.
            REQUIRE(biased < unbiased - 12.0);
        }
    }
}

TEST_CASE("the carrier is a rotation, and it does not drift",
          "[bias][teeth]")
{
    // THE OSCILLATOR CALLED `std::cos` EVERY SAMPLE, at the OVERSAMPLED rate:
    // 768,000 transcendentals a second per armed track, for a sinusoid at a
    // frequency that never changes. Measured at 7.53 ns a step against a
    // 121 ns write-path budget -- the fourth most expensive thing in the record
    // chain, and the only one doing no work. It is a rotation now: two
    // multiplies and a subtract, 1.58 ns.
    //
    // THE RISK A ROTATION CARRIES IS DRIFT, which is why the old code wrapped
    // the phase and why this renormalises. A carrier whose amplitude wanders is
    // an alignment drifting for a reason that is not physical, and it would be
    // blamed on the bias model. So the guarantee is asserted over a take long
    // enough for rounding to accumulate.
    constexpr double rate = 768000.0;
    constexpr double hz = 128000.0;
    constexpr double amplitude = 1.0;

    tape::BiasOscillator b;
    REQUIRE(b.prepare(rate, hz, amplitude));

    const double increment = 2.0 * M_PI * hz / rate;
    double worst = 0.0;
    double worstPeak = 0.0;

    // Ten minutes of tape at the rate the record chain solves at.
    const int samples = static_cast<int>(rate * 600.0);
    for (int i = 0; i < samples; ++i)
    {
        const double got = b.next();
        const double want = amplitude * std::cos(increment * static_cast<double>(i));
        worst = std::max(worst, std::abs(got - want));
        worstPeak = std::max(worstPeak, std::abs(got));
    }

    // Measured at 3.5e-8, which is -149 dB. A hundredth of a decibel of
    // amplitude error would be 1.2e-3, so this has four orders of margin.
    INFO("worst error " << worst << " over ten minutes; peak " << worstPeak);
    REQUIRE(worst < 1.0e-6);

    // AND THE AMPLITUDE IS THE ONE ASKED FOR, which is the failure a rotation
    // has and a cosine does not: unit length lost to rounding shows up as a
    // carrier that quietly fades or grows.
    REQUIRE(worstPeak == Approx(amplitude).margin(1.0e-6));
}
