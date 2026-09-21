#pragma once

// dbx-style noise reduction: 2:1 on the way in, 1:2 on the way out.
//
// TWO HALVES ON OPPOSITE SIDES OF `PRINCIPLES §2`. The compressor is in the
// RECORD path, so what it does is magnetised onto the tape and is permanent;
// the expander is in the PLAYBACK path and stores nothing. That is not an
// implementation choice, it is what the machine is -- and it means a tape
// recorded with the system engaged and played back without it comes off
// ENCODED, levels rising and falling, rather than merely a little noisier.
//
// AND THE BREATHING IS NOT MODELLED. `SOURCES §53`: the expander raises its
// gain when the signal is loud, the tape's own hiss is inside that gain because
// it arrived between the two halves, and the decoder cannot tell hiss from
// signal. So the floor rises and falls with the music. It is audible only when
// the music has no high frequency content of its own to mask it -- a solo bass
// grows "a halo of noise", a full mix does not.
//
// So there is no breathing code here. Build both halves correctly, put the tape
// between them, and the halo arrives on its own. Anything that produced it by
// modelling it would not know that a bright signal masks its own noise, which
// is the half that makes it musical rather than periodic.
//
// TYPE II, WHICH IS THE CASSETTE ONE. It "rolls off the high and low-frequency
// response to desensitize the system to frequency response errors", where
// Type I assumes a medium with 60 dB of signal-to-noise before companding. A
// cassette does not have that.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <algorithm>
#include <cmath>

namespace chalkwalk::tape
{
    // ONE INSTANCE IS ONE HALF. The encoder and the decoder are separate cards
    // in the machine and separate objects here: each has its own detector state
    // and its own emphasis filter, and sharing them would make the decoder's
    // gain depend on what the encoder saw, which is precisely the coupling the
    // tape is supposed to break. Call `encode` on one and `decode` on another.
    class Compander
    {
    public:
        // ---- THE RATIO IS THE WHOLE SYSTEM ----
        //
        // 2:1 in decibels means a signal N dB below the reference is recorded
        // N/2 below it, and the expander doubles the distance back. The
        // detector is RMS and linear in decibels over a 60 dB window
        // (`SOURCES §53`), which is what makes the two halves complementary
        // across the range rather than near one level.
        static constexpr double kRatio = 2.0;
        static constexpr double kWindowDb = 60.0;

        // HF pre-emphasis before the compressor and its complement after the
        // expander. A corner "of about 10 kHz is an appropriate choice ... for
        // consumer Compact Cassette" (`SOURCES §53`); the AMOUNT is an
        // UNSOURCED RATE.
        // ---- AND THE DETECTOR IS BAND-LIMITED, WHICH IS WHAT MAKES IT
        //      TYPE II (`SOURCES §53`) ----
        //
        //   > it "rolls off the high and low-frequency response to desensitize
        //   > the system to frequency response errors", where Type I assumes a
        //   > medium with at least 60 dB of signal-to-noise before companding
        //
        // THE ROLL-OFF IS IN THE SIDE-CHAIN, NOT IN THE SIGNAL PATH. Rolling
        // off the signal would only make the machine duller and would
        // desensitise nothing. What buys the insensitivity is the DETECTOR not
        // looking where the medium is unreliable: a decoder whose detector sees
        // the whole band reads a dull tape as a QUIETER one and expands by the
        // wrong amount, so a worn head or a misaligned azimuth stops being a
        // frequency-response error and becomes a LEVEL error -- which is much
        // worse, because it moves the whole band and not just the part that was
        // wrong.
        //
        // THE CORNERS ARE **DECLARED**. No specification gives them, and the
        // mechanism says where they belong rather than what they are: above the
        // head bump and the low time constant at the bottom, below where gap
        // loss and azimuth bite at the top -- the octaves where a cassette's
        // response is worth trusting. 100 Hz and 5 kHz, one pole each, because
        // the point is to stop the detector STARING at the edges of the band
        // rather than to exclude them.
        static constexpr double kDetectorLowHz = 100.0;
        static constexpr double kDetectorHighHz = 5000.0;

        static constexpr double kEmphasisHz = 10000.0;
        static constexpr double kEmphasisDb = 12.0;
        static constexpr double kEmphasisGain = 3.98107170553497;  // 10^(12/20)

        void prepare(double sampleRateHz) noexcept
        {
            rate_ = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
            emphasisCoeff_ = onePole(kEmphasisHz, rate_);
            detectLowCoeff_ = onePole(kDetectorLowHz, rate_);
            detectHighCoeff_ = onePole(kDetectorHighHz, rate_);

            // ---- THE DETECTOR'S TIMING IS AN UNSOURCED RATE ----
            //
            // The patent literature describes a non-linear timing circuit with
            // several speeds -- slow to low frequencies so it does not distort
            // them, fast to high-level transients so it does not overload, and
            // fast to release so it does not modulate the noise -- and gives no
            // numbers (`SOURCES §53`). These are chosen to that description:
            // an attack fast enough not to overshoot a transient onto the tape,
            // and a release slow enough not to chatter on a bass note, which is
            // exactly the compromise that makes the halo audible.
            attack_ = std::exp(-1.0 / (0.005 * rate_));
            release_ = std::exp(-1.0 / (0.120 * rate_));
            reset();
        }

        void reset() noexcept
        {
            meanSquare_ = kFloor * kFloor;
            emphasisState_ = 0.0;
            lastIn_ = 0.0;
            lastOut_ = 0.0;
            detectLow_ = 0.0;
            detectHigh_ = 0.0;
        }

        // ---- ENCODE: RECORD SIDE, AND PERMANENT ----
        [[nodiscard]] double encode(double x) noexcept
        {
            const double pre = emphasise(x);
            const double db = detect(pre);
            // Halve the distance from the reference, in decibels.
            const double gainDb = -(db) * (1.0 - 1.0 / kRatio);
            return pre * std::pow(10.0, gainDb / 20.0);
        }

        // ---- DECODE: PLAYBACK SIDE, AND LIVE ----
        //
        // Detects what came OFF THE TAPE, which is the encoded signal plus
        // whatever the medium added to it. That is the whole mechanism of the
        // halo and it needs no help.
        [[nodiscard]] double decode(double x) noexcept
        {
            const double db = detect(x);
            const double gainDb = +(db) * (kRatio - 1.0);
            const double expanded = x * std::pow(10.0, gainDb / 20.0);
            return deemphasise(expanded);
        }

    private:
        // RMS, in decibels relative to the reference, clamped to the window the
        // real detector is linear over.
        [[nodiscard]] double detect(double x) noexcept
        {
            const double band = weigh(x);
            const double sq = band * band;
            const double c = (sq > meanSquare_) ? attack_ : release_;
            meanSquare_ = sq + c * (meanSquare_ - sq);
            const double rms = std::sqrt(std::max(meanSquare_, kFloor * kFloor));
            return std::clamp(20.0 * std::log10(rms), -kWindowDb, kWindowDb);
        }

        // ---- THE EMPHASIS PAIR, AND WHY THE SECOND HALF IS NOT A SHELF ----
        //
        // The pre-emphasis is a first-order high shelf: a one-pole lowpass
        // taken away from the signal gives the high part, and `g - 1` of it
        // added back lifts the top by `kEmphasisDb`.
        //
        // THE DE-EMPHASIS IS THE INVERSE FILTER, NOT THE MIRROR SHELF, and the
        // difference is the whole reason this comment exists. Building the
        // second half by running the same code with the reciprocal gain looks
        // exactly right -- +12 dB then -12 dB -- and is wrong, because two
        // mirrored shelves do not cancel. They agree at DC, where both are
        // unity, and at the top, where `g` and `1/g` multiply to one; BETWEEN
        // those they are both above their own asymptote and the product is a
        // BUMP. With a 10 kHz corner it peaks at +3.8 dB and reaches +2.5 dB at
        // 5 kHz, which is a companding system that brightens every tape it
        // touches.
        //
        // It was measured before it was derived: a round trip through the deck
        // came back +2.55 dB at 5 kHz and +3.74 at 10 kHz, and the closed form
        // below gives +2.52 and +3.80 for the pair alone -- so the error was
        // the filters and nothing else in the machine.
        //
        // THE UNIT TESTS COULD NOT SEE IT, which is the lesson worth keeping.
        // They asserted the round trip at four LEVELS and every one passed,
        // because a level is a question about the detector and this is a
        // question about frequency. There is now a test that sweeps.
        //
        // With `p = 1 - a` the pre-emphasis is
        //
        //     E(z) = [(1 + (g-1)p) - g p z^-1] / (1 - p z^-1)
        //
        // so its exact inverse is one pole and one zero:
        //
        //     y[n] = (x[n] - p x[n-1] + g p y[n-1]) / (1 + (g-1)p)
        //
        // and it is unconditionally stable for `p < 1`, because
        // `1 + (g-1)p - gp = 1 - p > 0` puts the pole inside the unit circle
        // for every corner and every amount.
        // The side-chain's band pass: a one-pole high pass under the low
        // corner and a one-pole low pass over the high one.
        [[nodiscard]] double weigh(double x) noexcept
        {
            detectLow_ += detectLowCoeff_ * (x - detectLow_);
            const double above = x - detectLow_;
            detectHigh_ += detectHighCoeff_ * (above - detectHigh_);
            return detectHigh_;
        }

        [[nodiscard]] double emphasise(double x) noexcept
        {
            emphasisState_ += emphasisCoeff_ * (x - emphasisState_);
            return x + (kEmphasisGain - 1.0) * (x - emphasisState_);
        }

        [[nodiscard]] double deemphasise(double x) noexcept
        {
            const double p = 1.0 - emphasisCoeff_;
            const double norm = 1.0 + (kEmphasisGain - 1.0) * p;
            const double y = (x - p * lastIn_ + kEmphasisGain * p * lastOut_) / norm;
            lastIn_ = x;
            lastOut_ = y;
            return y;
        }

        [[nodiscard]] static double onePole(double hz, double fs) noexcept
        {
            const double a = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * hz / fs);
            return std::clamp(a, 0.0, 1.0);
        }

        static constexpr double kFloor = 1.0e-7;

        double rate_ = 48000.0;
        double attack_ = 0.0, release_ = 0.0;
        double emphasisCoeff_ = 0.0, emphasisState_ = 0.0;
        double detectLowCoeff_ = 0.0, detectHighCoeff_ = 0.0;
        double detectLow_ = 0.0, detectHigh_ = 0.0;
        // The de-emphasis inverse's one zero and one pole.
        double lastIn_ = 0.0, lastOut_ = 0.0;
        double meanSquare_ = kFloor * kFloor;
    };
}
