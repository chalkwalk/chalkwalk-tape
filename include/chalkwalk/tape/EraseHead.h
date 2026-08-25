#pragma once

#include <chalkwalk/tape/Heads.h>

#include <cmath>

namespace chalkwalk::tape
{
    // The erase head (DESIGN §40.10).
    //
    // Replace = erase + write, as on tape. Additive overdub needs no erase at
    // all; what needs one is *replacing* content while the medium moves at an
    // arbitrary rate, because the write head is add-only by construction and
    // would otherwise pile the new take on top of the old.
    //
    // The law is per **medium** sample, not per engine sample. A head at rate
    // 0.5 lingers over each medium sample for two engine samples, and at rate 2
    // it skips every other one; an erase applied once per engine sample would
    // therefore erase twice as hard at half speed and leave gaps at double. So
    // the head sweeps: on each step it attenuates exactly the integer medium
    // samples it passed over, once each. Consequences fall out rather than being
    // special-cased:
    //
    //   * rate 1 → a one-sample window, which is today's integer-unity record.
    //   * rate 0 → a stalled head sweeps nothing and erases nothing. A tape
    //     sitting still under an erase head is not being wiped.
    //   * reverse → it sweeps backwards, erasing the same samples it would have
    //     erased going forwards.
    //
    // **It runs ahead of the write.** Position it `gap` medium samples downstream
    // of the write head (leadFor()) so it clears tape the write kernel has not
    // reached yet. A gap of kHalf is the minimum that cannot eat the write's own
    // deposit; the deck supplies it.
    //
    // Erasure is a fraction, not a flag: 1.0 wipes (replace), 0.3 is a tape loop
    // that forgets slowly. Partial erasure is why the layer stack's decay and
    // this head are the same idea seen from two sides.
    class EraseHead : public Head
    {
    public:
        // Fraction of the existing signal removed as each medium sample passes
        // under the head. 1 = full erase, 0 = a head that is not energised.
        void setErasure(float e) noexcept { erasure_ = std::clamp(e, 0.0f, 1.0f); }
        [[nodiscard]] float erasure() const noexcept { return erasure_; }

        // Where an erase head must sit to lead `w` by `gap` medium samples. A
        // reversing write head wants the erase head on its other side, so the
        // deck resyncs (setPosition) across a direction flip rather than letting
        // the head sweep across the gap and wipe the take it just laid down.
        [[nodiscard]] static double leadFor(const WriteHead& w, double gap) noexcept
        {
            const double dir = (w.rate() < 0.0) ? -1.0 : 1.0;
            return w.position() + dir * gap;
        }

        // The gap the erase head must lead by, for a write at `rate`.
        //
        // PREFER THIS TO kMinGap. The write kernel's half is what cannot be
        // eaten, and that follows the rate: 8 at unity, not the bank's worst
        // case. Extending the bank to rate 32 took the constant from 24 to 128
        // and put a 128-sample un-erased lead-in at the head of every replace
        // pass -- which the tests caught, and which is why this exists.
        [[nodiscard]] static double minGapFor(double rate) noexcept
        {
            return static_cast<double>(sharedKernels().halfFor(rate));
        }

        // The worst case across the whole bank, for a caller that cannot know
        // its rate in advance. Correct, and five times larger than it needs to
        // be at any rate a write actually runs at.
        static constexpr double kMinGap = Resampler::kHalf;

        // Attenuate every medium sample swept between the current position and
        // the next one, then advance. Call once per engine sample, BEFORE the
        // write head deposits.
        void sweep(Medium& m, int sub) noexcept
        {
            if (m.bound() && erasure_ > 0.0f)
            {
                const float keep = 1.0f - erasure_;
                const double from = pos_;
                const double to = pos_ + rate_;

                if (rate_ > 0.0)
                {
                    const auto first = static_cast<std::int64_t>(std::floor(from)) + 1;
                    const auto last = static_cast<std::int64_t>(std::floor(to));
                    for (std::int64_t k = first; k <= last; ++k)
                        scaleFrame(m, sub, k, keep);
                }
                else if (rate_ < 0.0)
                {
                    const auto first = static_cast<std::int64_t>(std::ceil(from)) - 1;
                    const auto last = static_cast<std::int64_t>(std::ceil(to));
                    for (std::int64_t k = first; k >= last; --k)
                        scaleFrame(m, sub, k, keep);
                }
            }
            step(m);
        }

    private:
        static void scaleFrame(Medium& m, int sub, std::int64_t idx, float keep) noexcept
        {
            for (int ch = 0; ch < m.channels(); ++ch)
                m.scale(sub, ch, idx, keep);
        }

        float erasure_ = 1.0f;
    };
}
