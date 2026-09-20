#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chalkwalk::tape
{
    // The tape (DESIGN §40.3, §40.10/§40.11).
    //
    // A Medium is geometry + a non-owning view of caller memory. It is NOT a
    // buffer class: the host decides where the storage lives (a volatile pool
    // slot, a lazily committed reel), and deck_core never allocates it. That is
    // what lets Lockstep's pool slots and the partner app's reels be the same
    // medium without either product's allocation policy leaking into the core.
    //
    // Two axes make it a medium rather than an array:
    //
    //  * **Topology** — Circular (a loop: indices wrap) or Linear (a reel:
    //    indices past the ends do not exist). The heads read the topology; they
    //    do not know which face of the deck they are serving.
    //  * **Its own sample rate.** `mediumRate` is decoupled from the engine rate
    //    (§40.10). The heads already read and write at arbitrary ratio, so a 2×
    //    oversampled tape is configuration, not a mode. Lockstep instantiates 1×.
    //
    // Storage is f32 or i16 (§40.3 — a 16-bit tape halves the RAM). This is the
    // ONE place the depth is branched on: `Store` below. Nothing above it is
    // templated on depth, because an engine templated on its storage would put
    // the choice in every type in the library.
    //
    // **High-water (lazy commit).** Storage is never zero-filled: each sub-track
    // records how far it has been written, and reads past that return silence.
    // An unrecorded tail therefore costs address space, not resident pages —
    // the same discipline the volatile capture slots keep.

    enum class Topology
    {
        Circular,  // a loop: index arithmetic wraps at capacity
        Linear     // a reel: index arithmetic clamps, and out-of-range is silence
    };

    enum class Depth
    {
        F32,
        I16
    };

    // A non-owning, depth-erased sample store. Scale is 32767 both ways, so
    // ±1.0 maps to full scale exactly and an i16 round-trip of an i16-representable
    // value is lossless.
    class Store
    {
    public:
        Store() = default;

        Store(float* p, std::size_t n) noexcept : f32_(p), size_(n) {}
        Store(std::int16_t* p, std::size_t n) noexcept : i16_(p), size_(n) {}

        // ---- A PLANE WOVEN THROUGH OTHERS (`stride` > 1) ----
        //
        // One track of an INTERLEAVED block: `n` samples of this track, each
        // `stride` samples apart. It exists because a reel file is interleaved
        // -- sixteen samples at one tape position, which is what makes sample
        // alignment structural rather than maintained (`PRINCIPLES §3`) -- and a
        // window that matches the file's layout is filled by `memcpy` instead of
        // by a de-interleave.
        //
        // `stride` is 1 for every plane that is its own allocation, which is
        // every existing caller, and the multiply then folds away.
        Store(float* p, std::size_t n, std::size_t stride) noexcept
            : f32_(p), size_(n), stride_(stride) {}
        Store(std::int16_t* p, std::size_t n, std::size_t stride) noexcept
            : i16_(p), size_(n), stride_(stride) {}

        [[nodiscard]] std::size_t stride() const noexcept { return stride_; }

        [[nodiscard]] bool valid() const noexcept { return f32_ != nullptr || i16_ != nullptr; }
        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] Depth depth() const noexcept { return f32_ ? Depth::F32 : Depth::I16; }

        [[nodiscard]] float get(std::size_t i) const noexcept
        {
            const std::size_t k = i * stride_;
            if (f32_ != nullptr) return f32_[k];
            return static_cast<float>(i16_[k]) * (1.0f / 32767.0f);
        }

        void set(std::size_t i, float v) noexcept
        {
            const std::size_t k = i * stride_;
            if (f32_ != nullptr) { f32_[k] = v; return; }
            i16_[k] = quantise(v);
        }

        void add(std::size_t i, float v) noexcept
        {
            const std::size_t k = i * stride_;
            if (f32_ != nullptr) { f32_[k] += v; return; }
            i16_[k] = quantise(get(i) + v);
        }

        void scale(std::size_t i, float g) noexcept
        {
            const std::size_t k = i * stride_;
            if (f32_ != nullptr) { f32_[k] *= g; return; }
            i16_[k] = quantise(get(i) * g);
        }

        void fill(std::size_t begin, std::size_t count, float v) noexcept
        {
            // `std::fill_n` only on a plane of its own; a woven one has to step.
            if (stride_ == 1)
            {
                if (f32_ != nullptr) { std::fill_n(f32_ + begin, count, v); return; }
                std::fill_n(i16_ + begin, count, quantise(v));
                return;
            }
            if (f32_ != nullptr)
            {
                for (std::size_t i = 0; i < count; ++i)
                    f32_[(begin + i) * stride_] = v;
                return;
            }
            const auto q = quantise(v);
            for (std::size_t i = 0; i < count; ++i)
                i16_[(begin + i) * stride_] = q;
        }

        // The sub-store [begin, begin + n). A flat block of storage slices into
        // one plane per (sub-track, channel); a host whose channels are already
        // separate allocations — a juce::AudioBuffer, a pool slot — hands each
        // plane over directly instead.
        // One woven plane out of an interleaved block: `n` samples of this
        // track starting at `offset`, each `stride` apart. `size()` is the
        // number of ADDRESSABLE samples -- what a medium's capacity means --
        // and not the extent of the memory they are spread over.
        [[nodiscard]] Store plane(std::size_t offset, std::size_t n,
                                  std::size_t stride) const noexcept
        {
            if (f32_ != nullptr) return Store{ f32_ + offset, n, stride };
            return Store{ i16_ + offset, n, stride };
        }

        [[nodiscard]] Store slice(std::size_t begin, std::size_t n) const noexcept
        {
            if (f32_ != nullptr) return Store{ f32_ + begin * stride_, n, stride_ };
            return Store{ i16_ + begin * stride_, n, stride_ };
        }

    private:
        static std::int16_t quantise(float v) noexcept
        {
            const float s = std::round(v * 32767.0f);
            if (s >= 32767.0f) return 32767;
            if (s <= -32768.0f) return -32768;
            return static_cast<std::int16_t>(s);
        }

        float* f32_ = nullptr;
        std::int16_t* i16_ = nullptr;
        std::size_t size_ = 0;
        // ONE by default, which is a plane of its own and every existing
        // caller. The multiply by a constant 1 is free.
        std::size_t stride_ = 1;
    };

    class Medium
    {
    public:
        struct Config
        {
            Topology topology = Topology::Circular;
            double mediumRate = 48000.0;  // medium samples per second
            // Width is the HOST's, not the core's (§40.11). These are defaults, not
            // bounds: a host binds whatever N and C it wants (Lockstep binds 4 × 2).
            int numSubTracks = 1;
            int channelsPerSubTrack = 2;  // a stereo default; nothing here assumes it
            int capacitySamples = 0;      // per channel

            // ---- HOW LONG THE REEL IS, WHEN THE STORAGE IS ONLY A WINDOW ----
            //
            // A LINEAR medium may be longer than the memory bound to it. A
            // sixteen-track reel at a studio deck's density is about 4 MB of
            // tape a second, so twenty minutes is five gigabytes: the storage
            // is a WINDOW that a host streams through, and the tape's
            // coordinates have to go on meaning the whole reel or nothing above
            // this class can address it.
            //
            // So there are two lengths. `capacitySamples` is how much memory
            // there is; this is how much TAPE there is. Indices are always the
            // reel's, and `resolve` maps them onto the window -- which means a
            // head, a wear map and a seam all keep working in absolute tape
            // coordinates and know nothing about the streaming.
            //
            // **ZERO MEANS THE WINDOW IS THE WHOLE REEL**, which is the
            // unwindowed medium every existing caller binds and is bit-identical
            // to it. Ignored for a Circular medium: a loop has no outside.
            std::int64_t reelSamples = 0;
        };

        // Samples of storage a Config needs — the host allocates this many
        // floats or int16_ts and hands the pointer to `bind`.
        [[nodiscard]] static std::size_t storageSamples(const Config& c) noexcept;

        Medium() = default;

        // Bind caller-owned storage, as one flat block sliced into planes. Its
        // size must be >= storageSamples(c); a short or absent store leaves the
        // medium unbound (every read silent, every write dropped) rather than
        // reaching past its end.
        void bind(const Config& c, Store store) noexcept;

        // Bind planes the caller already holds separately — one per
        // (sub-track, channel), in that order, each at least `capacitySamples`
        // long. This is how a juce::AudioBuffer or a pool slot becomes a medium:
        // its channels are distinct allocations and nothing may copy them.
        void bindPlanes(const Config& c, const Store* planes, int count) noexcept;

        // ---- BIND ONE INTERLEAVED BLOCK ----
        //
        // A frame is `numSubTracks * channelsPerSubTrack` consecutive samples at
        // one position, which is a reel file's layout. Each plane is a woven
        // `Store` into it, so the medium reads and writes exactly as before and
        // the storage can be `memcpy`d to and from the file.
        void bindInterleaved(const Config& c, Store store) noexcept;

        void unbind() noexcept;

        [[nodiscard]] bool bound() const noexcept { return ! planes_.empty(); }
        [[nodiscard]] const Config& config() const noexcept { return cfg_; }
        [[nodiscard]] Topology topology() const noexcept { return cfg_.topology; }
        [[nodiscard]] double mediumRate() const noexcept { return cfg_.mediumRate; }
        [[nodiscard]] int capacity() const noexcept { return cfg_.capacitySamples; }

        // HOW MUCH TAPE, as against how much memory. They differ only on a
        // windowed linear medium; everywhere else this is `capacity()`.
        //
        // RESOLVED AT BIND AND STORED, not worked out here, because `resolve`
        // is in the innermost gather loop -- thirty-four taps a sample a track
        // -- and a branch on the topology plus a reach into `cfg_` is not free
        // at that rate. This is the one place the window costs anything at all,
        // so it is the one place worth spending care on.
        [[nodiscard]] std::int64_t reelLength() const noexcept { return reelLength_; }

        // WHICH STRETCH OF THE REEL IS RESIDENT. `origin` is the reel index the
        // first stored sample holds, so the window covers
        // `[origin, origin + capacity())`.
        //
        // MOVING IT DOES NOT MOVE ANY AUDIO. The medium does not own its
        // storage and cannot stream: sliding the window says the memory now
        // means a different stretch of tape, and it is the HOST's business to
        // have put that stretch there. Slide it without filling it and the
        // reads come back as whatever was left behind, which is the same
        // contract `bindPlanes` already has and is why streaming belongs above
        // this class.
        //
        // Clamped so the window cannot hang off either end of the reel, because
        // a window that did would make some of its own storage unaddressable.
        void setWindow(std::int64_t origin) noexcept
        {
            const std::int64_t last = reelLength_
                                    - static_cast<std::int64_t>(cfg_.capacitySamples);
            windowOrigin_ = std::clamp<std::int64_t>(origin, 0, std::max<std::int64_t>(0, last));
        }

        [[nodiscard]] std::int64_t windowOrigin() const noexcept { return windowOrigin_; }

        // Whether this index is resident right now -- what a streaming host
        // asks before it lets the heads at a position, and what an underrun
        // is the absence of (`PRINCIPLES §8` in the host that has one).
        [[nodiscard]] bool resident(std::int64_t i) const noexcept
        {
            if (cfg_.topology == Topology::Circular)
                return true;
            return i >= windowOrigin_
                && i < windowOrigin_ + static_cast<std::int64_t>(cfg_.capacitySamples);
        }
        [[nodiscard]] int numSubTracks() const noexcept { return cfg_.numSubTracks; }
        [[nodiscard]] int channels() const noexcept { return cfg_.channelsPerSubTrack; }
        [[nodiscard]] Depth depth() const noexcept
        {
            return planes_.empty() ? Depth::F32 : planes_.front().depth();
        }

        // How far this sub-track has been written (its high-water mark). Reads
        // past it are silence; nothing below it is ever uninitialised.
        //
        // **IN THE REEL'S COORDINATES, NOT THE WINDOW'S**, which is what makes
        // it survive the window moving: how much of a tape has been recorded is
        // a fact about the tape. On an unwindowed medium the two are the same
        // number, which is why this changed nothing for existing callers.
        //
        // 64-bit because a reel is: at a studio deck's density an `int` runs
        // out after about four and a half hours of tape, which is a limit
        // nobody should meet by accident.
        [[nodiscard]] std::int64_t used(int sub) const noexcept
        {
            return (sub >= 0 && sub < static_cast<int>(used_.size())) ? used_[static_cast<std::size_t>(sub)] : 0;
        }

        // Raise the high-water mark to `upTo` samples, zeroing the storage the
        // mark skipped over. Call it BEFORE writing there: uncommitted storage is
        // garbage, and this is what turns garbage into silence. A write head
        // commits the span its kernel is about to touch, which is why a scatter
        // deposit into virgin tape adds to zero rather than to whatever the
        // allocator left. Never lowers the mark — that is `resetUsed`.
        void ensureCommitted(int sub, std::int64_t upTo) noexcept;

        // Raise the mark WITHOUT zeroing: the storage below `upTo` already holds
        // audio the caller vouches for — a loop loaded from the pool, a take
        // restored from undo, a buffer the host filled before binding. The two
        // verbs are the difference between virgin tape and a tape with a
        // recording on it, and calling the wrong one either wipes the take or
        // plays back uninitialised memory.
        void adoptUsed(int sub, std::int64_t upTo) noexcept;

        void resetUsed(int sub) noexcept;
        void resetAllUsed() noexcept;

        // Map a signed medium index onto storage. Circular media wrap; linear
        // media reject out-of-range. Returns false when the index does not exist.
        [[nodiscard]] bool resolve(std::int64_t i, int& out) const noexcept
        {
            const std::int64_t cap = cfg_.capacitySamples;
            if (cap <= 0) return false;
            if (cfg_.topology == Topology::Circular)
            {
                std::int64_t m = i % cap;
                if (m < 0) m += cap;
                out = static_cast<int>(m);
                return true;
            }
            // ---- LINEAR, AND POSSIBLY A WINDOW ONTO SOMETHING LONGER ----
            //
            // Two rejections, and they are different failures. Off the REEL is
            // tape that does not exist, which is silence and always was. Off the
            // WINDOW is tape that exists and is not in memory, which is a
            // streaming host that has not kept up -- indistinguishable here, and
            // `resident()` is how a caller tells them apart before it gets here.
            // ONE TEST, NOT TWO, and that it is one is a property of the clamp
            // rather than an economy. `setWindow` keeps the window inside the
            // reel and `reelLengthOf` keeps the reel at least as long as the
            // window, so **being in the window implies being on the reel** and
            // the reel's own bound is unreachable here. An unbound medium has a
            // capacity of zero and so rejects everything.
            //
            // The first draft tested both, which cost a compare pair in the
            // innermost gather loop for a condition that cannot be true.
            //
            // WHAT THE WINDOW COSTS, MEASURED (`RemanenceBench perf`, Capstan,
            // 48 kHz, read path, ns per sample per track):
            //
            //     no window at all          190.9
            //     window, first draft       208.3   +17.4 ns  (+9.1 %)
            //     window, as it stands      199.9    +9.0 ns  (+4.7 %)
            //
            // So dropping the redundant test and the topology branch in `read`
            // gave back half of it, and the remaining 9 ns is the subtract and
            // the 64-bit compare -- which is what addressing a reel longer than
            // memory actually costs, rather than an oversight. Sixteen tracks
            // playing goes from 0.13 of a core to 0.136.
            //
            // What is LOST is telling the two failures apart, and they are
            // different: off the reel is tape that does not exist, off the
            // window is tape that exists and is not resident. `resident()` is
            // where a streaming host asks that, before it gets here rather than
            // inside the loop.
            const std::int64_t k = i - windowOrigin_;
            if (k < 0 || k >= cap) return false;
            out = static_cast<int>(k);
            return true;
        }

        // Sample access at an already-resolved index. The heads own the index
        // arithmetic (kernel taps, wrap, direction); the medium owns storage,
        // depth, and the high-water rule.
        [[nodiscard]] float read(int sub, int ch, std::int64_t i) const noexcept
        {
            int k = 0;
            if (! bound() || ! resolve(i, k)) return 0.0f;
            // ---- WHICH INDEX THE MARK IS COMPARED WITH, AND IT IS ONE SUM ----
            //
            // The two topologies want different indices and get them from the
            // same arithmetic, which is why there is no branch here.
            //
            // A LOOP wraps, so index 19 on a sixteen-sample loop IS index 3 and
            // the mark has to be read against the RESOLVED one. Comparing the
            // raw index there would make every lap past the first play silence,
            // and the library's own circular test says so -- which is how that
            // was found rather than shipped.
            //
            // A REEL does not wrap, and its mark is in the reel's coordinates,
            // so the RAW one is right. Using the resolved one would make a
            // windowed reel's recorded extent move every time the window did:
            // silence in the middle of a take.
            //
            // `windowOrigin_` IS ALWAYS ZERO ON A LOOP -- `setWindow` clamps it
            // to a range of zero width there, because a loop's storage is all
            // of it -- so `k + windowOrigin_` is `k` on a loop and `i` on a
            // reel. One add replaces a branch on the topology, in a loop that
            // runs thirty-four times a sample a track.
            if (static_cast<std::int64_t>(k) + windowOrigin_ >= used(sub))
                return 0.0f;
            return plane(sub, ch).get(static_cast<std::size_t>(k));
        }

        void add(int sub, int ch, std::int64_t i, float v) noexcept
        {
            int k = 0;
            if (! bound() || ! resolve(i, k)) return;
            plane(sub, ch).add(static_cast<std::size_t>(k), v);
        }

        void write(int sub, int ch, std::int64_t i, float v) noexcept
        {
            int k = 0;
            if (! bound() || ! resolve(i, k)) return;
            plane(sub, ch).set(static_cast<std::size_t>(k), v);
        }

        void scale(int sub, int ch, std::int64_t i, float g) noexcept
        {
            int k = 0;
            if (! bound() || ! resolve(i, k)) return;
            plane(sub, ch).scale(static_cast<std::size_t>(k), g);
        }

        // Erase a sub-track's content without dropping its high-water mark
        // (silence is recorded content; absence is not).
        void clearSubTrack(int sub) noexcept;

    private:
        // One store per (sub-track, channel), sub-track-major. Whether they came
        // from one flat block or from a host's separate channel allocations stops
        // mattering here — which is the point.
        [[nodiscard]] Store& plane(int sub, int ch) noexcept
        {
            return planes_[static_cast<std::size_t>(sub * cfg_.channelsPerSubTrack + ch)];
        }
        [[nodiscard]] const Store& plane(int sub, int ch) const noexcept
        {
            return planes_[static_cast<std::size_t>(sub * cfg_.channelsPerSubTrack + ch)];
        }

        Config cfg_{};
        std::vector<Store> planes_;
        // Per sub-track high-water, in samples, IN THE REEL'S COORDINATES.
        std::vector<std::int64_t> used_;
        // Which reel index the first stored sample holds. Always 0 on a medium
        // whose storage is the whole reel, which is every unwindowed one.
        std::int64_t windowOrigin_ = 0;
        // How much tape, resolved once when the storage is bound. Zero when
        // nothing is bound, which is what makes an unbound medium reject every
        // index without needing a `bound()` test of its own in `resolve`.
        std::int64_t reelLength_ = 0;
    };
}
