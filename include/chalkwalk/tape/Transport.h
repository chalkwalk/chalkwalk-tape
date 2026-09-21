#pragma once

// The transport: one tau(t) per machine (DESIGN.md section 4.4, SOURCES
// section 19).
//
// PLAYBACK SIDE (PRINCIPLES section 2). This produces a TIMING OFFSET and
// stores nothing. It is applied where the heads are placed, so the same tape
// played twice gives two different timings and the medium is untouched by both.
//
// ONE SIGNAL PER MACHINE, SHARED BY EVERY HEAD (PRINCIPLES section 3). There is
// one capstan and one motor, so two tracks recorded in one pass cannot drift
// apart -- which is not a saving but a constraint a real machine could not
// violate. Per-track flutter would destroy every stereo pair and every bounce
// (fence #10).
//
// NOT TWO OSCILLATORS LABELLED "WOW" AND "FLUTTER". Every rotating element in a
// tape path turns at a rate its diameter and the tape speed fix, and
// eccentricity modulates the speed once per revolution. Capstan and pinch land
// in the flutter band, reels land in the wow band, and they are the same
// phenomenon at two radii. What that buys: every rate scales with tape speed for
// free, so selecting 7.5 ips instead of 15 halves the whole spectrum with no
// parameter involved.
//
// AND WHERE A DISTURBANCE ENTERS DECIDES HOW MUCH SURVIVES (SOURCES section 19).
// The flywheel is a second-order low-pass, so motor torque ripple at twice the
// line frequency is filtered while capstan runout, pinch flat spots and scrape
// flutter are downstream of it and are not attenuated at all. That is the
// difference between a transport model and a sum of sines.
//
// TAU IS THE INTEGRAL OF THE SPEED ERROR, not the speed error itself. A relative
// speed error `e` at rate `f` gives a timing offset `e / (2 pi f)` -- so the same
// 0.1 % error is a large offset at 0.7 Hz and a tiny one at 3 kHz. That single
// relationship is why wow is heard as pitch drift and flutter as roughness, and
// it falls out rather than being modelled.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chalkwalk::tape
{
    // What the tape path is made of. Diameters and radii in metres.
    struct TransportGeometry
    {
        // Rotating elements the tape wraps. A roller of diameter `d` turns once
        // per `pi * d` of tape, so its rate is `v / (pi * d)`.
        double capstanDiameterMetres = 0.0;
        double pinchDiameterMetres = 0.0;

        // The reels. A pack of radius `r` turns once per `2 pi r`, and `r` moves
        // across a take because tape leaves one pack and arrives at the other.
        double hubRadiusMetres = 0.0;
        double fullPackRadiusMetres = 0.0;
        double tapeThicknessMetres = 0.0;

        // THE FLYWHEEL, which is a filter and not a component (SOURCES section
        // 19). `f = (1/2pi) sqrt(k/J)` for belt stiffness `k` and flywheel
        // inertia `J`. A STRETCHED BELT LOWERS BOTH -- the corner drops into the
        // wow band and the damping goes with it -- so age moves these two
        // together and in the same direction, which is a consequence rather than
        // a choice.
        double flywheelCornerHz = 0.0;
        double flywheelQ = 0.7071;

        // Mains. Torque follows the square of an alternating current, so a
        // single-phase motor pulsates at TWICE this (SOURCES section 19).
        double lineFrequencyHz = 50.0;

        // Scrape flutter: a LONGITUDINAL standing wave in the tape itself, so
        // `f = c / 2L` over the unsupported span between ROTATING elements.
        // Fixed guides are not barriers; rotating ones are, which is why a
        // machine with roller guides has several short spans and a cheap one has
        // one long one.
        double unsupportedSpanMetres = 0.0;
        double longitudinalWaveSpeedMps = 1700.0;

        [[nodiscard]] constexpr bool modelled() const noexcept
        {
            return capstanDiameterMetres > 0.0;
        }

        // Rate of a roller the tape wraps, in Hz. One turn per `pi * d`.
        [[nodiscard]] constexpr double rollerRateHz(double diameterMetres,
                                                    double speedMps) const noexcept
        {
            const double circumference = 3.14159265358979323846 * diameterMetres;
            return (circumference > 0.0) ? speedMps / circumference : 0.0;
        }

        // Pack radius after `metresWound` metres have gone onto a pack that
        // started at the hub. Area of the annulus is the tape's length times its
        // thickness, so `pi (r^2 - r_hub^2) = t * x`.
        [[nodiscard]] double packRadius(double metresWound) const noexcept
        {
            const double area = tapeThicknessMetres * std::max(0.0, metresWound)
                              / 3.14159265358979323846;
            const double r = std::sqrt(hubRadiusMetres * hubRadiusMetres + area);
            return std::min(r, fullPackRadiusMetres);
        }

        // Rate of a pack of this radius. One turn per `2 pi r`.
        [[nodiscard]] double packRateHz(double radiusMetres, double speedMps) const noexcept
        {
            const double circumference = 2.0 * 3.14159265358979323846 * radiusMetres;
            return (circumference > 0.0) ? speedMps / circumference : 0.0;
        }

        [[nodiscard]] double scrapeResonanceHz() const noexcept
        {
            return (unsupportedSpanMetres > 0.0)
                 ? longitudinalWaveSpeedMps / (2.0 * unsupportedSpanMetres) : 0.0;
        }

        // How much tape the machine holds, from the full pack down to the hub.
        [[nodiscard]] double capacityMetres() const noexcept
        {
            if (tapeThicknessMetres <= 0.0)
                return 0.0;
            const double full = fullPackRadiusMetres * fullPackRadiusMetres;
            const double hub = hubRadiusMetres * hubRadiusMetres;
            return 3.14159265358979323846 * (full - hub) / tapeThicknessMetres;
        }
    };

    // A second-order low-pass in direct form II transposed, from a corner and a
    // Q. This is the flywheel, so the Q matters as much as the corner: a
    // disturbance NEAR the resonance is amplified rather than attenuated, which
    // is the behaviour that makes an ageing belt drive worse in two ways at once.
    class Biquad
    {
    public:
        void designBandPass(double centreHz, double q, double sampleRateHz) noexcept
        {
            if (centreHz <= 0.0 || sampleRateHz <= 0.0 || q <= 0.0)
            {
                b0_ = b1_ = b2_ = a1_ = a2_ = 0.0;
                return;
            }
            constexpr double kPi = 3.14159265358979323846;
            const double w = 2.0 * kPi * std::min(centreHz, 0.49 * sampleRateHz)
                           / sampleRateHz;
            const double alpha = std::sin(w) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            // Constant PEAK gain, so `q` shapes the band without also changing
            // how loud the excitation is -- otherwise a seed that sharpened the
            // resonance would quietly turn it up.
            b0_ = alpha / a0;
            b1_ = 0.0;
            b2_ = -alpha / a0;
            a1_ = -2.0 * std::cos(w) / a0;
            a2_ = (1.0 - alpha) / a0;
        }

        void designLowPass(double cornerHz, double q, double sampleRateHz) noexcept
        {
            if (cornerHz <= 0.0 || sampleRateHz <= 0.0 || q <= 0.0)
            {
                b0_ = 1.0; b1_ = b2_ = a1_ = a2_ = 0.0;
                return;
            }
            constexpr double kPi = 3.14159265358979323846;
            const double w = 2.0 * kPi * std::min(cornerHz, 0.49 * sampleRateHz)
                           / sampleRateHz;
            const double alpha = std::sin(w) / (2.0 * q);
            const double cosw = std::cos(w);
            const double a0 = 1.0 + alpha;
            b0_ = (1.0 - cosw) / 2.0 / a0;
            b1_ = (1.0 - cosw) / a0;
            b2_ = b0_;
            a1_ = -2.0 * cosw / a0;
            a2_ = (1.0 - alpha) / a0;
        }

        void reset() noexcept { z1_ = z2_ = 0.0; }

        [[nodiscard]] double process(double x) noexcept
        {
            const double y = b0_ * x + z1_;
            z1_ = b1_ * x - a1_ * y + z2_;
            z2_ = b2_ * x - a2_ * y;
            return y;
        }

        // Magnitude at a frequency, for tests that want the design rather than a
        // measured sweep.
        [[nodiscard]] double magnitudeAt(double hz, double sampleRateHz) const noexcept
        {
            constexpr double kPi = 3.14159265358979323846;
            const double w = 2.0 * kPi * hz / sampleRateHz;
            const double cw = std::cos(w), sw = std::sin(w);
            const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
            const double nr = b0_ + b1_ * cw + b2_ * c2;
            const double ni = -(b1_ * sw + b2_ * s2);
            const double dr = 1.0 + a1_ * cw + a2_ * c2;
            const double di = -(a1_ * sw + a2_ * s2);
            const double num = std::sqrt(nr * nr + ni * ni);
            const double den = std::sqrt(dr * dr + di * di);
            return (den > 0.0) ? num / den : 0.0;
        }

    private:
        double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
        double z1_ = 0.0, z2_ = 0.0;
    };
}

namespace chalkwalk::tape
{
    // The machine's timing signal.
    //
    // `next()` returns tau in SECONDS: how far the tape is from where a perfect
    // transport would have put it. Positive means the tape is ahead.
    class Transport
    {
    public:
        // RELATIVE SPEED ERRORS, dimensionless -- 0.001 is a tenth of a per
        // cent. These are what a seed and an Age set (DESIGN.md section 7.2);
        // none of them is a control, and `fence #4` is why: a machine exposing
        // "wow depth" is a simulator operated through its parameters.
        struct Depths
        {
            double capstanRunout = 0.0;
            double pinchFlatSpot = 0.0;
            double reelEccentricity = 0.0;
            double motorRipple = 0.0;
            double scrapeFlutter = 0.0;
        };

        void prepare(const TransportGeometry& geometry, double sampleRateHz,
                     std::uint64_t seed) noexcept
        {
            geometry_ = geometry;
            sampleRateHz_ = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;

            // Phases are seeded, so two machines are not in step at the start of
            // a take -- and the SAME machine always is, which PRINCIPLES
            // section 5 requires of everything derived from a seed.
            state_ = seed | 1u;
            capstanPhase_ = fraction();
            pinchPhase_ = fraction();
            supplyPhase_ = fraction();
            takeupPhase_ = fraction();
            ripplePhase_ = fraction();

            flywheel_.designLowPass(geometry.flywheelCornerHz, geometry.flywheelQ,
                                    sampleRateHz_);
            flywheel_.reset();
            scrape_.designBandPass(geometry.scrapeResonanceHz(), kScrapeQ,
                                   sampleRateHz_);
            scrape_.reset();
            offset_ = 0.0;
        }

        void setSpeed(double metresPerSecond) noexcept { speedMps_ = metresPerSecond; }
        void setDepths(const Depths& d) noexcept { depths_ = d; }

        // What it is actually running, which is not always what was asked for:
        // the scrape-flutter term is scaled by the tape's friction before it
        // gets here (`TapeDeck`, `SOURCES` sections 19 and 23).
        [[nodiscard]] const Depths& depths() const noexcept { return depths_; }

        // WHERE THE TAPE IS, in metres played from the start of the reel. The
        // pack radii come from this and nothing else, because position IS the
        // tape (DESIGN.md section 3.1) -- so the supply's wow rises across a take
        // while the take-up's falls, with nothing dialling it.
        //
        // Called once a block. The radii move by microns in a block and the
        // rates they set are under 3 Hz, so per-sample would compute the same
        // number many times.
        void setTapePosition(double metresPlayed) noexcept
        {
            const double capacity = geometry_.capacityMetres();
            const double played = std::clamp(metresPlayed, 0.0, capacity);
            supplyRateHz_ = geometry_.packRateHz(
                geometry_.packRadius(capacity - played), speedMps_);
            takeupRateHz_ = geometry_.packRateHz(
                geometry_.packRadius(played), speedMps_);
        }

        // One sample of tau, in seconds.
        [[nodiscard]] double next() noexcept
        {
            if (! geometry_.modelled() || speedMps_ <= 0.0)
                return 0.0;

            const double dt = 1.0 / sampleRateHz_;
            constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

            // --- downstream of the flywheel: nothing filters these ---
            //
            // TAU IS THE INTEGRAL OF THE SPEED ERROR, and for a sinusoid that
            // integral is exact: `A cos(theta)` integrates to `A/(2 pi f)
            // sin(theta)`. Closed form rather than a running sum, so there is no
            // drift to leak away and no state to get out of step between takes.
            double tau = 0.0;
            tau += sinusoid(capstanPhase_,
                            geometry_.rollerRateHz(geometry_.capstanDiameterMetres,
                                                   speedMps_),
                            depths_.capstanRunout, dt);
            tau += sinusoid(supplyPhase_, supplyRateHz_, depths_.reelEccentricity, dt);
            tau += sinusoid(takeupPhase_, takeupRateHz_, depths_.reelEccentricity, dt);

            // A FLAT SPOT IS NOT A SINE. It is one bump per revolution, so it is
            // modelled as one: a raised cosine over a fraction of the turn, zero
            // elsewhere. A sine here would spread the same energy over the whole
            // revolution and sound like wow rather than like a tick.
            //
            // AND IT IS A DIP THAT EASES BACK, WHICH IS NOT COSMETIC. The bump
            // on its own is one-sided, so it carries a DC term -- and a DC speed
            // error integrates into a timing offset that grows without bound.
            // Measured before this was subtracted, the flat spot was **83% DC**:
            // a constant lead of 0.667 ms with a 0.8 ms wobble on it, while
            // every other component read under 0.25%. The leaky integrator was
            // HIDING it, turning an unbounded ramp into a bounded constant that
            // looked like a plausible offset.
            //
            // The physical argument is that the capstan sets the average tape
            // speed and nothing downstream of it can change the mean. Slip at a
            // flat spot does lose real distance -- but that is a speed
            // CALIBRATION effect, and the machine's nominal speed is already the
            // achieved speed with slip in it (DESIGN.md section 4.4). Leaving
            // the DC here would count it twice.
            //
            // So the roller dips as the flat passes and runs a hair fast for the
            // rest of the turn, and the two cancel exactly. That shape is forced
            // by the zero-mean requirement rather than chosen for it.
            const double pinchRate = geometry_.rollerRateHz(
                geometry_.pinchDiameterMetres, speedMps_);
            advance(pinchPhase_, pinchRate, dt);
            double pinch = 0.0;
            if (depths_.pinchFlatSpot > 0.0)
            {
                // What the bump averages over a whole revolution: the raised
                // cosine averages to a half of its height, over `kFlatSpotFraction`
                // of the turn.
                const double meanOfBump = depths_.pinchFlatSpot * 0.5 * kFlatSpotFraction;
                double bump = 0.0;
                if (pinchPhase_ < kFlatSpotFraction)
                {
                    const double u = pinchPhase_ / kFlatSpotFraction;
                    bump = depths_.pinchFlatSpot * 0.5 * (1.0 - std::cos(kTwoPi * u));
                }
                pinch = meanOfBump - bump;
            }

            // Scrape flutter: friction is broadband, what it excites is narrow
            // (SOURCES section 19).
            const double excitation = depths_.scrapeFlutter * (2.0 * fraction() - 1.0);
            const double scrape = scrape_.process(excitation);

            // --- upstream of the flywheel: this one is filtered ---
            //
            // Torque follows the square of an alternating current, so the ripple
            // is at TWICE the line frequency and does not scale with tape speed
            // -- the only component here that does not, because it comes from
            // the mains rather than from the tape path.
            advance(ripplePhase_, 2.0 * geometry_.lineFrequencyHz, dt);
            const double ripple = depths_.motorRipple
                                * std::cos(kTwoPi * ripplePhase_);
            const double filtered = flywheel_.process(ripple);

            // The non-sinusoidal parts are integrated, and leaked so that a
            // one-sided disturbance cannot walk the tape away over a long take.
            // The time constant is far below every rate here, so it removes DC
            // and nothing else.
            offset_ += (pinch + scrape + filtered) * dt;
            offset_ -= offset_ * dt / kLeakSeconds;

            return tau + offset_;
        }

        void reset() noexcept
        {
            flywheel_.reset();
            scrape_.reset();
            offset_ = 0.0;
        }

        [[nodiscard]] const TransportGeometry& geometry() const noexcept { return geometry_; }
        [[nodiscard]] double supplyRateHz() const noexcept { return supplyRateHz_; }
        [[nodiscard]] double takeupRateHz() const noexcept { return takeupRateHz_; }
        [[nodiscard]] const Biquad& flywheel() const noexcept { return flywheel_; }

        // **UNSOURCED RATE**, and the reclassification from UNSOURCED needs one
        // correction (`SOURCES §40` then `§41`). The first pass concluded "no
        // source gives a sharpness, so the number appears not to exist". No
        // source gives a NUMBER, which stands -- but Manquen gives the constant
        // a SHAPE, and a shapeless free number is not the same thing as a free
        // scale on a sourced coupling.
        //
        // WHAT IS SOURCED: the resonance FREQUENCY (3 to 10 kHz, set by the
        // tuning of the free tape span, the tape bowed like a violin string),
        // the MECHANISM (stick-slip), and -- this is the new part -- that
        // **the longer the span, the lower the frequency AND THE HIGHER THE Q**,
        // because the damping is losses within the tape itself and raising the
        // frequency raises the attenuation.
        //
        // SO THIS CONSTANT IS COUPLED TO `unsupportedSpanMetres` AND IS NOT
        // WRITTEN THAT WAY. `scrapeResonanceHz()` already derives the frequency
        // from the span; the Q sits here at a fixed value regardless, which the
        // source says it cannot be. The honest form is a function of the span
        // with one free scale. Not built: the LAW is unsourced even though the
        // direction is not, so it would be DECLARED, and it changes the sound.
        //
        // ON THE VALUE: the source says only that "the peak is typically quite
        // broad due to the somewhat random nature of the stick/slip
        // phenomenon". Capstan's 85 mm span puts the resonance at 10 kHz where
        // 12 is an 833 Hz bandwidth -- defensible as broad, at the narrow end
        // of what the phrase suggests.
        static constexpr double kScrapeQ = 12.0;

        // What fraction of a revolution a flat spot occupies.
        // An UNSOURCED RATE: physically it is the chord the flat presents
        // against the roller's circumference, and nothing publishes how big a
        // flat gets.
        static constexpr double kFlatSpotFraction = 0.08;

        static constexpr double kLeakSeconds = 10.0;

    private:
        void advance(double& phase, double rateHz, double dt) noexcept
        {
            phase += rateHz * dt;
            phase -= std::floor(phase);
        }

        // Advance a phase and return its contribution to tau.
        [[nodiscard]] double sinusoid(double& phase, double rateHz, double depth,
                                      double dt) noexcept
        {
            advance(phase, rateHz, dt);
            if (rateHz <= 0.0 || depth <= 0.0)
                return 0.0;
            constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
            return depth / (kTwoPi * rateHz) * std::sin(kTwoPi * phase);
        }

        // xorshift64. Deterministic, identical on every platform, and it has to
        // be: PRINCIPLES section 5 promises a project opens the same everywhere,
        // and a transport seeded from the machine is part of that promise.
        [[nodiscard]] double fraction() noexcept
        {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 7;
            state_ ^= state_ << 17;
            return static_cast<double>(state_ >> 11) * (1.0 / 9007199254740992.0);
        }

        TransportGeometry geometry_{};
        Depths depths_{};
        Biquad flywheel_, scrape_;
        double sampleRateHz_ = 48000.0;
        double speedMps_ = 0.0;
        double supplyRateHz_ = 0.0, takeupRateHz_ = 0.0;
        double capstanPhase_ = 0.0, pinchPhase_ = 0.0;
        double supplyPhase_ = 0.0, takeupPhase_ = 0.0, ripplePhase_ = 0.0;
        double offset_ = 0.0;
        std::uint64_t state_ = 1;
    };
}
