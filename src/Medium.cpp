#include <chalkwalk/tape/Medium.h>

namespace chalkwalk::tape
{
    namespace
    {
        [[nodiscard]] bool geometryOk(const Medium::Config& c) noexcept
        {
            return c.numSubTracks > 0 && c.channelsPerSubTrack > 0 && c.capacitySamples > 0;
        }

        // HOW MUCH TAPE A CONFIG DESCRIBES. A window shorter than its reel is
        // the whole point; a `reelSamples` SHORTER than the storage is a caller
        // mistake, and taking the larger of the two means the storage is always
        // addressable rather than partly stranded.
        [[nodiscard]] std::int64_t reelLengthOf(const Medium::Config& c) noexcept
        {
            const auto cap = static_cast<std::int64_t>(c.capacitySamples);
            return (c.topology == Topology::Linear && c.reelSamples > cap)
                 ? c.reelSamples
                 : cap;
        }
    }

    std::size_t Medium::storageSamples(const Config& c) noexcept
    {
        if (! geometryOk(c)) return 0;
        return static_cast<std::size_t>(c.numSubTracks)
             * static_cast<std::size_t>(c.channelsPerSubTrack)
             * static_cast<std::size_t>(c.capacitySamples);
    }

    void Medium::bind(const Config& c, Store store) noexcept
    {
        const std::size_t need = storageSamples(c);
        if (need == 0 || ! store.valid() || store.size() < need)
        {
            unbind();
            return;
        }

        const auto cap = static_cast<std::size_t>(c.capacitySamples);
        const int count = c.numSubTracks * c.channelsPerSubTrack;

        cfg_ = c;
        planes_.clear();
        planes_.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            planes_.push_back(store.slice(static_cast<std::size_t>(i) * cap, cap));
        used_.assign(static_cast<std::size_t>(c.numSubTracks), 0);
        windowOrigin_ = 0;
        reelLength_ = reelLengthOf(c);
    }

    void Medium::bindPlanes(const Config& c, const Store* planes, int count) noexcept
    {
        const int need = geometryOk(c) ? c.numSubTracks * c.channelsPerSubTrack : 0;
        if (need == 0 || planes == nullptr || count < need)
        {
            unbind();
            return;
        }

        const auto cap = static_cast<std::size_t>(c.capacitySamples);
        for (int i = 0; i < need; ++i)
        {
            if (! planes[i].valid() || planes[i].size() < cap)
            {
                unbind();
                return;
            }
        }

        cfg_ = c;
        planes_.assign(planes, planes + need);
        used_.assign(static_cast<std::size_t>(c.numSubTracks), 0);
        windowOrigin_ = 0;
        reelLength_ = reelLengthOf(c);
    }

    void Medium::unbind() noexcept
    {
        planes_.clear();
        used_.clear();
        windowOrigin_ = 0;
        // ZERO, so an unbound medium rejects every index in `resolve` without
        // a `bound()` test of its own in the innermost loop.
        reelLength_ = 0;
    }

    void Medium::ensureCommitted(int sub, std::int64_t upTo) noexcept
    {
        if (! bound() || sub < 0 || sub >= cfg_.numSubTracks) return;

        const std::int64_t target = std::min(upTo, reelLength());
        std::int64_t& mark = used_[static_cast<std::size_t>(sub)];
        if (target <= mark) return;

        // ---- ZERO ONLY WHAT IS RESIDENT ----
        //
        // The span being committed is in the reel's coordinates and the storage
        // is a window onto it, so the part to zero is the INTERSECTION. On an
        // unwindowed medium that is the whole span and this is what it always
        // did.
        //
        // WHAT ABOUT THE PART OUTSIDE THE WINDOW? It is not this class's to
        // zero, because it is not in memory: a streaming host owns the file and
        // is what will fill the window when it slides there. The mark still
        // moves over it, which is the promise that matters -- the tape has been
        // committed, and where the samples live is the host's business
        // (`setWindow`).
        const std::int64_t origin = windowOrigin_;
        const std::int64_t end = origin + static_cast<std::int64_t>(cfg_.capacitySamples);
        const std::int64_t from = std::max(mark, origin);
        const std::int64_t to = std::min(target, end);
        if (to > from)
        {
            const auto begin = static_cast<std::size_t>(from - origin);
            const auto count = static_cast<std::size_t>(to - from);
            for (int ch = 0; ch < cfg_.channelsPerSubTrack; ++ch)
                plane(sub, ch).fill(begin, count, 0.0f);
        }

        mark = target;
    }

    void Medium::adoptUsed(int sub, std::int64_t upTo) noexcept
    {
        if (! bound() || sub < 0 || sub >= cfg_.numSubTracks) return;
        const std::int64_t target = std::min(upTo, reelLength());
        std::int64_t& mark = used_[static_cast<std::size_t>(sub)];
        if (target > mark) mark = target;
    }

    void Medium::resetUsed(int sub) noexcept
    {
        if (sub >= 0 && sub < static_cast<int>(used_.size()))
            used_[static_cast<std::size_t>(sub)] = 0;
    }

    void Medium::resetAllUsed() noexcept
    {
        std::fill(used_.begin(), used_.end(), 0);
    }

    void Medium::clearSubTrack(int sub) noexcept
    {
        if (! bound() || sub < 0 || sub >= cfg_.numSubTracks) return;
        // THE RESIDENT PART OF THE TAKE, for the same reason `ensureCommitted`
        // zeroes only what is in the window: erasing tape that is not in memory
        // is the host's to do against the file. The mark is untouched either
        // way -- silence is recorded content, and absence is not.
        const std::int64_t mark = std::min(used(sub),
                                           windowOrigin_
                                           + static_cast<std::int64_t>(cfg_.capacitySamples));
        if (mark <= windowOrigin_) return;
        const auto count = static_cast<std::size_t>(mark - windowOrigin_);
        for (int ch = 0; ch < cfg_.channelsPerSubTrack; ++ch)
            plane(sub, ch).fill(0, count, 0.0f);
    }
}
