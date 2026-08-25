// ResamplerTest — the shared bandlimited resampler (9.25 R1).
//
// Verifies the three properties the DSP paths rely on:
//   * unity passthrough — rate 1 at integer positions reproduces the source.
//   * DC gain — a constant reads back as itself at every fraction and rate.
//   * pitch-up purity / anti-aliasing — reading faster than unity band-limits
//     the source so out-of-band content does NOT fold back, unlike a naive
//     Hermite read (the "bright sample pitched up aliases" defect, 9.24 audit).

#include "LegacyCheck.h"
#include "SpectralMeasure.h"

using measure::kPi;

#include <chalkwalk/tape/Resampler.h>

#include <cmath>
#include <vector>

    namespace
    {
        // Naive 4-point Hermite read at a fractional position (the pre-9.25 path),
        // for the anti-aliasing comparison.
        float hermiteRead(const std::vector<float>& s, double pos)
        {
            const int i0 = static_cast<int>(std::floor(pos));
            const int n = static_cast<int>(s.size());
            const auto at = [&](int i) {
                i = i < 0 ? 0 : (i >= n ? n - 1 : i);
                return s[static_cast<std::size_t>(i)];
            };
            const auto t = static_cast<float>(pos - std::floor(pos));
            // The reference this resampler has to beat, spelled out here rather
            // than imported: 4-point Hermite is what a naive fractional read
            // does, and the point of the test is that it aliases where the
            // polyphase bank does not. A reference the test owns cannot quietly
            // become the thing under test.
            const float ym1 = at(i0 - 1), y0 = at(i0), y1 = at(i0 + 1), y2 = at(i0 + 2);
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * t + c2) * t + c1) * t + y0;
        }
    }
TEST_CASE("resampler") {
        const chalkwalk::tape::Resampler rs;
        constexpr double kSr = 48000.0;

        // ── Unity passthrough ────────────────────────────────────────────────
        // rate 1, integer positions in the interior reproduce the source (the
        // kernel's group delay is compensated). A mixed-frequency signal.
        {
            const int n = 2048;
            std::vector<float> s(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                const double x = static_cast<double>(i);
                s[static_cast<std::size_t>(i)] = static_cast<float>(
                    0.5 * std::sin(0.013 * x) + 0.3 * std::sin(0.11 * x));
            }
            float worst = 0.0f;
            for (int p = 16; p < n - 16; ++p)
            {
                const float got = rs.read(s.data(), n, static_cast<double>(p), 1.0);
                worst = std::max(worst, std::abs(got - s[static_cast<std::size_t>(p)]));
            }
            CHECK_MSG(worst < 1.0e-3f,
                  "unity integer read reproduces the source (max err "
                  + std::to_string(worst) + ")");
        }

        // ── DC gain ──────────────────────────────────────────────────────────
        // A constant reads back as itself at every fraction and read rate.
        {
            std::vector<float> s(512, 0.75f);
            float worst = 0.0f;
            for (double rate : { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0 })
                for (double frac = 0.0; frac < 1.0; frac += 0.125)
                {
                    const float got = rs.read(s.data(), 512, 128.0 + frac, rate);
                    worst = std::max(worst, std::abs(got - 0.75f));
                }
            CHECK_MSG(worst < 5.0e-3f,
                  "DC passes at unity gain across fractions and rates (max err "
                  + std::to_string(worst) + ")");
        }

        // ── In-band content is preserved on pitch-up ─────────────────────────
        // A 4 kHz tone read at rate 2 (up an octave) → 8 kHz, well inside the
        // rate-2 cutoff (~12 kHz), so it survives with high purity.
        {
            // Long buffers so the tone resolves to a narrow FFT main lobe (a short
            // segment would smear it past the purity band and look impure).
            const int nIn = 40000;
            std::vector<float> s(static_cast<std::size_t>(nIn));
            for (int i = 0; i < nIn; ++i)
                s[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(
                    2.0 * kPi * 4000.0 * i / kSr));

            const int nOut = 16384;
            std::vector<float> out(static_cast<std::size_t>(nOut), 0.0f);
            for (int i = 0; i < nOut; ++i)
                out[static_cast<std::size_t>(i)] = rs.read(s.data(), nIn, 8.0 + 2.0 * i, 2.0);

            const float purity = measure::sinePurityDb(out, 8000.0, kSr);
            CHECK_MSG(purity > 40.0f,
                  "in-band pitch-up stays pure (8 kHz @ rate 2, purity "
                  + std::to_string(purity) + " dB)");
        }

        // ── Out-of-band content is rejected on pitch-up (anti-aliasing) ──────
        // An 18 kHz tone read at rate 2 would image to 36 kHz and fold back to
        // 12 kHz. The rate-2 kernel (cutoff ~12 kHz) attenuates the 18 kHz source,
        // so the resampler's aliased output is far quieter than a naive Hermite
        // read that passes the alias straight through.
        {
            const int nIn = 8192;
            std::vector<float> s(static_cast<std::size_t>(nIn));
            for (int i = 0; i < nIn; ++i)
                s[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(
                    2.0 * kPi * 18000.0 * i / kSr));

            const int nOut = 3072;
            std::vector<float> band(static_cast<std::size_t>(nOut), 0.0f);
            std::vector<float> naive(static_cast<std::size_t>(nOut), 0.0f);
            for (int i = 0; i < nOut; ++i)
            {
                const double pos = 8.0 + 2.0 * i;
                band[static_cast<std::size_t>(i)] = rs.read(s.data(), nIn, pos, 2.0);
                naive[static_cast<std::size_t>(i)] = hermiteRead(s, pos);
            }
            const double bandRms = measure::rms(band);
            const double naiveRms = measure::rms(naive);
            CHECK_MSG(bandRms < naiveRms * 0.5,
                  "pitch-up band-limits out-of-band content vs Hermite (band "
                  + std::to_string(bandRms) + " vs naive " + std::to_string(naiveRms) + ")");
            CHECK_MSG(bandRms < 0.1,
                  "aliased image is strongly attenuated (rms "
                  + std::to_string(bandRms) + ")");
        }

        // ── Scatter-add (R4): the transpose of read() ────────────────────────
        // At rate 1 on integer positions the kernel is a delta, so a scatter is a
        // bit-exact `+= in` at that position (the unity-overdub guarantee).
        {
            std::vector<float> buf(64, 0.0f);
            rs.scatterAddCircular(buf.data(), 64, 20.0, 1.0, 0.5f);
            CHECK_MSG(std::abs(buf[20] - 0.5f) < 1.0e-4f,
                  "scatter at an integer position, rate 1 → bit-exact delta");
            float leak = 0.0f;
            for (int i = 0; i < 64; ++i)
                if (i != 20) leak += std::abs(buf[static_cast<std::size_t>(i)]);
            CHECK_MSG(leak < 1.0e-4f, "scatter rate-1 delta has no spread into neighbours");
        }

        // A constant input stream scattered at rate 1 reconstructs the constant
        // (DC passes at unity through the scatter, circularly), matching read()'s DC.
        {
            std::vector<float> buf(128, 0.0f);
            for (int i = 0; i < 256; ++i)  // two full circular passes
                rs.scatterAddCircular(buf.data(), 128, static_cast<double>(i), 1.0, 0.75f);
            float worst = 0.0f;
            for (float v : buf) worst = std::max(worst, std::abs(v - 2.0f * 0.75f));
            CHECK_MSG(worst < 5.0e-3f,
                  "constant scattered over the loop reconstructs the constant (worst "
                  + std::to_string(worst) + ")");
        }

        // ── Scatter gain is amplitude-invariant across rates (9.28.1) ────────
        // The head signal law (DESIGN §40.10): a write at any speed reads back at
        // the input's amplitude. Kernel density is 1/rate, so without the |rate|
        // deposit gain a rate-2 pass would land at 0.5x and a half-speed pass at
        // 2x. Two full circular passes at each rate → expect exactly 2 * in.
        {
            for (const double rate : { 0.5, 2.0 })
            {
                const int len = 128;
                std::vector<float> buf(static_cast<std::size_t>(len), 0.0f);
                const int nIn = static_cast<int>(2.0 * len / rate);  // two passes
                for (int i = 0; i < nIn; ++i)
                    rs.scatterAddCircular(buf.data(), len,
                                          static_cast<double>(i) * rate, rate, 0.75f);
                float worst = 0.0f;
                for (float v : buf) worst = std::max(worst, std::abs(v - 2.0f * 0.75f));
                CHECK_MSG(worst < 2.0e-2f,
                      "varispeed scatter is amplitude-invariant (rate "
                      + std::to_string(rate) + ", worst " + std::to_string(worst) + ")");
            }
        }

        // ── A stalled write head writes (almost) nothing (9.28.1) ────────────
        // rate → 0 used to pile unbounded energy onto one spot; the |rate| gain
        // makes the deposit vanish with the speed — a scrub through zero fades.
        {
            const int len = 64;
            std::vector<float> buf(static_cast<std::size_t>(len), 0.0f);
            double pos = 20.0;
            for (int i = 0; i < 1000; ++i)
            {
                rs.scatterAddCircular(buf.data(), len, pos, 1.0e-4, 1.0f);
                pos += 1.0e-4;
            }
            float peak = 0.0f;
            for (float v : buf) peak = std::max(peak, std::abs(v));
            CHECK_MSG(peak < 0.2f,
                  "near-stall scatter deposits near-nothing (peak "
                  + std::to_string(peak) + ", was ~62 uncompensated)");
        }

        // ── Reverse write is the mirror of forward (9.28.1) ──────────────────
        // A constant written while the head runs backwards reconstructs the same
        // constant — the gain and cutoff depend on |rate| only.
        {
            const int len = 128;
            std::vector<float> fwd(static_cast<std::size_t>(len), 0.0f);
            std::vector<float> rev(static_cast<std::size_t>(len), 0.0f);
            for (int i = 0; i < 2 * len; ++i)
            {
                rs.scatterAddCircular(fwd.data(), len,  static_cast<double>(i), 1.0, 0.5f);
                rs.scatterAddCircular(rev.data(), len, -static_cast<double>(i), 1.0, 0.5f);
            }
            float worst = 0.0f;
            for (int i = 0; i < len; ++i)
                worst = std::max(worst, std::abs(rev[static_cast<std::size_t>(i)]
                                                 - fwd[static_cast<std::size_t>(i)]));
            CHECK_MSG(worst < 5.0e-3f,
                  "reverse scatter mirrors forward (worst " + std::to_string(worst) + ")");
        }

        // ── readCircular: seam continuity + rate-axis corners (9.28.3) ───────
        // On a buffer holding exactly one period of a tone, a circular read is
        // periodic across the seam (pos and pos+len agree), matches read() in the
        // interior, holds a defined value at rate 0, and is direction-agnostic.
        {
            const int len = 512;
            std::vector<float> s(static_cast<std::size_t>(len));
            for (int i = 0; i < len; ++i)  // 8 cycles → exactly periodic over len
                s[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(
                    2.0 * kPi * 8.0 * i / len));

            float worstSeam = 0.0f, worstInner = 0.0f, worstDir = 0.0f;
            for (double frac = 0.0; frac < 1.0; frac += 0.093)
            {
                // Periodicity: reads one whole loop apart agree, including reads
                // whose window straddles the seam.
                const double nearSeam = static_cast<double>(len) - 2.0 + frac;
                worstSeam = std::max(worstSeam,
                    std::abs(rs.readCircular(s.data(), len, nearSeam, 1.0)
                           - rs.readCircular(s.data(), len, nearSeam - len, 1.0)));

                // Interior agreement with the clamping read (window touches no edge).
                const double inner = 100.0 + frac;
                worstInner = std::max(worstInner,
                    std::abs(rs.readCircular(s.data(), len, inner, 1.0)
                           - rs.read(s.data(), len, inner, 1.0)));

                // Direction-agnostic: the reconstructed waveform at a position does
                // not depend on the travel direction (|rate| picks the kernel).
                worstDir = std::max(worstDir,
                    std::abs(rs.readCircular(s.data(), len, inner, 2.0)
                           - rs.readCircular(s.data(), len, inner, -2.0)));
            }
            CHECK_MSG(worstSeam < 1.0e-4f,
                  "circular read is periodic across the seam (worst "
                  + std::to_string(worstSeam) + ")");
            CHECK_MSG(worstInner < 1.0e-4f,
                  "circular read matches read() in the interior (worst "
                  + std::to_string(worstInner) + ")");
            CHECK_MSG(worstDir < 1.0e-6f,
                  "circular read is direction-agnostic (worst "
                  + std::to_string(worstDir) + ")");

            // Rate 0 (a parked or turning head) holds a finite, sensible value.
            const float held = rs.readCircular(s.data(), len, 100.25, 0.0);
            CHECK_MSG(std::isfinite(held) && std::abs(held) <= 0.6f,
                  "rate-0 circular read holds a bounded sample ("
                  + std::to_string(held) + ")");
        }
    }

// ── The kernel length follows the rate ───────────────────────────────────────
//
// A bucket band-limits to Nyquist/maxRate, so its sinc's zeros are maxRate
// source samples apart. Held in a FIXED sixteen-tap window that is 1.4 zero
// crossings at the top bucket, which is not a filter: measured before this
// changed, a scatter-write at rate 4.68 rippled by 1.90 dB where a correct
// interpolating kernel is flat, and the gather rejected a tone half an octave
// above what the rate could carry by only 13 dB.
//
// Both faults are a function of taps/rate rather than of taps, so the bank's
// rows scale with the bucket. These are the two properties that buys.
TEST_CASE("scatter-write partition of unity, at every rate") {
    const chalkwalk::tape::Resampler rs;

    // Deposit a CONSTANT and look at what lands. A correct interpolating kernel
    // sums flat whatever the spacing; a truncated one ripples, and the ripple is
    // the "written at the edge of the kernel rather than the middle" error.
    for (const double rate : { 1.0, 2.0, 2.34, 4.0, 4.68, 5.0 }) {
        const int len = 4096;
        std::vector<float> medium(static_cast<std::size_t>(len), 0.0f);
        const int deposits = static_cast<int>(len / rate) - 64;
        for (int i = 0; i < deposits; ++i)
            rs.scatterAddCircular(medium.data(), len, 64.0 + i * rate, rate, 1.0f);

        float lo = 1.0e30f, hi = -1.0e30f;
        for (int i = len / 3; i < len / 3 + 512; ++i) {
            lo = std::min(lo, medium[static_cast<std::size_t>(i)]);
            hi = std::max(hi, medium[static_cast<std::size_t>(i)]);
        }
        const double rippleDb = 20.0 * std::log10(static_cast<double>(hi / lo));
        INFO("rate " << rate << ": ripple " << rippleDb << " dB");
        // Flat to a hundredth of a decibel. The fixed-length kernel gave 1.90 dB
        // at 4.68, so this fails loudly if the length stops following the rate.
        CHECK(rippleDb < 0.05);
    }
}

TEST_CASE("the gather rejects what the rate cannot carry") {
    const chalkwalk::tape::Resampler rs;

    // A source tone half an octave above what the read rate can carry. Reading
    // at `rate`, source content above 0.5/rate folds into the output; this one
    // sits at 0.75/rate, so it lands at three quarters of the output rate and
    // folds to a quarter of it. It should not survive.
    for (const double rate : { 2.34, 4.68 }) {
        const int frames = 8000;
        // Long enough that the read never reaches the ends: read() CLAMPS at the
        // buffer edges, so running off one turns the tail into held DC and the
        // measurement reports that instead of the filter. It read -8 dB where
        // the answer is -58 until this was sized from the rate.
        const int len = static_cast<int>(2000.0 + frames * rate) + 256;
        std::vector<float> src(static_cast<std::size_t>(len));
        const double srcHz = 0.75 / rate;   // cycles per source sample
        for (int i = 0; i < len; ++i)
            src[static_cast<std::size_t>(i)] =
                static_cast<float>(std::sin(2.0 * kPi * srcHz * i));

        double acc = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double v = rs.read(src.data(), len, 1000.0 + i * rate, rate);
            acc += v * v;
        }
        const double level = std::sqrt(acc / frames) * std::sqrt(2.0);
        const double rejectionDb = 20.0 * std::log10(std::max(level, 1.0e-12));
        INFO("rate " << rate << ": " << rejectionDb << " dB survives");
        // Was -33 dB at 2.34 and -13 at 4.68 with a fixed sixteen taps.
        CHECK(rejectionDb < -45.0);
    }
}

TEST_CASE("the bank reaches rate 32, and shuttle rates are no longer clamped") {
    const chalkwalk::tape::Resampler rs;

    // WHAT THIS IS FOR. `bucketFor` clamps past the end of the bank, so before
    // the bank reached 32 a read at rate 28 was band-limited for 5.66 and
    // everything between the two folded. Measured on music through a tape
    // medium, a twelve-times shuttle aliased at -39 dB and a thirty-two-times
    // one at -23 dB, which is hash rather than a quiet cue.
    //
    // Same probe as the test above -- a tone half an octave above what the rate
    // can carry, which must fold and must not survive -- extended to the rates a
    // transport actually shuttles at.
    const auto survivingDb = [&rs](double rate) {
        const int frames = 4000;
        const int len = static_cast<int>(2000.0 + frames * rate) + 4096;
        std::vector<float> src(static_cast<std::size_t>(len));
        const double srcHz = 0.75 / rate;
        for (int i = 0; i < len; ++i)
            src[static_cast<std::size_t>(i)] =
                static_cast<float>(std::sin(2.0 * kPi * srcHz * i));

        double acc = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double v = rs.read(src.data(), len, 1000.0 + i * rate, rate);
            acc += v * v;
        }
        const double level = std::sqrt(acc / frames) * std::sqrt(2.0);
        return 20.0 * std::log10(std::max(level, 1.0e-12));
    };

    // Before the extension these rates all clamped to the 5.66 bucket, whose
    // cutoff is 0.088 cycles/sample -- and the probe at rate 32 sits at 0.023,
    // WELL INSIDE it, so it passed at essentially full level. Now it is stopped.
    //
    // The threshold is -25 and not -45 because of the bucket-top behaviour the
    // next test pins: a rate sitting exactly at its bucket's maxRate is the
    // worst case anywhere in the bank, and -28 dB is what the bank has always
    // given there. 8, 16 and 32 are bucket tops; 11.31 and 22.63 are not.
    for (const double rate : { 8.0, 11.31, 16.0, 22.63, 28.08, 32.0 }) {
        const double got = survivingDb(rate);
        INFO("rate " << rate << ": " << got << " dB survives");
        CHECK(got < -25.0);
    }

    // kTopRate names the last bucket, so a caller can decimate to get under it
    // instead of mirroring the number as a literal and watching it go stale.
    CHECK(chalkwalk::tape::Resampler::kTopRate == 32.0);

    // Past the top the anti-aliasing stops improving, and that is still true --
    // it is now true somewhere useful. Reads above the end share one bucket.
    std::vector<float> probe(2048);
    for (std::size_t i = 0; i < probe.size(); ++i)
        probe[i] = static_cast<float>(std::sin(0.31 * static_cast<double>(i)));
    CHECK(rs.read(probe.data(), 2048, 700.37, 33.0)
          == rs.read(probe.data(), 2048, 700.37, 1000.0));
}

TEST_CASE("the kernel length follows the rate, at kTapsPerRate") {
    // THE INVARIANT THE BANK EXISTS TO HOLD. Rejection and ripple depend on
    // taps/rate rather than on taps, so every bucket gets kTapsPerRate taps per
    // unit of the rate it serves, floored at the original sixteen.
    //
    // This replaces a test that asserted the halves were unchanged from the
    // eight-taps-per-rate bank. That claim was true when the only change was
    // ADDING buckets; raising kTapsPerRate to 12 deliberately changes all of
    // them, so asserting the old numbers would have been asserting the old
    // decision.
    const chalkwalk::tape::Resampler rs;

    constexpr double perRate = chalkwalk::tape::Resampler::kTapsPerRate;
    for (const double rate : { 0.5, 1.0, 1.2, 2.0, 2.82842712, 4.0,
                               5.65685425, 8.0, 16.0, 32.0 }) {
        const int half = rs.halfFor(rate);
        INFO("rate " << rate << " -> half " << half);
        // The bucket serving `rate` has maxRate >= rate, so its half is at
        // least what `rate` itself demands, and never more than the next
        // half-octave up demands.
        CHECK(half >= chalkwalk::tape::Resampler::kMinHalf);
        CHECK(2 * half >= static_cast<int>(perRate * rate) - 1);
        // The upper bound has to admit the FLOOR: at rate 0.5 the kernel is
        // sixteen taps because kMinHalf says so, not because the rate asked.
        CHECK(2 * half <= std::max(2 * chalkwalk::tape::Resampler::kMinHalf,
                                   static_cast<int>(perRate * rate * 1.4143) + 4));
    }

    // The floor really is a floor: nothing below rate 1.33 is shortened by it.
    CHECK(rs.halfFor(0.25) == chalkwalk::tape::Resampler::kMinHalf);
    CHECK(rs.halfFor(1.0) == chalkwalk::tape::Resampler::kMinHalf);

    // And the ceiling is not binding at the top bucket, which it would be if
    // kTapsPerRate rose again without kMaxHalf following.
    CHECK(rs.halfFor(chalkwalk::tape::Resampler::kTopRate)
          < chalkwalk::tape::Resampler::kMaxHalf + 1);
    CHECK(rs.halfFor(chalkwalk::tape::Resampler::kTopRate)
          == static_cast<int>(std::ceil(perRate * 0.5
                                        * chalkwalk::tape::Resampler::kTopRate)));
}

TEST_CASE("worst-case rejection is at the TOP of a bucket, and is -49 dB") {
    // WHERE THE BANK IS WEAKEST, pinned because it was not written down and was
    // -28 dB until kTapsPerRate went from 8 to 12. It is not a consequence of
    // extending the bank: it holds identically at every bucket, including the
    // ones that have been there all along.
    //
    // A bucket band-limits to Nyquist/maxRate, and the probe tone sits at
    // 0.75/rate. When `rate == maxRate` the tone is 1.5x the cutoff -- which is
    // inside the TRANSITION, because a Kaiser of bandwidth 9 over 8*maxRate taps
    // has a transition 1.125/maxRate wide against a passband only 0.5/maxRate
    // wide. The transition is more than twice the passband, so the stopband
    // barely exists and what saves the common case is having margin below the
    // bucket top rather than the filter's own rolloff.
    //
    // Half-octave spacing puts every rate in [0.707, 1.0] of its bucket, so the
    // top is always reachable and this IS the bank's worst case. Twelve taps per
    // unit of rate takes it to -49; the cutoff guard was the alternative and
    // lost on rejection, passband and transient peak at once (kCutoffGuard).
    const chalkwalk::tape::Resampler rs;

    const auto survivingDb = [&rs](double rate) {
        const int frames = 4000;
        const int len = static_cast<int>(2000.0 + frames * rate) + 4096;
        std::vector<float> src(static_cast<std::size_t>(len));
        for (int i = 0; i < len; ++i)
            src[static_cast<std::size_t>(i)] =
                static_cast<float>(std::sin(2.0 * kPi * (0.75 / rate) * i));
        double acc = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double v = rs.read(src.data(), len, 1000.0 + i * rate, rate);
            acc += v * v;
        }
        return 20.0 * std::log10(
            std::max(std::sqrt(acc / frames) * std::sqrt(2.0), 1.0e-12));
    };

    // Scale-invariant: the same shape at an old bucket and a new one. If these
    // ever diverge, the cutoff has stopped being a pure function of maxRate.
    for (const double bucket : { 2.82842712, 5.65685425, 32.0 }) {
        INFO("bucket " << bucket);
        CHECK(survivingDb(bucket * 0.72) < -120.0);   // bottom: excellent
        CHECK(survivingDb(bucket * 0.90) < -80.0);    // middle
        CHECK(survivingDb(bucket) < -45.0);           // top: still the worst case
    }
}

TEST_CASE("the cutoff guard does not touch the unity bucket") {
    // THE PROMISE IT NEARLY BROKE. At rate 1 on an integer position the kernel
    // must be a delta, so a write deposits the sample exactly and a rate-1 round
    // trip is transparent. Applying the guard everywhere made the unity bucket a
    // 0.4-cutoff lowpass and broke both -- caught by the scatter's bit-exact
    // test and by the echo tests, which is why the guard skips it.
    //
    // It is principled rather than an exemption: at or below rate 1 the read
    // interpolates instead of decimating, so nothing folds and band-limiting is
    // pure loss.
    const chalkwalk::tape::Resampler rs;

    std::vector<float> impulse(64, 0.0f);
    impulse[32] = 1.0f;
    // Read the impulse back at unity, on integer positions: it must come back
    // as itself, with nothing smeared either side.
    CHECK(std::abs(rs.read(impulse.data(), 64, 32.0, 1.0) - 1.0f) < 1.0e-6f);
    for (const double at : { 29.0, 30.0, 31.0, 33.0, 34.0, 35.0 }) {
        INFO("neighbour " << at);
        CHECK(std::abs(rs.read(impulse.data(), 64, at, 1.0)) < 1.0e-6f);
    }

    // And DC passes at unity through every bucket, guard or not, because each
    // phase is normalised. A guard that broke this would be a gain error.
    std::vector<float> dc(4096, 1.0f);
    for (const double rate : { 0.5, 1.0, 2.34, 5.0, 20.0, 32.0 }) {
        INFO("rate " << rate);
        CHECK(std::abs(rs.read(dc.data(), 4096, 2000.37, rate) - 1.0f) < 1.0e-5f);
    }
}
