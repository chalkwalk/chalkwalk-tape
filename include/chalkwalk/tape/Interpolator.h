#pragma once

// The record chain's oversampler, 48 -> 768 kHz (DESIGN.md section 4.2).
//
// RECORD SIDE (PRINCIPLES section 2). This feeds the hysteresis solver, and
// what it images the solver will distort and the decimator will fold back onto
// the medium permanently.
//
// NOT THE BENCH'S `interpolate`, WHICH IS AN INSTRUMENT, in exactly the way
// `decimate` was: its cutoff stays at 0.45 of the BASE rate whatever the
// factor, so its tap count grows as 64x the factor -- 1025 taps at 16x. That is
// the right design for a ruler and the wrong one for a product.
//
// FOUR 2:1 STAGES, AND THE COST IS ALL IN THE LAST ONES. A stage's filter gets
// SHORTER as the ladder climbs, because the transition band it must resolve
// stays fixed in hertz while its rate doubles -- but it also runs twice as
// often. The second effect wins, so the cheap-looking top of the ladder is
// where the work is, and the sharp filter at the bottom is nearly free. Any
// attempt to economise should start at the top.
//
// THE PASSBAND EDGE MATCHES THE INSTRUMENT'S, deliberately. `interpolate` cuts
// at 0.45 of the base rate -- 21.6 kHz at 48 kHz -- so the two are being asked
// for the same thing and the timing comparison means something. It is also
// where an oversampler has to cut: the first image of a 48 kHz signal starts at
// 48 minus the passband edge, so a passband taken to Nyquist would need a brick
// wall.
//
// JUCE-free by design. Promotion target: chalkwalk-dsp.

#include <chalkwalk/tape/FirDesign.h>

#include <algorithm>
#include <array>

#include <cstddef>
#include <vector>

namespace chalkwalk::tape
{
    // One 2:1 interpolating stage, polyphase.
    //
    // Zero-stuffing then filtering does the same arithmetic as this and half of
    // it against zeros. Splitting the kernel into its even and odd taps gives
    // two shorter filters, each producing one of the two outputs, with no
    // multiply spent on a sample known to be zero.
    //
    // ---- SYMMETRIC, AND MINIMUM PHASE WAS MEASURED AND NOT TAKEN ----
    //
    // The loss filter, the contour's split and the record equaliser all went
    // minimum phase because it was better on BOTH axes -- less delay AND less
    // error. This one is not, and the numbers are here so it is not re-derived.
    //
    // Stage 0 is the whole question: 123 taps at twice the engine rate is
    // 0.635 ms, which is 77 % of the ladder's 0.828 ms and the last term of any
    // size in the machine's latency. Designed against the same -89 dB:
    //
    //                    taps   live   stopband     delay
    //   symmetric (this)  123     63     -89.2 dB   0.635 ms
    //   min phase         123    123     -67.7      0.080
    //   min phase         255    255     -79.7      0.080
    //
    // THE STOPBAND IS NOT THE OBSTACLE, and an earlier note here said it was.
    // A hand-written raised-cosine target reached only -67.7 dB and looked
    // fatal; feeding the cepstrum the SYMMETRIC DESIGN'S OWN DTFT instead
    // carries the response across exactly, because minimum phase preserves
    // magnitude. Measured at the same 123 taps:
    //
    //                taps   live   stopband   ripple    delay
    //   symmetric     123     63   -89.08 dB   0.000   61.0 smp = 0.635 ms
    //   minimum       123    123   -89.08 dB   0.000    7.4 smp = 0.077 ms
    //
    // Same length, same stopband, same passband, an EIGHTH of the delay. It was
    // built, and it is not here, for two reasons that are better than the one
    // it replaced.
    //
    // 1. IT OVERSHOOTS THE MEDIUM. Minimum phase concentrates a filter's energy
    //    instead of spreading it symmetrically, which raises the crest factor
    //    of the oversampled signal -- and this ladder feeds a NONLINEARITY, so
    //    a higher intersample peak drives the tape harder. Slipback's medium
    //    peak went from under full scale to 1.00523, which `i16` stock would
    //    clamp. "A loop erases what it laid down last time round" catches it.
    //    That is `PRINCIPLES §2` earning its keep: this is the RECORD side, the
    //    change is written into `Medium`, and no amount of cleaning the heads
    //    takes it back.
    //
    // 2. IT INVERTS THE LADDER. Half a symmetric halfband's taps are
    //    structurally zero and `liveTaps` skips every one; a minimum-phase
    //    kernel has none. Stage 0 goes from 63 multiplies to 123 and becomes
    //    the most expensive stage in a ladder built so the sharp filter sits
    //    where it runs least often -- which "the upsampler's work is at the TOP
    //    of the ladder" asserts as a design property.
    //
    // Half a millisecond is not worth either. Recorded so the design is not
    // re-attempted from the same starting point a third time.
    class InterpolatorStage
    {
    public:
        void design(double passbandNormalised, double stopbandNormalised,
                    double stopbandDb)
        {
            // Designed at the OUTPUT rate, so the caller's normalised
            // frequencies are halved on the way in.
            const auto h = fir::designLowpass(0.5 * passbandNormalised,
                                              0.5 * stopbandNormalised,
                                              stopbandDb, 2.0);
            length_ = h.size();

            phaseA_.clear();
            phaseB_.clear();
            for (const auto& t : fir::liveTaps(h))
            {
                // Even taps see one polyphase branch, odd taps the other.
                const std::size_t k = t.index / 2;
                if ((t.index % 2) == 0) phaseA_.push_back({k, t.value});
                else                    phaseB_.push_back({k, t.value});
            }

            history_.assign((length_ / 2 + 2) * 2, 0.0);
            span_ = length_ / 2 + 1;
            cursor_ = 0;
        }

        void reset() noexcept
        {
            for (auto& v : history_) v = 0.0;
            cursor_ = 0;
        }

        // One input, two outputs.
        void push(double x, double& first, double& second) noexcept
        {
            history_[cursor_] = x;
            history_[cursor_ + span_] = x;
            cursor_ = (cursor_ + 1 == span_) ? 0 : cursor_ + 1;

            const double* p = history_.data() + cursor_;
            double a = 0.0, b = 0.0;
            for (const auto& t : phaseA_)
                a += t.value * p[span_ - 1 - t.index];
            for (const auto& t : phaseB_)
                b += t.value * p[span_ - 1 - t.index];
            first = a;
            second = b;
        }

        [[nodiscard]] std::size_t length() const noexcept { return length_; }
        [[nodiscard]] std::size_t multiplies() const noexcept
        {
            return phaseA_.size() + phaseB_.size();
        }

    private:
        std::vector<fir::Tap> phaseA_, phaseB_;
        std::vector<double> history_;
        std::size_t length_ = 0;
        std::size_t span_ = 1;
        std::size_t cursor_ = 0;
    };

    // 16x as four 2:1 stages.
    class Interpolator
    {
    public:
        static constexpr int kStages = 4;
        static constexpr int kFactor = 1 << kStages;

        // `passbandHz` and `inputRate` in the same units.
        void design(double inputRate, double passbandHz, double stopbandDb = 89.0)
        {
            double rate = inputRate;
            for (int s = 0; s < kStages; ++s)
            {
                // Each stage must suppress the images of ITS input, which begin
                // at (its input rate - passband). The higher the stage, the
                // wider that gap and the shorter the filter -- which is why the
                // sharp one is at the bottom and the frequent one at the top.
                stages_[static_cast<std::size_t>(s)]
                    .design(passbandHz / rate, (rate - passbandHz) / rate, stopbandDb);
                rate *= 2.0;
            }
        }

        void reset() noexcept
        {
            for (auto& s : stages_) s.reset();
        }

        // One input sample, kFactor outputs written to `out`.
        //
        // TWO BUFFERS, AND ASCENDING, BECAUSE A STAGE HAS MEMORY. Each stage is
        // called several times per input -- stage s runs 2^s times -- and those
        // calls are consecutive samples at that stage's rate, so they must
        // arrive in time order. Expanding in place needs a DESCENDING loop to
        // avoid clobbering, which feeds every stage its samples backwards; that
        // was the first version, and it imaged so badly the first image came
        // back 2 dB ABOVE the fundamental. A filter fed time-reversed input is
        // still a filter, so nothing crashes and nothing is obviously wrong
        // until the spectrum is looked at.
        void push(double x, double* out) noexcept
        {
            double a[kFactor] = {};
            double b[kFactor] = {};
            double* in = a;
            double* work = b;

            in[0] = x;
            int count = 1;
            for (int s = 0; s < kStages; ++s)
            {
                for (int i = 0; i < count; ++i)
                    stages_[static_cast<std::size_t>(s)]
                        .push(in[i], work[2 * i], work[2 * i + 1]);
                count *= 2;
                std::swap(in, work);
            }
            for (int i = 0; i < kFactor; ++i)
                out[i] = in[i];
        }

        [[nodiscard]] const InterpolatorStage& stage(int s) const noexcept
        {
            return stages_[static_cast<std::size_t>(s)];
        }

        // Multiplies per INPUT sample, which is what compares against the rest
        // of the record chain. Stage s runs 2^s times per input.
        [[nodiscard]] double multipliesPerInput() const noexcept
        {
            double total = 0.0;
            for (int s = 0; s < kStages; ++s)
                total += static_cast<double>(stages_[static_cast<std::size_t>(s)]
                                                 .multiplies())
                       * static_cast<double>(1 << s);
            return total;
        }

    private:
        std::array<InterpolatorStage, kStages> stages_;
    };
}
