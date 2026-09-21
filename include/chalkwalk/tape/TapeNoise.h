#pragma once

// The two noise floors, and they are two because they behave differently
// (DESIGN.md sections 4.7 and 4.11, SOURCES section 27).
//
// PARTICULATE NOISE IS ON THE TAPE. A magnetic coating is a finite number of
// discrete particles, randomly oriented, and the head reads a finite sample of
// them -- so the granularity is audible on tape nobody has damaged. Noise power
// goes as the reciprocal of the number of particles in the read volume, which
// makes a narrow track inherently noisier than a wide one: Splice is noisier
// than Capstan BY DERIVATION rather than by a tier offset (`PRINCIPLES §6`).
//
// PREAMP NOISE IS IN THE MACHINE. A reproduce head puts out millivolts, so
// there is a high-gain amplifier in front of everything, and its Johnson noise
// does not scale with the tape at all.
//
// THE SPLIT IS THE WHOLE POINT, AND IT IS `PRINCIPLES §2` AGAIN:
//
//   particulate   generated in the coating, so it goes through the head losses,
//                 and it is ADDRESSED IN SPACE -- the same inch of tape reads
//                 the same noise twice, backwards backwards, and an octave up
//                 at double speed, because the granularity is frozen in the
//                 medium
//   preamp        generated after the head, so no head loss applies, and it is
//                 addressed in TIME, because it is a live electronic process
//                 that knows nothing about where the tape is
//
// AND IT IS WHY WEAR DESTROYS INFORMATION RATHER THAN TURNING THE VOLUME DOWN.
// Signal amplitude follows the coating; particulate noise follows its SQUARE
// ROOT, because the particles add in random phase; the preamp follows nothing.
// So as a region wears, both floors rise relative to the signal, and at the
// limit there is hiss with nothing in it. Turn the gain up and you get louder
// hiss, not recovered music. None of that is modelled -- it is subtraction.
//
// THE LEVEL IS SOURCED. `SOURCES §27`: the Otari MTR-90 III's published
// signal-to-noise ratio is 69 dB at IEC 15 ips, unweighted over 30 Hz to
// 18 kHz, referenced to 1040 nWb/m -- and 15 ips is Capstan's nominal speed, so
// the calibration machine and the modelled machine are the same machine.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <chalkwalk/tape/LossEffects.h>


// `<algorithm>` for std::clamp, and it is here because its ABSENCE compiled.
// This file reached it transitively through a C++20 libstdc++ header and built
// clean at -std=c++20 for months; at -std=c++17 -- which is chalkwalk-tape's
// floor, and therefore what this file must satisfy to be promoted -- it does
// not, and the error is a missing name rather than a missing include.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chalkwalk::tape
{
    struct NoiseConstants
    {
        // SOURCED (`SOURCES §27`). Otari MTR-90 III, IEC 15 ips, unweighted,
        // 30 Hz to 18 kHz, at 1040 nWb/m. We identify that reference fluxivity
        // with our own operating level, which is what a machine lined up to
        // 0 VU gives at full-scale input (`RecordChain::operatingFieldAmPerM`).
        double referenceSnrDb = 69.0;

        // HOW THAT TOTAL DIVIDES between the tape and the electronics, at the
        // machine it was measured on. **CALIBRATED**, and it is the one
        // assumption in this project that was made for want of anything better
        // and has since been CHECKED AND SURVIVED (`SOURCES §45`, `§46`).
        //
        // THE ROUTE: the machine's specification gives the SUM, a tape data
        // sheet gives the TAPE, and the split is arithmetic. Both numbers
        // exist -- Ampex 456 at 64.3 dB weighted against a reference level of
        // 260 nWb/m at 15 in/s, and the Ampex MM-1200 at 64 dB UNWEIGHTED
        // against peak record level with 456 named as the tape.
        //
        // WHAT UNLOCKED IT WAS A FLUX LEVEL. The MM-1200 states that "peak
        // record level corresponds to a tape flux of 520 nWb/m", which is
        // +6.0 dB over the sheet's reference level -- NOT the +12.3 dB that
        // separates reference level from 3 % distortion. An earlier reading
        // here assumed peak record level WAS the 3 % point, made the tape six
        // decibels too quiet, and concluded that the electronics dominate by
        // 5 to 9 dB and that this split was wrong by seven. They do not, and
        // it is not.
        //
        // AND THE TRACK WIDTH IS AMPEX'S OWN. Tape noise goes as
        // 1/sqrt(width) and electronics noise does not, so the split turns on
        // the width the sheet was measured at. MRL gives 1.9 mm as the AMPEX
        // STANDARD two-track width (`SOURCES §46`) -- what an Ampex sheet on an
        // Ampex reference recorder would use -- and it is 0.28 dB from the
        // MM-1200's own 1.78 mm track, so almost no correction is needed
        // between them. It is also inside the 3.41 mm the arithmetic
        // independently allows, since a machine cannot be quieter than its own
        // tape and a full-track quarter-inch reading would make it so.
        //
        //     tape alone          66.5 dB
        //     electronics alone   67.6 dB
        //     this equal split    67.01 dB each
        //
        // AND A SECOND MACHINE OF THE SAME CLASS MOVES IT SEVERAL DECIBELS, so
        // read the above as one reading rather than the answer (`SOURCES §48`).
        // The Otari MX-80 -- also 2-inch, also on 456, also unweighted 30 Hz to
        // 18 kHz -- quotes 56 dB at 320 nWb/m where the MM-1200 normalises to
        // 59.8 dB at the same flux. Two machines of one class, 3.8 dB apart.
        //
        // Carried through with the weighting correction spanning the 3 dB of
        // `§44` and the 5 dB of `§47`, the electronics lead by -5.7 dB against
        // the MM-1200 and by +5.8 dB against the MX-80. **The documents bracket
        // the split at about plus or minus six decibels around equal, and equal
        // is the centre of that bracket.**
        //
        // So each still contributes 3.01 dB below the total, and the standing
        // of that number is: the centre of everything two machines and a tape
        // sheet allow, which is a better reason than "least committal" and a
        // weaker claim than "measured". It is not pinned.
        //
        // It is an assumption at ONE machine and everything else is derived:
        // the particulate term scales with the read volume, so Splice's much
        // narrower track makes it tape-dominated without a second constant, and
        // a wide-track deck stays closer to its electronics.
        double preampShareDb = 3.01;

        // HOW MUCH WORSE THIS MACHINE'S ELECTRONICS ARE than the one the whole
        // floor was calibrated on, in decibels. Set from the machine's GRADE
        // (`MachineGeometry.h`), zero for the studio deck that IS the
        // reference.
        //
        // IT TOUCHES THE ELECTRONICS AND NOTHING ELSE. `particulateAmplitude`
        // does not read it and must not: the tape's floor is a property of the
        // coating and the read volume, and a cheap machine threaded with good
        // tape has a good tape floor and bad amplifiers. That separation is the
        // entire reason grade is a second axis rather than a quality dial.
        double electronicsPenaltyDb = 0.0;

        // The reference machine's read volume, in cubic metres: Capstan's
        // reproduce head. Track width times coating thickness times gap length
        // is the slab of coating the gap can see at one instant, and the
        // particle count is proportional to it.
        [[nodiscard]] static double referenceVolume() noexcept
        {
            // Capstan's reproduce geometry, spelled out rather than included,
            // so this header does not depend on the machine table it calibrates
            // against (`SOURCES §14`, §25, §13).
            constexpr double trackWidth = 70.0 * 25.4e-6;   // 70 mil
            constexpr double thickness = 11.9e-6;
            constexpr double gap = 6.0e-6;
            return trackWidth * thickness * gap;
        }
    };

    // TWO CONVERSIONS BETWEEN A SPECIFICATION AND A GENERATOR, both of which
    // are arithmetic and neither of which is obvious enough to leave implicit.
    // Getting them wrong measured 72.6 dB against a 69 dB specification, which
    // is the kind of error that looks like a good machine.
    //
    //   the reference is an RMS   a signal-to-noise ratio is quoted against a
    //                            SINE at reference level, whose RMS is 1/sqrt2
    //                            of its peak -- so a floor referenced to peak
    //                            is 3 dB too quiet
    //   the generator is uniform  white noise uniform on [-1, 1) has an RMS of
    //                            1/sqrt3, not 1 -- so an amplitude used as
    //                            though it were an RMS is 4.8 dB too quiet
    //
    // Together: multiply the specified RMS by sqrt(3) to get the generator's
    // amplitude, and by 1/sqrt(2) to refer it to a sine's RMS.
    inline constexpr double kSineRmsPerPeak = 0.70710678118654752;
    inline constexpr double kUniformAmplitudePerRms = 1.7320508075688772;

    // THE TAPE'S OWN FLOOR, as an amplitude relative to operating level.
    //
    // `coating` is what is left of the magnetic layer, and it enters as a SQUARE
    // ROOT: half the particles is half the signal and only 3 dB less noise, so
    // wear costs 3 dB of signal-to-noise for every 6 dB of signal.
    [[nodiscard]] inline double particulateAmplitude(const HeadGeometry& head,
                                                     double coating,
                                                     const NoiseConstants& k) noexcept
    {
        const double left = std::clamp(coating, 1.0e-4, 1.0);
        const double volume = head.trackWidthMetres * head.thicknessMetres
                            * head.gapLengthMetres;
        if (!(volume > 0.0))
            return 0.0;
        const double rms = std::pow(10.0, -(k.referenceSnrDb + k.preampShareDb) / 20.0)
                         * kSineRmsPerPeak;

        // THE TWO TERMS PULL OPPOSITE WAYS, and getting the second backwards is
        // easy because both are square roots of a particle count.
        //
        //   the GEOMETRY term is relative, because the floor is quoted against
        //   an operating level that every machine is normalised to: a narrow
        //   track carries less signal for the same particle statistics, so its
        //   floor RISES as `1/sqrt(volume)`
        //
        //   the COATING term is absolute, because the wear gain has already
        //   taken the signal down before this is added: fewer particles is less
        //   noise as well as less signal, so the floor FALLS as `sqrt(coating)`
        //
        // Signal goes as the coating and noise as its square root, so
        // signal-to-noise goes as the square root too -- six decibels of signal
        // for three of ratio, all the way down. That asymmetry is the whole of
        // why wear destroys information rather than turning the volume down,
        // and it is arithmetic rather than a rule.
        //
        // This divided by the square root of the coating for a while, which
        // made bare tape hiss twelve decibels LOUDER than new tape and drowned
        // the electronics floor so thoroughly that mis-wiring it could not be
        // detected. Found by a teeth check that failed to bite.
        return rms * kUniformAmplitudePerRms * std::sqrt(left)
             * std::sqrt(NoiseConstants::referenceVolume() / volume);
    }

    // WHICH AMPLIFIERS A MONITOR PATH GOES THROUGH (`SOURCES §28`, Otari's own
    // service manual and block diagram, rather than three of us reasoning about
    // a circuit none of us had seen).
    //
    //   REPRO   reproduce head -> gate -> EQ amplifier -> monitor switch
    //   SYNC    record head -> A SEL-REP AMPLIFIER OF ITS OWN, three stages
    //           with a separate level trim -> the same gate -> the same EQ
    //           amplifier -> monitor switch
    //   SOURCE  line and mic in -> line pre-amplifier -> record EQ, with the
    //           monitor switch tapping that chain. It never touches the
    //           reproduce side
    //
    // AND THEN ALL THREE SHARE THE OUTPUT. The monitor switch sits BEFORE the
    // pre-output amplifier and the balanced line stage, so every selection
    // carries whatever those contribute -- which makes it a floor common to all
    // three rather than something that separates them, and it is why SOURCE
    // monitoring is quiet rather than silent.
    //
    // SEL-SYNC IS NOISIER THAN REPRO, AND NOW FOR A REASON. It is the same
    // chain with three more stages in front of it, so the folklore turns out to
    // be topology. The AMOUNT is not published; the direction is.
    enum class MonitorPath { Source, Sync, Reproduce };

    // How the one published figure divides between stages. **DECLARED** -- the
    // MTR-90's specification quotes the whole record-to-reproduce chain and the
    // MX-5050's manual gives the topology without levels, so no document
    // divides it and none is expected to. These are assumptions about a
    // DOCUMENTED structure rather than about an imagined one.
    //
    // The head amplifier dominates, because a reproduce head is a millivolt
    // source and the stage behind it is the high-gain one. The output stage is
    // well below it, and the line pre-amplifier that feeds SOURCE is lower
    // still because it works at line level throughout.
    inline constexpr double kOutputStageBelowDb = 12.0;
    inline constexpr double kSourcePathBelowDb = 18.0;
    // The sel-rep amplifier's three stages, on top of everything the reproduce
    // path already carries.
    inline constexpr double kSelRepExtraDb = 3.0;

    // THE ELECTRONICS' FLOOR, which scales with nothing. Not with the tape, not
    // with the coating, not with the wear gain -- which is the one thing that
    // must not be got wrong, because a floor attenuated along with the signal
    // keeps the signal-to-noise ratio constant and turns disintegration back
    // into a fade.
    //
    // AND IT DOES NOT AGE. `SOURCES §21` looked for what ages in a circuit and
    // found aluminium electrolytics and essentially nothing else; what that
    // produces is rising 2x-line hum through the supply rails, which is the
    // `Hum` axis's job, not broadband hiss.
    [[nodiscard]] inline double preampAmplitude(
        const NoiseConstants& k, MonitorPath path = MonitorPath::Reproduce) noexcept
    {
        double offset = 0.0;
        switch (path)
        {
            case MonitorPath::Reproduce: offset = 0.0; break;
            case MonitorPath::Sync:      offset = -kSelRepExtraDb; break;
            case MonitorPath::Source:    offset = kSourcePathBelowDb; break;
        }
        return std::pow(10.0, -(k.referenceSnrDb + k.preampShareDb + offset
                                - k.electronicsPenaltyDb) / 20.0)
             * kSineRmsPerPeak * kUniformAmplitudePerRms;
    }

    // THE STAGE EVERY SELECTION PASSES, because the monitor switch is in front
    // of it: the pre-output amplifier and the balanced line driver. Present on
    // SOURCE, SYNC and REPRO alike, which is what stops any of them being
    // silent.
    [[nodiscard]] inline double outputStageAmplitude(const NoiseConstants& k) noexcept
    {
        return std::pow(10.0,
                        -(k.referenceSnrDb + k.preampShareDb + kOutputStageBelowDb
                          - k.electronicsPenaltyDb) / 20.0)
             * kSineRmsPerPeak * kUniformAmplitudePerRms;
    }

    // A DETERMINISTIC FIELD, because a tape's granularity is frozen into it.
    //
    // Value noise over the medium index, hashed from the reel's seed: the same
    // inch reads the same noise every time, in either direction, and at double
    // speed it comes back an octave up -- which is what tape hiss does and what
    // a free-running generator could not give at any price (`PRINCIPLES §5`).
    class ParticulateField
    {
    public:
        void prepare(std::uint64_t reelSeed) noexcept { seed_ = reelSeed; }

        [[nodiscard]] double at(double mediumIndex) const noexcept
        {
            const double floored = std::floor(mediumIndex);
            const auto i = static_cast<std::int64_t>(floored);
            const double f = mediumIndex - floored;
            const double a = value(i), b = value(i + 1);
            // Linear rather than smooth: this is a broadband source and the
            // corner is the point, where the wear map wanted the opposite.
            return a + (b - a) * f;
        }

    private:
        [[nodiscard]] double value(std::int64_t index) const noexcept
        {
            std::uint64_t x = seed_ ^ (static_cast<std::uint64_t>(index) * 0x9e3779b97f4a7c15ull);
            x += 0x9e3779b97f4a7c15ull;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
            x ^= x >> 31;
            // [-1, 1), with the top 53 bits so the value does not depend on
            // whichever bits the multiplier leaves least mixed.
            return static_cast<double>(x >> 11) / 4503599627370496.0 - 1.0;
        }

        std::uint64_t seed_ = 0;
    };

    // THE MACHINE'S OWN, which is live and free-running. Seeded at prepare from
    // the machine's seed, so a render started from a fresh prepare repeats
    // exactly; nothing about it is a function of where the tape is, because a
    // preamp does not know.
    class PreampNoise
    {
    public:
        void prepare(std::uint64_t machineSeed) noexcept
        {
            state_ = machineSeed | 1ull;
        }

        [[nodiscard]] double next() noexcept
        {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 7;
            state_ ^= state_ << 17;
            return static_cast<double>(state_ >> 11) / 4503599627370496.0 - 1.0;
        }

    private:
        std::uint64_t state_ = 1;
    };
}
