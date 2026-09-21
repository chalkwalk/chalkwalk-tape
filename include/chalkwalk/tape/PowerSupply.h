#pragma once

// THE MACHINE'S POWER SUPPLY (`DESIGN.md` §4.11, `SOURCES §21`).
//
// Two effects live here and they are one object because they are one cause: a
// reservoir capacitor that is not what it was. `SOURCES §21` establishes that
// ageing electronics are almost entirely aluminium electrolytics -- film,
// ceramic, resistors, transformers and cores do not drift -- and that the
// audible consequences are NOT a duller machine:
//
//     "ageing electronics do not make a machine duller, they make it noisier,
//      softer under load, and out of tune with itself"
//
// ---- ONE: HUM, WHICH IS TWO EFFECTS AT TWO FREQUENCIES ----
//
// The service literature's diagnostic is a FREQUENCY difference, which is why
// it is worth having: **line frequency indicates a grounding or shielding
// problem; twice line frequency indicates the power supply** (`SOURCES §21`,
// and flagged there as the weakest provenance in that file -- it is used for
// the CLASSIFICATION only, never for a level).
//
// So one symptom sits on both of `DESIGN.md` §7.2's clocks. A shield, a ground
// and a head lead dress are things a visit fixes, so the line-frequency
// component runs on NEGLECT. A reservoir only new capacitors fix, so the
// twice-line component runs on WEAR and no amount of servicing touches it.
//
// **THE LEVELS ARE DECLARED, AND THE SEARCH FOR A PUBLISHED ONE IS RECORDED**
// (`SOURCES §59`). The Otari MTR-90 III -- the machine this project's whole
// noise floor is calibrated from -- publishes signal-to-noise, flutter,
// response, crosstalk, distortion and erase efficiency, and NO hum figure at
// all; IEC 60094-1 and DIN 45500-4 define the measurement and are paywalled.
// What exists is a CEILING, and it is enough to build against: that 69 dB is
// UNWEIGHTED over 30 Hz to 18 kHz, so it already contains whatever hum the
// machine makes, and hum must therefore sit far enough below it not to
// dominate the sum. A machine whose hum HAS risen to dominate is exactly the
// neglected machine we want to be able to build.
//
// ---- TWO: RAIL SAG, WHICH IS THE ONE THAT SOUNDS LIKE SOMETHING ----
//
// A tired reservoir cannot hold the rail through a loud passage, so the supply
// is MODULATED BY THE PROGRAMME: low-order distortion plus a slow level
// dependence, audibly a softening and a slight bloom on exactly the material
// that is already hitting the tape hard. It is dynamic rather than static,
// which is why no equalisation curve can stand in for it.
//
// ---- AND ONE SUPPLY FEEDS EVERY CHANNEL, WHICH IS THE INTERESTING PART ----
//
// This is the same argument `PRINCIPLES §3` makes about the transport, in the
// electronics: a machine has ONE power supply, so the rail every amplifier
// works against is the same rail. A loud track therefore ducks a quiet one --
// sixteen channels of a studio deck share one set of rails, and a chorus that
// slams track 1 moves track 8 with it.
//
// That is not a side effect to be tolerated; on a machine with tired
// electrolytics it is a large part of what "it glues" means, and it is
// something no per-channel model can produce however carefully it is tuned.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <algorithm>
#include <cmath>

namespace chalkwalk::tape
{
    class PowerSupply
    {
    public:
        struct Config
        {
            // ---- WHERE THE MACHINE IS PLUGGED IN ----
            //
            // A property of the ELECTRICITY and not of the deck: the same
            // machine hums at 50 Hz in Europe and 60 in America, and at twice
            // that in each. 50 is the default because it has to be something;
            // `SOURCES §21`'s diagnostic is quoted in its American form.
            double lineHz = 50.0;

            // Amplitude at line frequency, relative to the level an
            // operating-level tone reproduces at. NEGLECT: shielding, grounding
            // and head lead dress, all of which a visit fixes.
            double humLine = 0.0;

            // Amplitude at TWICE line frequency. WEAR: supply ripple, which
            // only new capacitors fix.
            double humRipple = 0.0;

            // How far the rail droops when the machine is working hard, as a
            // fraction. 0 is a supply that does not sag at all.
            double sagDepth = 0.0;

            // The load at which `sagDepth` is reached -- the sum of what every
            // amplifier is putting out, in the same units as the signal, so
            // "one" is roughly one channel at operating level.
            double sagReference = 4.0;

            // THE RESERVOIR'S OWN TIME CONSTANT, and it is what makes this
            // dynamic rather than a compressor. Long enough that a kick does
            // not duck the mix -- that would be a gate with a bad attack -- and
            // short enough that a sustained loud passage visibly pulls the rail
            // down and lets it back up afterwards.
            //
            // **DECLARED.** A real supply's droop and recovery are set by the
            // reservoir, the load and the rectifier's conduction angle, and no
            // machine publishes any of the three.
            double sagSeconds = 0.08;
        };

        void prepare(double sampleRateHz, const Config& config) noexcept
        {
            rate_ = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
            config_ = config;

            // Two phase increments, and the second is exactly twice the first
            // rather than separately computed -- they are one mains cycle seen
            // in two ways, and letting them drift apart would be a bug that
            // sounded like chorus.
            const double w = 2.0 * kPi * std::max(0.0, config_.lineHz) / rate_;
            lineStep_ = w;
            rippleStep_ = 2.0 * w;

            const double tau = std::max(1.0e-4, config_.sagSeconds);
            sagCoeff_ = std::exp(-1.0 / (tau * rate_));

            reset();
        }

        void reset() noexcept
        {
            linePhase_ = 0.0;
            ripplePhase_ = 0.0;
            load_ = 0.0;
        }

        [[nodiscard]] bool active() const noexcept
        {
            return config_.humLine > 0.0 || config_.humRipple > 0.0
                || config_.sagDepth > 0.0;
        }

        // ---- THE RAIL THIS SAMPLE, FROM THE LOAD SO FAR ----
        //
        // Causal, deliberately: the rail a sample works against is what the
        // PRECEDING programme left it at. A supply that sagged from a sample it
        // had not yet passed would be a look-ahead compressor, which is a
        // different machine and a much more modern one.
        //
        // 1.0 is the nominal rail. Never negative, and never above nominal: a
        // reservoir does not overshoot upwards.
        [[nodiscard]] double rail() const noexcept
        {
            if (config_.sagDepth <= 0.0 || config_.sagReference <= 0.0)
                return 1.0;
            const double fraction = std::min(1.0, load_ / config_.sagReference);
            return 1.0 - config_.sagDepth * fraction;
        }

        // What every amplifier on this machine is drawing, summed, once a
        // sample. The rectified magnitude, because a supply does not care which
        // way the signal went.
        void draw(double total) noexcept
        {
            const double want = std::abs(total);
            load_ = sagCoeff_ * load_ + (1.0 - sagCoeff_) * want;
        }

        // ---- AND THE HUM, WHICH IS ADDED AND NOT MULTIPLIED ----
        //
        // It is a voltage on the rail that gets into the signal path, so it is
        // there whether or not anything is playing -- which is exactly how you
        // find it on a real machine, by turning everything down and listening.
        //
        // Advances the oscillators, so it is called ONCE A SAMPLE for the
        // machine and its result shared by every channel. Calling it per track
        // would give each track its own hum at its own phase, which is sixteen
        // supplies rather than one.
        [[nodiscard]] double hum() noexcept
        {
            if (config_.humLine <= 0.0 && config_.humRipple <= 0.0)
                return 0.0;

            const double v = config_.humLine * std::sin(linePhase_)
                           + config_.humRipple * std::sin(ripplePhase_);

            linePhase_ += lineStep_;
            ripplePhase_ += rippleStep_;
            if (linePhase_ >= kTwoPi) linePhase_ -= kTwoPi;
            if (ripplePhase_ >= kTwoPi) ripplePhase_ -= kTwoPi;
            return v;
        }

        [[nodiscard]] const Config& config() const noexcept { return config_; }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static constexpr double kTwoPi = 2.0 * kPi;

        Config config_{};
        double rate_ = 48000.0;
        double lineStep_ = 0.0, rippleStep_ = 0.0;
        double linePhase_ = 0.0, ripplePhase_ = 0.0;
        double sagCoeff_ = 0.0;
        double load_ = 0.0;
    };
}
