#pragma once

// =============================================================================
// A cache for filter designs, and the reason it is worth having.
//
// Preparing one Capstan takes about 970 ms, of which the crosstalk coupling
// filters are 544 and the two loss filters 201 (`RemanenceBench` and
// `RemanenceCoreTests "[cost]"`). None of that work depends on the instance:
// a filter design is a pure function of the head geometry, the tape speed and
// the rate. The same Capstan is designed again for every plugin instance, on
// every sample-rate change, and 272 times over in the test suite.
//
// NOT FOR THE AUDIO THREAD, EVER. This locks and it allocates, which is exactly
// what `PRINCIPLES §8` forbids there. It is for `prepare` and for the design
// thread, both of which may block. The audio thread's route to a new design is
// `PlaybackFilter::adopt`, which does neither.
//
// KEYED ON EVERY FIELD THE DESIGN CAN READ, not on the ones it currently does.
// A cache keyed on a subset returns a filter designed for a different machine
// the moment somebody uses one more field, and it does it silently -- the
// failure is a wrong sound, not a crash. So the key is the whole geometry, and
// adding a field to `HeadGeometry` without adding it here is a compile error
// rather than a bug.
// =============================================================================

#include <chalkwalk/tape/LossEffects.h>
#include <chalkwalk/tape/TapeEq.h>

#include <cstddef>
#include <list>
#include <mutex>
#include <utility>

namespace chalkwalk::tape
{
    // The whole of a head, compared field by field. Deliberately not a memcmp:
    // padding bytes are unspecified, so two identical heads could compare
    // different and the cache would simply never hit.
    [[nodiscard]] inline bool sameHead(const HeadGeometry& a,
                                       const HeadGeometry& b) noexcept
    {
        return a.gapLengthMetres    == b.gapLengthMetres
            && a.turns              == b.turns
            && a.efficiency         == b.efficiency
            && a.spacingMetres      == b.spacingMetres
            && a.thicknessMetres    == b.thicknessMetres
            && a.trackWidthMetres   == b.trackWidthMetres
            && a.guardBandMetres    == b.guardBandMetres
            && a.shieldingDb        == b.shieldingDb
            && a.azimuthRadians     == b.azimuthRadians
            && a.faceLengthMetres   == b.faceLengthMetres
            && a.shieldGapMetres    == b.shieldGapMetres
            && a.contourRounding    == b.contourRounding
            && a.contourShieldGain  == b.contourShieldGain
            && a.gapGeometry        == b.gapGeometry;
    }

    struct DesignKeyHead
    {
        HeadGeometry head{};
        double sampleRateHz = 0.0;
        double tapeSpeedMps = 0.0;
        int    taps = 0;
        int    extra = 0;          // separation, track count, whatever the caller needs
        // The reproduce equalisation, which is part of the design and therefore
        // part of its identity: two machines with the same head on different
        // standards are two different filters.
        EqCurve eq{};

        [[nodiscard]] bool operator==(const DesignKeyHead& o) const noexcept
        {
            return sampleRateHz == o.sampleRateHz
                && tapeSpeedMps == o.tapeSpeedMps
                && taps == o.taps
                && extra == o.extra
                && eq == o.eq
                && sameHead(head, o.head);
        }
    };

    // Small, and bounded on purpose. A session uses a handful of designs -- one
    // per machine per speed on the grid -- and an unbounded cache in a plugin
    // is a leak with a good excuse.
    template <typename Value, std::size_t kCapacity = 64>
    class DesignCache
    {
    public:
        // Returns a COPY. The caller owns filter state (histories, cursors) and
        // must not share it between tracks or instances; only the DESIGN is
        // shared, and copying a designed filter is a vector copy against an FIR
        // design plus a minimum-phase transform.
        template <typename Make>
        [[nodiscard]] Value get(const DesignKeyHead& key, Make&& make)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                for (auto it = entries_.begin(); it != entries_.end(); ++it)
                    if (it->first == key)
                    {
                        // Most recently used to the front, so the eviction below
                        // drops what a session has stopped asking for.
                        entries_.splice(entries_.begin(), entries_, it);
                        return it->second;
                    }
            }

            // DESIGNED OUTSIDE THE LOCK. Two threads asking for the same new
            // design will both build it and one will be thrown away, which
            // costs a duplicate design once; holding the lock across the design
            // would instead let one slow build stall every other machine's
            // prepare.
            Value made = make();

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& e : entries_)
                    if (e.first == key)
                        return e.second;      // somebody else won the race
                entries_.emplace_front(key, made);
                while (entries_.size() > kCapacity)
                    entries_.pop_back();
            }
            return made;
        }

        void clear()
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            entries_.clear();
        }

        [[nodiscard]] std::size_t size() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return entries_.size();
        }

    private:
        mutable std::mutex mutex_;
        std::list<std::pair<DesignKeyHead, Value>> entries_;
    };
}
