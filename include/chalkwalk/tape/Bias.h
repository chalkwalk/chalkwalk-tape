#pragma once

// Bias (SOURCES section 7, DESIGN.md section 4.1).
//
// RECORD SIDE (PRINCIPLES section 2): what bias does is written into the tape.
// Recording with the bias wrong is a take you have to redo, not a setting you
// can change afterwards.
//
// A high-frequency current added to the signal before the record head, so that
// recording happens on the linear part of the magnetisation curve rather than
// across the deadzone around zero. Camras's patent -- the primary source, and
// public domain, in docs/references/ -- states the mechanism directly: with HF
// bias superimposed the residual magnetisation curve becomes "substantially a
// true straight line that passes through the zero position".
//
// ONE OSCILLATOR PER MACHINE, shared by every record head (PRINCIPLES section
// 3). Not one per track: a real deck has a single bias oscillator feeding all
// its record amplifiers, and per-track oscillators would put an audible beat
// between tracks that no machine has.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <chalkwalk/tape/Hysteresis.h>

#include <cmath>

namespace chalkwalk::tape
{
    // The bias amplitude a correctly aligned machine uses, in A/m.
    //
    // CALIBRATED AGAINST COERCIVITY, not against the signal. The source paper
    // describes it only as "about one order of magnitude larger than the
    // input", which is not a calibration -- it depends on how hard you happen to
    // be recording. Camras specifies it as a multiple of the coercive force,
    // quoting ranges around two to two and a half times, and our hysteresis
    // parameter `k` is approximately Hc for the stock. So this is a real number
    // in terms of a constant we already have.
    //
    // Overbias and underbias are then both reachable and both mean something:
    // underbias leaves the deadzone in play and gives crossover distortion,
    // overbias saturates and dulls the top. Neither is a knob -- bias is a
    // machine alignment that drifts with age (DESIGN.md section 4.10).
    [[nodiscard]] inline double nominalBiasAmplitude(const TapeStock& stock) noexcept
    {
        return 2.25 * stock.coercivity;
    }

    class BiasOscillator
    {
    public:
        // Returns false if the rate cannot represent the frequency.
        //
        // WORTH REFUSING RATHER THAN ALIASING. A 55 kHz carrier at 48 kHz folds
        // to 7 kHz, and a 7 kHz tone written permanently onto the tape would be
        // blamed on the hysteresis model rather than on the oscillator. This is
        // also the floor that forces oversampling on the record path
        // (DESIGN.md section 4.2): bias sets the minimum rate before aliasing
        // from the non-linearity is even considered.
        bool prepare(double sampleRate, double frequencyHz, double amplitude) noexcept
        {
            if (sampleRate <= 0.0 || frequencyHz <= 0.0)
                return false;

            amplitude_ = amplitude;
            // Strictly below Nyquist, with a margin: a carrier sitting just
            // under it is representable in principle and useless in practice.
            if (frequencyHz >= 0.45 * sampleRate)
            {
                increment_ = 0.0;
                return false;
            }

            increment_ = 2.0 * M_PI * frequencyHz / sampleRate;
            stepCos_ = std::cos(increment_);
            stepSin_ = std::sin(increment_);
            reset();
            return true;
        }

        void reset() noexcept
        {
            cos_ = 1.0;
            sin_ = 0.0;
            sinceTrim_ = 0;
        }

        // ---- A ROTATION, NOT A COSINE ----
        //
        // THIS CALLED `std::cos` EVERY SAMPLE, and it is called at the
        // OVERSAMPLED rate: 768,000 transcendentals a second per armed track,
        // for a pure sinusoid at a frequency that never changes. Measured at
        // 7.53 ns a step against a whole write-path budget of 121 ns
        // (`RemanenceBench perf-detail`), which made the carrier the fourth
        // most expensive thing in the record chain and the only one doing no
        // work.
        //
        // Advancing a unit vector by a fixed angle is two multiplies and a
        // subtract. The rotation matrix is built once in `prepare`.
        //
        // THE MAGNITUDE DRIFTS, WHICH IS WHY THE OLD CODE WRAPPED THE PHASE.
        // Repeated rotation loses unit length to rounding, and a carrier whose
        // amplitude wanders is an alignment drifting for a reason that is not
        // physical. `kTrimInterval` samples in, the vector is renormalised by
        // one Newton step of the inverse square root -- exact to second order,
        // no divide, and 27 ns amortised over 4096 samples is 0.007 of one.
        [[nodiscard]] double next() noexcept
        {
            const double value = amplitude_ * cos_;

            const double nextCos = cos_ * stepCos_ - sin_ * stepSin_;
            const double nextSin = sin_ * stepCos_ + cos_ * stepSin_;
            cos_ = nextCos;
            sin_ = nextSin;

            if (++sinceTrim_ >= kTrimInterval)
            {
                sinceTrim_ = 0;
                const double correction = 1.5 - 0.5 * (cos_ * cos_ + sin_ * sin_);
                cos_ *= correction;
                sin_ *= correction;
            }
            return value;
        }

        // BIAS DRIFT IS AN ALIGNMENT GOING STALE, so the amplitude moves while
        // the machine runs (`DESIGN.md` §7.2). Changing it needs no redesign of
        // anything -- the carrier's FREQUENCY sets the ladder and is untouched;
        // only how hard it is driven changes.
        void setAmplitude(double amplitude) noexcept { amplitude_ = amplitude; }

        [[nodiscard]] double amplitude() const noexcept { return amplitude_; }

    private:
        static constexpr int kTrimInterval = 4096;

        double amplitude_ = 0.0;
        double increment_ = 0.0;
        double stepCos_ = 1.0;
        double stepSin_ = 0.0;
        double cos_ = 1.0;
        double sin_ = 0.0;
        int sinceTrim_ = 0;
    };
}
