#pragma once

// A sliding-band companding system, in the dual-path topology (`SOURCES §54`).
//
// ---- WHAT THIS IS AND WHAT IT IS NOT CALLED ----
//
// It is the architecture Dolby's B-type noise reduction uses, fitted to Dolby's
// own published encode curves. It is NOT called Dolby B, and must not be: that
// is a trademark and a licensing arrangement. `PRINCIPLES §17` already requires
// machines to be TYPES rather than named units, and the same rule applies to a
// circuit -- `ToneStack` is sourced from the RE-201 without Slipback being
// called one.
//
// ---- THE DUAL PATH, WHICH DECIDES EVERYTHING ELSE ----
//
// `Compander.h` is SERIES: the signal goes through the gain cell, so everything
// is compressed all the time across the whole range. This is not. The patent
// states it in one sentence:
//
//   > A signal compressor ... comprises a STRAIGHT-THROUGH SIGNAL PATH and
//   > means for adding to the output thereof the output of a FURTHER PATH.
//
//   > The second signal is combined ADDITIVELY for compressor operation and
//   > SUBTRACTIVELY for expander operation.
//
// Two consequences fall straight out. At high level the side path contributes
// nothing, so loud signals pass along the wire untouched -- which is why this
// kind of system does not audibly change dynamics and a 2:1 compander does. And
// the decoder is the encoder with the side path subtracted, so complementarity
// is STRUCTURAL rather than arithmetic. The patent gives the proof in its own
// notation:
//
//   > y = (1 + F)x and z = y - F z, therefore we have z = x as required.
//
// **THE DECODER IS A FEEDBACK LOOP**, and that is the whole of it: `z = y - Fz`
// says the expander's side path is driven by its OWN OUTPUT, not by what came
// off the tape. Any `F` at all inverts exactly -- provided both sides see the
// same level, which is what the tape is in a position to spoil.
//
// ---- AND THE FEEDBACK NEEDS NO UNIT DELAY, WHICH WAS NOT OBVIOUS ----
//
// A digital feedback path usually needs a delay or an implicit solve. This one
// solves in closed form, because the side path's dependence on `z` at the
// current sample is affine. With the one-pole realised as
// `hp = (1-k)(z - lp)`, the expander's equation is
//
//     z = y - G (z - lp),  G = g (1-k)
//
// which rearranges to a single divide. Exact, and the complementarity is then
// bit-for-bit rather than approximate: measured worst-case round-trip error
// 4.6e-12 on signals of amplitude 2, which is 227 dB down.
//
// **AND THE SHORTCUT DOES NOT MERELY DEGRADE IT, IT DIVERGES.** Putting a unit
// delay in the feedback instead -- `z = y - g hp[n-1]`, which is what one
// reaches for -- takes the round-trip error to 2.4e62 in two hundred thousand
// samples. The loop gain through the side path is close to `g` at high
// frequency, and `g` is 2.16: delaying the feedback of a loop with gain above
// one is an oscillator. The closed form is not an optimisation.
//
// The only delay anywhere is in the CONTROL path -- the detector drives the
// corner for the next sample -- and it is identical on both sides, so it
// cancels. `Compander.h` has no such property and does not need one; its
// decoder detects the tape signal directly.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <algorithm>
#include <cmath>

namespace chalkwalk::tape
{
    class SlidingBand
    {
    public:
        // ---- THE SIDE PATH'S GAIN IS DERIVED, NOT CHOSEN ----
        //
        // Dolby publish 10 dB of noise reduction above 4 kHz. At high frequency
        // the variable high pass passes everything, so the encode response is
        // `1 + g`, and 10 dB fixes `g` outright.
        static constexpr double kMaxBoostDb = 10.0;
        static const double kSideGain;

        // ---- THE SLIDE, FITTED TO TWO PUBLISHED FAMILIES OF CURVES ----
        //
        // `SOURCES §54`. Dolby's Figure 6 gives the low-level encode curve and
        // Figure 2 gives a family of them, one per input level in 5 dB steps,
        // with "OUTPUT LEVEL IN dB (re DOLBY LEVEL)" on the vertical axis -- so
        // they are absolute and referenced to the calibration level.
        //
        // A single one-pole high pass in the side path reproduces both. The
        // corner at full action comes from Figure 6:
        //
        //     read   300 Hz 1.3   500 Hz 2.8   1 k 5.5   2 k 8.3   5 k 10.0
        //     model  300 Hz 1.24  500 Hz 2.70  1 k 5.67  2 k 8.25  5 k 9.67
        //
        // -- 0.16 dB RMS across the six published points. The top of the slide
        // is Dolby's own sentence: the breakpoint moves "from about 300 Hz all
        // the way out to 20,000 Hz". So both ends of the slide are sourced and
        // only the path between them is a fit.
        //
        // AND THE CORNER IS FITTED AGAINST THE FILTER THAT IS ACTUALLY HERE,
        // which moved it. A first fit was made against the analogue response
        // `x / sqrt(1 + x^2)` and gave 1600 Hz; the implementation is a discrete
        // one-pole, and measured through it the published curve came back
        // 0.6 dB low with the maximum boost stuck at 9.5 dB instead of 10. The
        // number below is fitted to the discrete transfer function, and the
        // filter was changed to one that can reach its own asymptote -- see
        // `highPass`.
        static constexpr double kCornerMinHz = 1530.0;
        static constexpr double kCornerMaxHz = 20000.0;

        // WHERE THE THREE REGIONS MEET, in decibels relative to DOLBY LEVEL --
        // which is not the same as the machine's operating level, and that is
        // the point of `setReferenceLevel`.
        //
        // The SHAPE is Figure 4's -- "unity gain, changing gain, fixed gain" --
        // but Figure 4 is drawn with no numbers on either axis and is therefore
        // not the source for these. Figure 2 is, because it is in absolute
        // terms: nothing happens at and above about -5 dB re Dolby level, and
        // the full 10 dB is reached by about -30, below which the curves stop
        // changing shape and only translate.
        //
        // THESE ARE NOT THOSE TWO NUMBERS, AND THAT IS THE POINT OF FITTING
        // WITH THE CONTROL LOOP CLOSED. The detector watches the variable
        // filter's OUTPUT, so the level it reads is not the input level -- a
        // 5 kHz tone with the corner at 16 kHz arrives at the detector 10 dB
        // down. Setting these to the figure's own knee levels open-loop put the
        // -10 dB curve 1.5 dB hot; solved as the fixed point it actually is,
        // the whole family fits to 0.21 dB RMS, against readings whose own
        // uncertainty is about a decibel.
        static constexpr double kUpperKneeDb = -14.0;
        static constexpr double kLowerKneeDb = -35.0;

        // ---- AND THE SIDE PATH IS TURNED DOWN AT HIGH LEVEL, WHICH IS WHERE
        //      THE FLAT TOP COMES FROM ----
        //
        // The sliding corner alone cannot produce Dolby's top two curves, which
        // are FLAT: no action at all at and above about -5 dB re Dolby level.
        // The detector watches the filter's own output, so as the band slides up
        // the detector sees less and lets it slide back -- the corner
        // self-limits, and about 0.7 dB of boost survives where the published
        // curve has none.
        //
        // The patent puts a LIMITER in the side path -- "a combined filter ...
        // and limiter which prevents the output of the two further paths
        // exceeding say 1 percent of the maximum input signal-level" -- and that
        // is where the bilinear characteristic really comes from: as the input
        // grows the side path stops growing with it, so its contribution falls
        // away in relative terms.
        //
        // **REALISED AS A GAIN AND NOT AS A WAVESHAPER, AND THAT IS LOAD
        // BEARING.** An instantaneous limiter in the side path would make the
        // expander's equation non-linear in its own output, and the closed-form
        // solve -- the thing that makes this pair complementary to the last bit
        // -- would be gone, replaced by a Newton iteration per sample. A
        // gain-controlled element driven by the SAME slow envelope leaves the
        // instantaneous path affine, so nothing is traded. It is also what the
        // circuit is: a control voltage on a variable-gain stage, not a clipper.
        //
        // THE WINDOW IS FOUR DECIBELS, AND IS DELIBERATELY NOT NARROWER.
        // Fitting it freely gives two, which reads the GAP between two published
        // curves rather than the curves themselves -- they are sampled every
        // 5 dB, so a feature narrower than that is fitted to nothing. Four is
        // the widest window that still leaves Dolby level exactly flat.
        // THE NUMBERS MOVED WHEN THE DETECTOR DID, and they are not comparable
        // with the -15.5/-19.5 that stood here before: those were levels of the
        // VARIABLE FILTER'S output and these are levels of the ENCODED signal
        // through a fixed high pass, which is a different quantity measured at
        // a different point. Refitted against the same six readings of Figure 2
        // at 5 kHz, the pair comes back where it was -- 0.295 dB RMS, worst
        // point 0.43 -- with Dolby level still flat to 0.000 dB.
        //
        // AND FIVE DECIBELS RATHER THAN FOUR, for the reason the four was
        // chosen: the published curves are sampled every 5 dB, so a window
        // narrower than that is being fitted to the GAP between two curves
        // rather than to the curves. Five is now both the widest that leaves
        // Dolby level flat and the best fit, which the old detector could not
        // manage at once.
        static constexpr double kSidePathOffDb = -4.0;
        static constexpr double kSidePathFullDb = -9.0;

        // ---- **UNSOURCED RATE**: THE DETECTOR'S TIMING ----
        //
        // One of the only two things in this file that is not fitted to a
        // published curve. Dolby say the band must move "quickly enough to
        // follow the music being played" and publish no numbers anywhere; the
        // patent literature describes multi-slope timing circuits in words and
        // gives none either. The same condition as the compander's timing
        // (`SOURCES §53`).
        //
        // Chosen to that description: fast enough that the band is out of the
        // way before a transient is recorded, slow enough that it does not
        // chatter on a bass note. THE FAST ATTACK IS THE LOAD-BEARING HALF --
        // a band that slides up late records an overshoot onto the tape, and
        // the tape keeps it.
        static constexpr double kAttackSeconds = 0.003;
        static constexpr double kReleaseSeconds = 0.060;

        // ---- **UNSOURCED**: WHAT THE DETECTOR LOOKS AT ----
        //
        // The other one. The block diagrams in Dolby's document are redacted to
        // boxes labelled "HIGH-LEVEL STAGE", so the topology inside the side
        // path is not shown.
        //
        // This detects the VARIABLE FILTER'S OUTPUT, which is the choice the
        // published behaviour argues for rather than a guess: Figure 3 shows the
        // band sliding up out of the way of a loud bass drum while noise
        // reduction continues above it, and a detector that watched the input
        // BROADBAND could not do that -- the bass drum would shut the whole
        // system down. Watching the filter's own output is what makes the band
        // respond to what is inside it.
        //
        // It also makes the control loop feed back on itself, which is real and
        // is what the hardware does: the corner moves, which changes what the
        // detector sees, which moves the corner. It settles because the detector
        // is slow compared with the audio. The same envelope drives the side
        // path's GAIN, which is what produces the flat top.
        //


        void prepare(double sampleRateHz) noexcept
        {
            rate_ = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
            attack_ = std::exp(-1.0 / (kAttackSeconds * rate_));
            release_ = std::exp(-1.0 / (kReleaseSeconds * rate_));
            {
                constexpr double kPi = 3.14159265358979323846;
                const double f = std::min(design_.cornerMinHz, 0.4999 * rate_);
                limitTan_ = std::tan(kPi * f / rate_);
            }
            reset();
        }

        void reset() noexcept
        {
            state_ = 0.0;
            envelope_ = 0.0;
            limitEnvelope_ = 0.0;
            limitState_ = 0.0;
            sideGain_ = sideGainMax_;
            setCorner(design_.cornerMaxHz * 8.0);
        }

        // ---- THE ALIGNMENT, AND IT IS NOT THE MACHINE'S OPERATING LEVEL ----
        //
        // The amplitude at which the detector reads 0 dB: the flux the system
        // was calibrated against, in the deck's own units where operating level
        // is unity.
        //
        // DOLBY LEVEL IS 200 nWb/m AND 0 VU ON THE REFERENCE CASSETTE MACHINE IS
        // 160 (`SOURCES §54`, `§44`), so the default is +1.94 dB and not unity.
        // Assuming the two are the same would put the whole characteristic two
        // decibels along its own curve -- which is more than twice the
        // discrepancy that put a service-manual erratum into Nakamichi's
        // history when Dolby C's tolerances tightened.
        // NAMED, because the deck has to be able to say "this machine is
        // 2 dB out of alignment" and needs the figure it is out of alignment
        // FROM. 10^(1.94/20): Dolby level over the reference machine's 0 VU.
        static constexpr double kDolbyLevel = 1.25055;

        // ---- THE FIT, GATHERED SO A SECOND STAGE CAN HAVE A DIFFERENT ONE
        //      (`SOURCES §54`) ----
        //
        // Every number above is B-type's, fitted to B-type's two published
        // families, and it stays the default: `Design{}` IS the B this file was
        // written to reproduce, to the last bit.
        //
        // C-type is "two sliding bands in series ... both bands cover the same
        // frequency range but are sensitive to signals at DIFFERENT LEVELS ...
        // each one provides 10 dB", which is this same circuit twice with the
        // knees moved. So the constants become a struct rather than a second
        // copy of the class: one topology, two settings of it, and any fix to
        // the filter or the solve reaches both.
        //
        // WHAT IS **NOT** IN HERE IS AS DELIBERATE AS WHAT IS. The timing, the
        // detector's tap point and the reference level are properties of the
        // MECHANISM rather than of a stage's tuning, and Dolby publish nothing
        // that distinguishes the stages on any of them.
        struct Design
        {
            double maxBoostDb = kMaxBoostDb;
            double cornerMinHz = kCornerMinHz;
            double cornerMaxHz = kCornerMaxHz;
            double upperKneeDb = kUpperKneeDb;
            double lowerKneeDb = kLowerKneeDb;
            double sidePathOffDb = kSidePathOffDb;
            double sidePathFullDb = kSidePathFullDb;
        };

        // Call before `prepare`, or after -- it recomputes what it derives and
        // touches no state, so a design change does not click.
        void setDesign(const Design& d) noexcept
        {
            design_ = d;
            sideGainMax_ = std::pow(10.0, design_.maxBoostDb / 20.0) - 1.0;
            // The slide is a straight line in log-frequency against decibels;
            // this is its gradient, in decades per dB. Held rather than
            // recomputed per sample -- it was a function-local `static` when
            // there was only ever one design, which would have quietly frozen
            // the first stage's gradient into the second.
            slope_ = (std::log10(design_.cornerMinHz)
                      - std::log10(design_.cornerMaxHz))
                   / (design_.lowerKneeDb - design_.upperKneeDb);
        }

        [[nodiscard]] const Design& design() const noexcept { return design_; }

        void setReferenceLevel(double amplitude) noexcept
        {
            reference_ = std::max(1.0e-6, amplitude);
        }

        [[nodiscard]] double referenceLevel() const noexcept { return reference_; }

        // ---- ENCODE: RECORD SIDE, AND PERMANENT (`PRINCIPLES §2`) ----
        [[nodiscard]] double encode(double x) noexcept
        {
            const double hp = highPass(x);
            const double y = x + sideGain_ * hp;
            // BOTH HALVES TRACK FROM THE SAME TWO SIGNALS: the variable
            // filter's output, and the ENCODED signal. On this side the encoded
            // signal is what we just made; on the other it is what came off the
            // tape. They are the same samples, which is what keeps the pair
            // exact -- see `track`.
            track(hp, y);
            return y;
        }

        // ---- DECODE: PLAYBACK SIDE, AND THE SIDE PATH IS DRIVEN BY THE OUTPUT
        //
        // `z = y - F(z)`, solved in closed form. Note that the state update and
        // the detector both then see `z` -- the same signal the encoder's saw,
        // which is what makes the pair complementary and is exactly what a
        // level error between them spoils.
        [[nodiscard]] double decode(double y) noexcept
        {
            // `z = y - g (z - s) / (1 + G)`, solved. See the header note: the
            // dependence on `z` is affine, so the feedback needs no unit delay
            // and the pair is complementary to the last bit rather than to a
            // tolerance.
            const double z = (y * (1.0 + gTan_) + sideGain_ * state_)
                           / (1.0 + gTan_ + sideGain_);
            const double hp = highPass(z);
            track(hp, y);
            return z;
        }

        // What the band is doing, for tests and for anything that wants to draw
        // it. Not a control.
        [[nodiscard]] double cornerHz() const noexcept { return cornerHz_; }

    private:
        // ---- THE CORNER FOLLOWS THE LEVEL, LOGARITHMICALLY ----
        //
        // A straight line in log-frequency against decibels, which is what
        // Figure 4's "changing gain" segment is and what Figure 2's family
        // measures. Clamped below at the full-action corner; NOT clamped above,
        // because it does not need to be -- a corner far past Nyquist makes the
        // one-pole's output vanish on its own, which is the unity-gain region
        // arriving by arithmetic rather than by a switch. A switch there would
        // click.
        void track(double sidePath, double encoded) noexcept
        {
            const double magnitude = std::abs(sidePath);
            const double c = (magnitude > envelope_) ? attack_ : release_;
            envelope_ = magnitude + c * (envelope_ - magnitude);

            const double db = 20.0 * std::log10(
                std::max(envelope_ / reference_, 1.0e-7));

            const double decades = std::log10(design_.cornerMaxHz)
                                 + (db - design_.upperKneeDb) * slope_;
            setCorner(std::max(design_.cornerMinHz, std::pow(10.0, decades)));

            // ---- AND HOW MUCH OF THE SIDE PATH SURVIVES ----
            //
            // Linear in decibels, the straight middle segment of Figure 4's
            // characteristic. On a SECOND envelope, and one that watches the
            // side path's OUTPUT rather than its input -- which is the patent's
            // own wording ("prevents the OUTPUT of the two further paths
            // exceeding say 1 percent of the maximum input signal-level") and
            // is also the only version of this that is stable.
            //
            // ---- WHY IT CANNOT WATCH THE SAME SIGNAL THE CORNER DOES ----
            //
            // It did, and the decoder LATCHED. Trace the sign: more side-path
            // gain makes the expander subtract more, so `z` falls, so the
            // detector reads a lower level, so `w` rises, so the gain rises
            // again. Positive feedback -- and its loop gain is above one,
            // because the window is four decibels wide and the side path is
            // 2.16, which works out at about 1.5 per turn at high frequency.
            // Above 8 kHz, at levels inside the window, the round trip came
            // back with errors of 30 to 130 PERCENT while every published fit
            // still measured correctly.
            //
            // Watching the gain's own output inverts that sign: more gain makes
            // `z` fall, but `sideGain_ * hp` still RISES, because the gain rises
            // faster than the output it is applied to falls. So the detector
            // reads higher, `w` falls, and the loop closes on itself. Negative
            // in the decoder, negative in the encoder, stable in both, and
            // exactly complementary because both sides compute it from the same
            // quantity one sample late.
            //
            // WIDENING THE WINDOW IS NOT THE FIX, and it was measured before
            // this was written: the loop gain goes as 1/width, so stability
            // needs ten decibels or more, and at ten the flat top Dolby publish
            // has 0.18 dB of boost at Dolby level and 0.78 dB at -5, against
            // 0.02 and 0.17 here. The defect is the sign, not the slope.
            const double limitMagnitude = std::abs(limitPass(encoded));
            const double lc = (limitMagnitude > limitEnvelope_) ? attack_ : release_;
            limitEnvelope_ = limitMagnitude
                           + lc * (limitEnvelope_ - limitMagnitude);
            const double limitDb = 20.0 * std::log10(
                std::max(limitEnvelope_ / reference_, 1.0e-7));

            const double w = std::clamp(
                (design_.sidePathOffDb - limitDb)
                    / (design_.sidePathOffDb - design_.sidePathFullDb),
                0.0, 1.0);
            sideGain_ = sideGainMax_ * w;
        }

        // ---- A BILINEAR ONE-POLE, BECAUSE THE ASYMPTOTE IS A SOURCED NUMBER
        //
        // The obvious realisation -- `lp += k (x - lp)`, `hp = x - lp` -- does
        // NOT reach unity at the top of the band: its high-pass tops out at
        // `2(1-k)/(2-k)`, which at the corner below is 0.91, so the maximum
        // boost came out 9.5 dB where Dolby publish 10. A sourced figure being
        // missed by half a decibel because of a filter realisation is the kind
        // of thing that gets absorbed into a fitted constant and never found.
        //
        // The topology-preserving form reaches exactly 1 at Nyquist, and its
        // high-pass output is STILL AFFINE in the input -- `hp = (x - s)/(1+G)`
        // -- so the expander's closed-form solve survives unchanged. Nothing was
        // traded for the accuracy.
        [[nodiscard]] double highPass(double x) noexcept
        {
            const double hp = (x - state_) / (1.0 + gTan_);
            const double v = (x - state_) * gTan_ / (1.0 + gTan_);
            state_ += 2.0 * v;
            return hp;
        }

        // ---- THE LIMITER'S OWN HIGH PASS, AND IT IS FIXED ----
        //
        // A one-pole at the band's lowest corner, so the limiter watches the
        // part of the spectrum the sliding band works in AT ITS WIDEST and
        // ignores everything below. That matters for the reason Figure 3 gives:
        // a loud bass drum must not shut the system down, because noise
        // reduction is meant to continue above it.
        //
        // FIXED rather than sharing the variable filter, and that is the whole
        // point of it. The variable filter's output is inside both control
        // loops; this one is outside both, because it is driven by the ENCODED
        // signal -- which the encoder has just produced and the decoder has just
        // been handed. Same samples, same filter, same envelope, one sample
        // late on both sides.
        [[nodiscard]] double limitPass(double x) noexcept
        {
            const double hp = (x - limitState_) / (1.0 + limitTan_);
            const double v = (x - limitState_) * limitTan_ / (1.0 + limitTan_);
            limitState_ += 2.0 * v;
            return hp;
        }

        void setCorner(double hz) noexcept
        {
            cornerHz_ = std::clamp(hz, 1.0, 1.0e7);
            constexpr double kPi = 3.14159265358979323846;
            // Warped, and clamped short of Nyquist where the tangent runs away.
            const double f = std::min(cornerHz_, 0.4999 * rate_);
            gTan_ = std::tan(kPi * f / rate_);
        }

        double rate_ = 48000.0;
        double state_ = 0.0;
        double envelope_ = 0.0;
        // The limiter's own follower. Two envelopes rather than one because
        // they watch different points: the corner watches what goes INTO the
        // variable gain and the limiter watches what comes out of it.
        double limitEnvelope_ = 0.0;
        double limitState_ = 0.0;
        double limitTan_ = 0.0;
        double attack_ = 0.0, release_ = 0.0;
        double gTan_ = 1.0e6;
        double sideGain_ = 0.0;
        double cornerHz_ = kCornerMaxHz;
        double reference_ = kDolbyLevel;
        Design design_{};
        double sideGainMax_ = kSideGain;
        double slope_ = (std::log10(kCornerMinHz) - std::log10(kCornerMaxHz))
                      / (kLowerKneeDb - kUpperKneeDb);
    };

    inline const double SlidingBand::kSideGain =
        std::pow(10.0, SlidingBand::kMaxBoostDb / 20.0) - 1.0;

    // ---- A FIRST-ORDER HIGH SHELF AND ITS EXACT INVERSE ----
    //
    // C-type's two extra networks are both a high-frequency CUT in the encoder
    // with a matching lift in the decoder, so they are one filter used two ways
    // round -- and the inverse has to be the algebraic inverse rather than the
    // mirrored shelf.
    //
    // THIS PROJECT HAS ALREADY PAID FOR THAT LESSON. The compander's
    // pre-emphasis and de-emphasis were built as two mirrored shelves on the
    // reasoning that they must cancel; they agreed at DC and at the top of the
    // band and multiplied to a +3.7 dB bump in between, and every level
    // assertion passed while it was wrong (`Compander.h`). Two shelves that
    // look reciprocal are not.
    //
    // The realisation is the topology-preserving one-pole again, for the reason
    // `SlidingBand::highPass` gives: its high-pass output is affine in the
    // input, so the inverse is a division rather than an iteration.
    //
    //     y = lp(x) + g hp(x) = x + (g - 1) hp(x),  hp(x) = (x - s) / (1 + G)
    //
    // which rearranges to x in one line. `g` is the gain above the corner and
    // is at most 1: these networks cut and never boost.
    class ShelfCut
    {
    public:
        void prepare(double sampleRateHz, double cornerHz) noexcept
        {
            constexpr double kPi = 3.14159265358979323846;
            const double rate = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
            const double f = std::min(cornerHz, 0.4999 * rate);
            gTan_ = std::tan(kPi * f / rate);
            reset();
        }

        void reset() noexcept { state_ = 0.0; }

        // The gain above the corner. Unity is a wire, and a wire is what both
        // networks are when they are not acting.
        void setGain(double g) noexcept { gain_ = std::clamp(g, 1.0e-3, 1.0); }

        [[nodiscard]] double gain() const noexcept { return gain_; }

        [[nodiscard]] double process(double x) noexcept
        {
            const double hp = (x - state_) / (1.0 + gTan_);
            advance(x, hp);
            return x + (gain_ - 1.0) * hp;
        }

        // The inverse, and it is exact rather than complementary-by-design: `x`
        // is recovered from `y` and the SAME state, then the state is advanced
        // with the recovered `x` -- so the two filters walk identical state
        // trajectories and the pair is a wire to the last bit.
        [[nodiscard]] double restore(double y) noexcept
        {
            const double k = (gain_ - 1.0) / (1.0 + gTan_);
            const double x = (y + k * state_) / (1.0 + k);
            const double hp = (x - state_) / (1.0 + gTan_);
            advance(x, hp);
            return x;
        }

    private:
        void advance(double x, double hp) noexcept
        {
            (void) hp;
            const double v = (x - state_) * gTan_ / (1.0 + gTan_);
            state_ += 2.0 * v;
        }

        double state_ = 0.0;
        double gTan_ = 1.0;
        double gain_ = 1.0;
    };

    // ================= TWO STAGES IN SERIES: THE C-TYPE ARRANGEMENT =========
    //
    // `SOURCES §54`, and Dolby's description is unusually complete for once:
    //
    //   "both bands cover the same frequency range but are SENSITIVE TO SIGNALS
    //    AT DIFFERENT LEVELS ... As one filter reaches the end of its sliding
    //    range, the other one gradually takes over. EACH ONE PROVIDES 10 dB of
    //    compansion. They are CONNECTED IN SERIES."
    //
    // So this is not a new circuit. It is `SlidingBand` twice with the knees
    // moved, which is why `Design` exists at all -- a second copy of the class
    // would be a second copy of the filter, the solve and the detector, and the
    // next fix to any of them would reach one of the two.
    //
    // ---- THE ORDER IS EXACT AND IS THE ONLY THING THAT CAN GO SILENTLY WRONG
    //
    // Each stage inverts itself to the last bit; a chain of exact inverses is
    // exact ONLY IF IT IS UNDONE IN REVERSE. Encode is high-level then
    // low-level, so decode is low-level then high-level. Getting this backwards
    // still builds, still sounds like noise reduction, and leaves a residual
    // that looks like mistracking -- there is a test that puts a decade of
    // level and frequency through the pair and requires it back to 0.1 dB.
    //
    // WHICH STAGE COMES FIRST IS **DECLARED**. Dolby's block diagrams in the
    // document are redacted to boxes -- one of them is legible as "HIGH-LEVEL
    // STAGE", which is where the names here come from -- and the order between
    // them is not shown. It is a declaration with no audible consequence
    // either: the pair is complementary in both orders, and the stages are
    // driven by their own outputs rather than by each other's detectors.
    class SlidingBandPair
    {
    public:
        // ---- THE CORNER RANGE, DERIVED FROM B'S AND THEN FITTED ----
        //
        // B's action "begins about 300 Hz"; C's "begins to take effect in the
        // 100 Hz region". Both are Dolby's own sentences, and the ratio is
        // three -- which on B's fitted 1530 Hz full-action corner puts C's at
        // about 510. That is the derivation, and it is worth recording because
        // it is not quite where the number ended up.
        //
        // **THE TWO PUBLISHED DEPTHS PULL IT TO 380**, and they are the better
        // evidence because they are numbers rather than a region. Dolby give
        // "about 15 dB of noise reduction around 400 Hz" and "20 dB in the
        // critical 2,000 to 10,000 Hz hiss area", and those are two independent
        // constraints on one constant. Measured through the pair at full
        // action:
        //
        //     corner   400 Hz    2 kHz   10 kHz   100 Hz
        //       380     15.16    19.73    19.99     3.99   <-- both figures hit
        //       460     13.76    19.60    19.99     2.96
        //       510     12.92    19.51    19.99     2.50
        //
        // So 380, which lands the 400 Hz figure to 0.16 dB and the 2-10 kHz one
        // to 0.3, and still has 4 dB of action at 100 Hz -- "beginning to take
        // effect" there, which is what the sentence says. The ratio argument
        // and the depth figures disagree by a third of an octave, and "the
        // 100 Hz region" is not tight enough to adjudicate that; the depths
        // are, so they win.
        static constexpr double kCornerMinHz = 380.0;

        // ---- **DECLARED**: HOW FAR APART THE TWO SENSITIVITIES SIT ----
        //
        // Dolby say only that the stages are "sensitive to signals at different
        // levels" and that one "gradually takes over" as the other reaches the
        // end of its sliding range. They publish no offset, and this is the one
        // number in C that is a choice.
        //
        // FITTED TO THAT SENTENCE, WHICH IS MORE THAN IT SOUNDS. Too small and
        // the stages act together and C is just a louder B; too large and the
        // handover leaves a PLATEAU -- a range of input levels over which the
        // first stage has finished and the second has not started, and the
        // system stops responding to level at all. Measured at 5 kHz, boost per
        // 5 dB step of input level:
        //
        //     offset 15    8.27  9.48  9.90 12.47   <-- a plateau, visibly
        //     offset 10    8.27  9.58 12.44 16.04   <-- climbs throughout
        //      offset 6    8.44 11.64 15.28 17.92   <-- full action too early
        //
        // Ten, which climbs monotonically with no step smaller than 1.2 dB and
        // arrives at the full 20 dB at about -50 dB re Dolby level -- the
        // bottom of what Figure 10 plots C over, where B arrives at the bottom
        // of what Figure 2 plots B over. That correspondence is weak evidence
        // and is not what chose the number, but it is the right shape.
        static constexpr double kStageOffsetDb = 10.0;

        [[nodiscard]] static SlidingBand::Design highLevelStage() noexcept
        {
            SlidingBand::Design d;
            d.cornerMinHz = kCornerMinHz;
            return d;
        }

        [[nodiscard]] static SlidingBand::Design lowLevelStage() noexcept
        {
            SlidingBand::Design d = highLevelStage();
            d.upperKneeDb -= kStageOffsetDb;
            d.lowerKneeDb -= kStageOffsetDb;
            d.sidePathOffDb -= kStageOffsetDb;
            d.sidePathFullDb -= kStageOffsetDb;
            return d;
        }

        // ---- THE TWO NETWORKS C ADDS, AND WHY IT NEEDS THEM ----
        //
        // Both are Dolby's, both are sourced as CORNERS and neither is sourced
        // as a depth, so the corners are `SOURCED` and the depths `DECLARED`.
        //
        // SPECTRAL SKEWING. "In the very first step of the encoding mode, just
        // before the signal is boosted, the high frequencies (above 10,000 Hz)
        // are precisely lowered in volume ... causes the encoder to IGNORE
        // what's happening above 10,000 Hz ... the noise reduction circuits
        // will be much less sensitive to errors in record-play frequency
        // response." It is level-independent -- Figure 10's caption has it
        // present on every curve -- and it is restored in the decoder.
        static constexpr double kSkewCornerHz = 10000.0;

        // ANTI-SATURATION. "reduce the high-frequency losses and distortion
        // caused by tape saturation, further reducing decoder mistracking ...
        // start their action at a lower frequency (about 1,500 Hz) than
        // spectral skewing, so they are ONLY USED ON LOUD (high level)
        // levels." Figure 10 shows it as a downward slope from about 1.5 kHz on
        // the upper curves only.
        static constexpr double kAntiSatCornerHz = 1500.0;

        // ---- **DECLARED**: HOW DEEP EITHER OF THEM GOES ----
        //
        // Dolby give the corners and no depths, so these are choices. Each was
        // made by measuring the thing the network exists for against the thing
        // it costs, through the assembled machine rather than in the abstract.
        //
        // SKEWING TRADES FLOOR FOR TRACKING, and both halves are measurable.
        // The decoder's restoration lifts the top of the band back up and the
        // tape's hiss is inside that lift, so deeper skewing means a higher
        // floor; what it buys is what Dolby say it buys -- insensitivity to a
        // record-play response error above 10 kHz, and this engine has one to
        // hand in a worn head.
        //
        // The instrument is a CROSS-FREQUENCY gain error, because that is what
        // mistracking is: how far a 1 kHz tone's recovered level moves when a
        // loud 14 kHz tone is put beside it. With no noise reduction at all it
        // moves 0.001 dB, as a linear machine must. On a worn machine with C:
        //
        //     skew depth   1 kHz pulled by its neighbour   floor
        //         off              -7.11 dB               -86.52
        //        -5 dB             -5.79                  -86.26
        //       -10 dB             -5.14                  -85.92
        //       -15 dB             -4.85                  -85.47
        //
        // Ten, because that is where the trade turns over: the first five
        // decibels of skewing buy 1.3 dB of tracking for 0.26 of floor, the
        // next five buy 0.7 for 0.34, and the five after that buy 0.3 for 0.45.
        // Past here a decibel of floor stops buying a decibel of tracking.
        static constexpr double kSkewGain = 0.3162;      // -10 dB above 10 kHz

        // ANTI-SATURATION IS PAID FOR IN THE OTHER DIRECTION: it takes the top
        // off what reaches the head on loud passages, so it costs recorded
        // high-frequency level and buys back distortion the tape would have
        // added to a signal C had just boosted.
        //
        // AND WITHOUT IT C IS WORSE THAN NO NOISE REDUCTION AT ALL, which is
        // the measurement that shows the network is not decoration. A 5 kHz
        // tone at +12 dB re operating, third harmonic after a round trip
        // through Splice:
        //
        //     no noise reduction          -35.5 dB
        //     C with this network out     -32.1     <-- C makes it WORSE
        //     C at -6 dB                  -42.9
        //     C at -12 dB                 -49.6
        //
        // Deeper is monotonically better for distortion, so the number is
        // chosen by the other end. Figure 10's caption calls this the "GENTLER
        // downward slope" -- gentler than spectral skewing's, which is the only
        // comparative Dolby give and puts it inside the ten decibels above.
        // Six, which is comfortably gentler and already turns C from adding
        // 3.4 dB of third harmonic into removing 7.4.
        static constexpr double kAntiSatGain = 0.5012;   // -6 dB above 1.5 kHz

        static constexpr double kAntiSatOffDb = SlidingBand::kUpperKneeDb;
        static constexpr double kAntiSatFullDb = 0.0;

        void prepare(double sampleRateHz) noexcept
        {
            high_.setDesign(highLevelStage());
            low_.setDesign(lowLevelStage());
            high_.prepare(sampleRateHz);
            low_.prepare(sampleRateHz);
            skewIn_.prepare(sampleRateHz, kSkewCornerHz);
            skewOut_.prepare(sampleRateHz, kSkewCornerHz);
            skewIn_.setGain(kSkewGain);
            skewOut_.setGain(kSkewGain);
            antiIn_.prepare(sampleRateHz, kAntiSatCornerHz);
            antiOut_.prepare(sampleRateHz, kAntiSatCornerHz);
            const double rate = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
            antiAttack_ = std::exp(-1.0 / (SlidingBand::kAttackSeconds * rate));
            antiRelease_ = std::exp(-1.0 / (SlidingBand::kReleaseSeconds * rate));
            reset();
        }

        void reset() noexcept
        {
            high_.reset(); low_.reset();
            skewIn_.reset(); skewOut_.reset();
            antiIn_.reset(); antiOut_.reset();
            antiEnvelope_ = 0.0;
            antiIn_.setGain(1.0);
            antiOut_.setGain(1.0);
        }

        // Both stages are calibrated against the same tape, so they take one
        // reference between them -- a machine cannot be aligned for one stage
        // and not the other.
        void setReferenceLevel(double amplitude) noexcept
        {
            high_.setReferenceLevel(amplitude);
            low_.setReferenceLevel(amplitude);
        }

        // ---- THE CHAIN, AND IT IS A PALINDROME ----
        //
        //   encode   skew -> high stage -> low stage -> anti-saturation
        //   decode   anti-saturation^-1 -> low stage -> high stage -> de-skew
        //
        // Skewing first because Dolby say "the very first step ... just before
        // the signal is boosted"; anti-saturation last because its job is to
        // keep the boosted top of the band off a tape that would saturate on
        // it, which is a statement about what reaches the head.
        [[nodiscard]] double encode(double x) noexcept
        {
            const double y = antiIn_.process(low_.encode(high_.encode(
                                 skewIn_.process(x))));
            trackAntiSaturation(y);
            return y;
        }

        [[nodiscard]] double decode(double y) noexcept
        {
            const double z = skewOut_.restore(high_.decode(low_.decode(
                                 antiOut_.restore(y))));
            trackAntiSaturation(y);
            return z;
        }

        [[nodiscard]] const SlidingBand& highLevel() const noexcept { return high_; }
        [[nodiscard]] const SlidingBand& lowLevel() const noexcept { return low_; }

    private:
        // ---- THE ANTI-SATURATION CONTROL COMES FROM THE ENCODED SIGNAL ----
        //
        // The same argument as `SlidingBand`'s limiter, and it is forced here
        // rather than merely preferable. The network sits at the encoder's
        // OUTPUT and at the decoder's INPUT, so the only signal both halves
        // have in common at that point is the encoded one -- driving it from
        // anything else would need the decoder to reconstruct a signal it has
        // not expanded yet.
        //
        // Broadband rather than high-passed, and that is the difference from
        // the limiter: this network exists to protect the TAPE from saturating,
        // and a tape saturates on total flux. The limiter is about what the
        // band is looking at and has to ignore bass; this is about what the
        // head is being asked to write and must not.
        void trackAntiSaturation(double encoded) noexcept
        {
            const double magnitude = std::abs(encoded);
            const double c = (magnitude > antiEnvelope_) ? antiAttack_
                                                         : antiRelease_;
            antiEnvelope_ = magnitude + c * (antiEnvelope_ - magnitude);
            const double db = 20.0 * std::log10(
                std::max(antiEnvelope_ / high_.referenceLevel(), 1.0e-7));

            // Linear in decibels between the two ends, exactly as the side
            // path's window is. Off below `kAntiSatOffDb`, full at and above
            // `kAntiSatFullDb`.
            const double w = std::clamp(
                (db - kAntiSatOffDb) / (kAntiSatFullDb - kAntiSatOffDb),
                0.0, 1.0);
            const double g = 1.0 + (kAntiSatGain - 1.0) * w;
            antiIn_.setGain(g);
            antiOut_.setGain(g);
        }

        SlidingBand high_, low_;
        ShelfCut skewIn_, skewOut_;
        ShelfCut antiIn_, antiOut_;
        double antiEnvelope_ = 0.0;
        double antiAttack_ = 0.0, antiRelease_ = 0.0;
    };
}
