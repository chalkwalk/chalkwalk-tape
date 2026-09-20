// MediumTest — chalkwalk::tape::Medium, the tape itself (DESIGN §40.3, §40.11).
//
// The medium is geometry plus a non-owning view of caller storage, and it owns
// exactly three rules that everything above it depends on:
//
//   * **Topology** — circular indices wrap, linear indices out of range do not
//     exist. The heads do index arithmetic; the medium decides what an index
//     means.
//   * **Depth** — f32 or i16 behind one accessor, so nothing above is templated
//     on storage. An i16 medium quantises; it must not shift, clip early, or
//     lose ±1.0.
//   * **High-water (lazy commit)** — storage is never zero-filled, so a read
//     past a sub-track's mark is silence rather than whatever the allocator
//     left, and committing a span turns garbage into silence *before* a write
//     head deposits into it.

#include "LegacyCheck.h"
#include <chalkwalk/tape/Medium.h>

#include <array>
#include <cstdint>
#include <vector>

    namespace
    {
        // Storage deliberately filled with garbage: nothing may read it before a
        // commit, and every zero the tests see must come from ensureCommitted.
        template <typename T>
        std::vector<T> dirtyStorage(std::size_t n, T poison)
        {
            return std::vector<T>(n, poison);
        }
    }
TEST_CASE("medium") {
        // ── Geometry + binding ───────────────────────────────────────────────
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Circular;
            cfg.numSubTracks = 4;
            cfg.channelsPerSubTrack = 2;
            cfg.capacitySamples = 100;

            const std::size_t need = chalkwalk::tape::Medium::storageSamples(cfg);
            CHECK_MSG(need == 800, "storageSamples = subTracks * channels * capacity");

            chalkwalk::tape::Medium m;
            CHECK_MSG(! m.bound(), "a default medium is unbound");

            // A store too small to hold the geometry must be refused outright
            // rather than half-bound: a medium that reads past its storage is a
            // worse failure than a silent one.
            auto small = dirtyStorage<float>(need - 1, 9.0f);
            m.bind(cfg, chalkwalk::tape::Store{small.data(), small.size()});
            CHECK_MSG(! m.bound(), "a short store leaves the medium unbound");
            CHECK_MSG(feq(m.read(0, 0, 0), 0.0f), "an unbound medium reads silence");

            auto store = dirtyStorage<float>(need, 9.0f);
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});
            CHECK_MSG(m.bound(), "a sufficient store binds");
            CHECK_MSG(m.capacity() == 100 && m.numSubTracks() == 4 && m.channels() == 2,
                  "geometry survives binding");
            CHECK_MSG(m.depth() == chalkwalk::tape::Depth::F32, "a float store is an f32 medium");
        }

        // ── High-water: uncommitted storage is never read ────────────────────
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.numSubTracks = 2;
            cfg.channelsPerSubTrack = 2;
            cfg.capacitySamples = 64;

            auto store = dirtyStorage<float>(chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});

            CHECK_MSG(m.used(0) == 0 && m.used(1) == 0, "a fresh medium has recorded nothing");
            CHECK_MSG(feq(m.read(0, 0, 0), 0.0f), "an uncommitted sample reads silence, not garbage");
            CHECK_MSG(feq(m.read(0, 0, 63), 0.0f), "...at every uncommitted index");

            m.ensureCommitted(0, 32);
            CHECK_MSG(m.used(0) == 32, "committing raises the high-water mark");
            CHECK_MSG(m.used(1) == 0, "...for that sub-track only");
            CHECK_MSG(feq(m.read(0, 0, 31), 0.0f), "committed storage is zeroed");
            CHECK_MSG(feq(m.read(0, 1, 31), 0.0f), "...on every channel of the sub-track");
            CHECK_MSG(feq(m.read(0, 0, 32), 0.0f), "past the mark still reads silence");

            // The garbage beyond the mark is still there — the medium hides it,
            // and hiding it is the whole point of the mark.
            m.add(0, 0, 40, 0.0f);  // resolves, but never commits
            CHECK_MSG(feq(m.read(0, 0, 40), 0.0f), "an add past the mark does not expose garbage");

            // A commit that skips over content zeroes only what it skipped.
            m.write(0, 0, 10, 0.5f);
            m.ensureCommitted(0, 64);
            CHECK_MSG(m.used(0) == 64, "the mark extends");
            CHECK_MSG(feq(m.read(0, 0, 10), 0.5f), "content below the old mark survives a commit");
            CHECK_MSG(feq(m.read(0, 0, 50), 0.0f), "the newly committed span is silent");

            m.ensureCommitted(0, 999);
            CHECK_MSG(m.used(0) == 64, "the mark never exceeds capacity");

            // Silence is recorded content; absence is not.
            m.write(0, 0, 10, 0.5f);
            m.clearSubTrack(0);
            CHECK_MSG(feq(m.read(0, 0, 10), 0.0f) && m.used(0) == 64,
                  "clearSubTrack erases audio but keeps the mark");
            m.resetUsed(0);
            CHECK_MSG(m.used(0) == 0, "resetUsed drops the mark");
        }

        // ── Planes: channels that were never one flat block ──────────────────
        // A juce::AudioBuffer's channels are separate allocations, and a pool slot
        // is not ours to copy. Binding planes is how those become a medium; above
        // this call nothing knows which kind of storage it is reading.
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.numSubTracks = 2;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 8;

            std::vector<float> left(8, 0.0f), right(8, 0.0f);
            const std::array<chalkwalk::tape::Store, 2> planes{ chalkwalk::tape::Store{ left.data(), left.size() },
                                                   chalkwalk::tape::Store{ right.data(), right.size() } };

            chalkwalk::tape::Medium m;
            m.bindPlanes(cfg, planes.data(), 2);
            CHECK_MSG(m.bound(), "separate planes bind");

            m.adoptUsed(0, 8);
            m.adoptUsed(1, 8);
            m.write(0, 0, 2, 0.5f);
            m.write(1, 0, 2, -0.5f);
            CHECK_MSG(feq(left[2], 0.5f) && feq(right[2], -0.5f),
                  "writes land in the caller's own arrays");

            // A plane shorter than the capacity is a buffer overrun waiting to
            // happen, so it does not bind at all.
            std::vector<float> stunted(4, 0.0f);
            const std::array<chalkwalk::tape::Store, 2> bad{ chalkwalk::tape::Store{ left.data(), left.size() },
                                                chalkwalk::tape::Store{ stunted.data(), stunted.size() } };
            m.bindPlanes(cfg, bad.data(), 2);
            CHECK_MSG(! m.bound(), "a plane shorter than the capacity refuses to bind");
        }

        // ── adoptUsed vouches; ensureCommitted wipes ─────────────────────────
        // The two verbs are the difference between a tape with a recording on it
        // and virgin tape. Calling the wrong one either wipes a take or plays back
        // uninitialised memory.
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 8;

            std::vector<float> store(8, 1.0f);
            chalkwalk::tape::Medium m;
            m.bindPlanes(cfg, std::array<chalkwalk::tape::Store, 1>{
                                  chalkwalk::tape::Store{ store.data(), store.size() } }.data(), 1);

            m.adoptUsed(0, 8);
            CHECK_MSG(m.used(0) == 8 && feq(m.read(0, 0, 3), 1.0f),
                  "adoptUsed raises the mark over content that is already there");

            m.resetUsed(0);
            m.ensureCommitted(0, 8);
            CHECK_MSG(m.used(0) == 8 && feq(m.read(0, 0, 3), 0.0f),
                  "ensureCommitted zeroes the span it claims");
        }

        // ── Circular topology: indices wrap, in both directions ──────────────
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Circular;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;

            auto store = dirtyStorage<float>(chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});
            m.ensureCommitted(0, 16);

            m.write(0, 0, 3, 1.0f);
            CHECK_MSG(feq(m.read(0, 0, 19), 1.0f), "index + capacity is the same sample");
            CHECK_MSG(feq(m.read(0, 0, -13), 1.0f), "a negative index wraps forward (reverse reads)");

            m.add(0, 0, -1, 0.25f);  // writes at 15
            CHECK_MSG(feq(m.read(0, 0, 15), 0.25f), "a write before zero lands at the seam");

            int k = 0;
            CHECK_MSG(m.resolve(-1, k) && k == 15, "resolve wraps a negative index");
            CHECK_MSG(m.resolve(32, k) && k == 0, "resolve wraps a multiple of capacity");
        }

        // ── Linear topology: off the reel is nowhere ─────────────────────────
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;

            auto store = dirtyStorage<float>(chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});
            m.ensureCommitted(0, 16);

            int k = 0;
            CHECK_MSG(! m.resolve(-1, k), "a linear index before the reel does not exist");
            CHECK_MSG(! m.resolve(16, k), "a linear index past the reel does not exist");
            CHECK_MSG(m.resolve(0, k) && k == 0, "the first sample exists");
            CHECK_MSG(m.resolve(15, k) && k == 15, "the last sample exists");

            // Writes off the end are dropped, not wrapped onto the start — a
            // reel that wraps is a loop, and confusing the two is the bug this
            // topology exists to prevent.
            m.write(0, 0, 0, 1.0f);
            m.write(0, 0, 16, 0.5f);
            CHECK_MSG(feq(m.read(0, 0, 0), 1.0f), "a write past the end does not wrap onto the start");
            CHECK_MSG(feq(m.read(0, 0, -1), 0.0f), "a read before the start is silence");
            CHECK_MSG(feq(m.read(0, 0, 16), 0.0f), "a read past the end is silence");
        }

        // ── A window onto a longer reel ──────────────────────────────────────
        //
        // The storage is 16 samples and the TAPE is 64, which is a reel four
        // times the size of the memory bound to it. Every index below is the
        // reel's; nothing here knows or cares where the window happens to be,
        // which is the property that lets a head, a wear map and a seam stay
        // written in tape coordinates while a host streams underneath them.
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;
            cfg.reelSamples = 64;

            auto store = dirtyStorage<float>(chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});

            CHECK_MSG(m.capacity() == 16, "the window is the memory");
            CHECK_MSG(m.reelLength() == 64, "the reel is the tape");
            CHECK_MSG(m.windowOrigin() == 0, "a fresh window starts at the head of the reel");

            // The whole reel is committed, which is a statement about TAPE and
            // not about memory: only the resident part can be zeroed, and the
            // mark moves over the rest because the host owns what is not here.
            m.ensureCommitted(0, 64);
            CHECK_MSG(m.used(0) == 64, "the mark covers the reel, not the window");

            int k = 0;
            CHECK_MSG(m.resolve(0, k) && k == 0, "the first reel sample is at the window's start");
            CHECK_MSG(m.resolve(15, k) && k == 15, "the last resident sample resolves");
            CHECK_MSG(! m.resolve(16, k), "tape outside the window does not resolve");
            CHECK_MSG(! m.resolve(64, k), "tape off the end of the reel does not resolve");
            CHECK_MSG(m.resident(15) && ! m.resident(16), "residency is the window");

            m.write(0, 0, 3, 1.0f);
            CHECK_MSG(feq(m.read(0, 0, 3), 1.0f), "a resident write reads back");

            // ---- AND NOW THE WINDOW MOVES ----
            //
            // Sliding it moves no audio: the same 16 samples of memory now MEAN
            // reel samples 32..47. A host streams the file into them; this test
            // is the medium's half of that contract, so it writes them itself.
            m.setWindow(32);
            CHECK_MSG(m.windowOrigin() == 32, "the window did not move");
            CHECK_MSG(! m.resolve(3, k), "the old stretch is no longer addressable");
            CHECK_MSG(m.resolve(32, k) && k == 0, "the new stretch starts at storage zero");
            CHECK_MSG(m.resolve(47, k) && k == 15, "and ends at the last sample of it");

            m.write(0, 0, 40, 0.5f);
            CHECK_MSG(feq(m.read(0, 0, 40), 0.5f), "a write into the moved window reads back");

            // THE MARK DID NOT MOVE WITH IT. How much of a tape has been
            // recorded is a fact about the tape, and a take does not become
            // unrecorded because the transport wound past it.
            CHECK_MSG(m.used(0) == 64, "the window moved the recorded extent");
        }

        // ── The mark is the reel's, and a window past it reads silence ───────
        //
        // THE CASE THE TEST ABOVE CANNOT SEE, and it took an injection to find
        // that out: with the whole reel committed, comparing the mark against
        // the window offset gives the same answer as comparing it against the
        // reel index, so the assertion passed with the comparison broken.
        //
        // What separates them is a PARTLY recorded reel whose window sits near
        // the end of the take. Reel sample 45 is past a mark of 40 and must be
        // silence; its window offset is 13, which is comfortably inside 40 and
        // would play whatever the storage last held. That is unrecorded tape
        // sounding like a take, which is the failure this whole coordinate
        // rule exists to prevent.
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;
            cfg.reelSamples = 64;

            auto store = dirtyStorage<float>(chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});

            // Forty samples of tape have been recorded, and the rest is virgin.
            m.ensureCommitted(0, 40);
            CHECK_MSG(m.used(0) == 40, "the take is forty samples long");

            m.setWindow(32);
            // Inside the take and resident: audible.
            m.write(0, 0, 35, 1.0f);
            CHECK_MSG(feq(m.read(0, 0, 35), 1.0f), "tape inside the take went silent");

            // Past the take and resident, WITH SOMETHING UNDER IT. The write
            // lands in storage -- writing does not move the mark, and
            // uncommitted storage is garbage by the medium's own discipline --
            // so the mark is the only thing keeping it quiet.
            //
            // AND IT HAS TO BE PUT THERE AFTER THE COMMIT, which is the second
            // thing an injection had to teach this test: `ensureCommitted`
            // zeroed the whole resident window, so reading uncommitted tape
            // gave 0.0 whichever index the mark was compared against, and the
            // assertion passed with the comparison broken.
            m.write(0, 0, 45, 0.75f);
            CHECK_MSG(feq(m.read(0, 0, 45), 0.0f),
                      "unrecorded tape inside the window played back");

            // A window cannot hang off either end, because storage that fell
            // outside the reel would be unaddressable.
            m.setWindow(-8);
            CHECK_MSG(m.windowOrigin() == 0, "the window ran off the front of the reel");
            m.setWindow(1000);
            CHECK_MSG(m.windowOrigin() == 48, "the window ran off the end of the reel");
        }

        // ── Committing tape that is mostly not in memory ─────────────────────
        //
        // `ensureCommitted` takes a span of TAPE and zeroes the part of it that
        // is resident. With the window at 32 and the whole 64-sample reel being
        // committed, that intersection is exactly the window -- and the storage
        // either side of the window is not this medium's to touch.
        //
        // THE GUARD IS THE ASSERTION. Sixteen samples are bound out of a
        // twenty-four sample allocation, so the last eight belong to nobody and
        // must come through untouched. Without it, a commit that ignores the
        // window writes `mark` to `target` from a negative offset, which is not
        // a wrong value but a wrong ADDRESS -- and an assertion on values
        // cannot see that at all.
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;
            cfg.reelSamples = 64;

            constexpr std::size_t kGuard = 8;
            auto store = dirtyStorage<float>(
                chalkwalk::tape::Medium::storageSamples(cfg) + kGuard, 9.0f);

            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), 16});

            m.setWindow(32);
            m.ensureCommitted(0, 64);

            CHECK_MSG(m.used(0) == 64, "the mark covers tape that is not resident");
            for (std::size_t i = 0; i < 16; ++i)
                CHECK_MSG(feq(store[i], 0.0f), "the resident window was not committed");
            for (std::size_t i = 16; i < 16 + kGuard; ++i)
                CHECK_MSG(feq(store[i], 9.0f), "the commit wrote outside the window");
        }

        // ── Interleaved storage is the same medium, woven ────────────────────
        //
        // A reel file is interleaved -- sixteen samples at one tape position --
        // and a window that matches it is filled by `memcpy` rather than by a
        // de-interleave. What must not change is what the medium IS: the same
        // indices, the same marks, the same silence past the mark. So the test
        // is an equivalence, run against a planar medium of the same geometry
        // doing the same things.
        {
            constexpr int kSubs = 4;
            constexpr int kCap = 64;

            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = kSubs;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = kCap;

            auto flat = dirtyStorage<float>(
                chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            auto woven = dirtyStorage<float>(
                chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);

            chalkwalk::tape::Medium planar, inter;
            planar.bind(cfg, chalkwalk::tape::Store{flat.data(), flat.size()});
            inter.bindInterleaved(cfg, chalkwalk::tape::Store{woven.data(), woven.size()});

            CHECK_MSG(inter.bound(), "an interleaved medium did not bind");
            CHECK_MSG(inter.capacity() == kCap, "the interleaved capacity is wrong");
            CHECK_MSG(inter.numSubTracks() == kSubs, "the interleaved width is wrong");

            for (int sub = 0; sub < kSubs; ++sub)
            {
                planar.ensureCommitted(sub, 40);
                inter.ensureCommitted(sub, 40);
            }

            for (int sub = 0; sub < kSubs; ++sub)
                for (int i = 0; i < 40; ++i)
                {
                    const float v = 0.01f * float(i) + 0.1f * float(sub);
                    planar.write(sub, 0, i, v);
                    inter.write(sub, 0, i, v);
                }

            bool same = true;
            for (int sub = 0; sub < kSubs && same; ++sub)
                for (int i = 0; i < kCap; ++i)
                    if (! feq(planar.read(sub, 0, i), inter.read(sub, 0, i)))
                    { same = false; break; }
            CHECK_MSG(same, "interleaved storage reads back differently from planar");

            // AND IT IS ACTUALLY WOVEN, which the equivalence above cannot see:
            // sub-track 2's sample 5 must live at `5 * 4 + 2` in the block, and
            // if it does not then this is a planar medium wearing a new name.
            CHECK_MSG(feq(woven[std::size_t(5 * kSubs + 2)], planar.read(2, 0, 5)),
                      "the interleaved block is not interleaved");

            // The mark still keeps unrecorded tape quiet.
            CHECK_MSG(feq(inter.read(0, 0, 50), 0.0f),
                      "an interleaved medium played past its mark");
        }

        // ── Repointing: same tape, different memory ──────────────────────────
        //
        // What a streaming host does when it has filled a second window and
        // wants the heads reading from it. The medium must come out of it
        // pointed at the new block, addressing the new stretch of reel, and
        // still knowing how much of the tape has been recorded -- that last one
        // being the whole reason this is not just another `bind`.
        {
            constexpr int kSubs = 2;
            constexpr int kCap = 16;

            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = kSubs;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = kCap;
            cfg.reelSamples = 256;

            auto first = dirtyStorage<float>(
                chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            auto second = dirtyStorage<float>(
                chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);

            chalkwalk::tape::Medium m;
            m.bindInterleaved(cfg, chalkwalk::tape::Store{first.data(), first.size()});

            // A take that runs well past either window.
            m.ensureCommitted(0, 200);
            m.write(0, 0, 5, 0.5f);
            CHECK_MSG(m.used(0) == 200, "the take is 200 samples long");

            // The host fills the second block for the stretch at 100 and hands
            // it over. Sub-track 0's sample 100 sits at offset 0 of that block,
            // woven two apart.
            second[0] = 0.25f;

            CHECK_MSG(m.repoint(chalkwalk::tape::Store{second.data(), second.size()}, 100),
                      "the medium refused a good window");
            CHECK_MSG(m.windowOrigin() == 100, "the window did not move");
            CHECK_MSG(feq(m.read(0, 0, 100), 0.25f),
                      "the medium is not reading the new block");
            int where = 0;
            CHECK_MSG(! m.resolve(5, where), "the old stretch is still addressable");

            // THE MARKS SURVIVED, which `bindInterleaved` would have zeroed --
            // and a reel whose recorded extent went back to nothing every time
            // the transport crossed a window boundary would fall silent behind
            // the playhead.
            CHECK_MSG(m.used(0) == 200, "repointing forgot how much tape was recorded");

            // AND A STORE TOO SMALL IS REFUSED RATHER THAN HALF-TAKEN, so a
            // caller that gets it wrong keeps a working medium on the old
            // window instead of a broken one on neither.
            std::vector<float> tiny(4, 0.0f);
            CHECK_MSG(! m.repoint(chalkwalk::tape::Store{tiny.data(), tiny.size()}, 0),
                      "the medium accepted a window too small for it");
            CHECK_MSG(m.windowOrigin() == 100, "a refused repoint moved the window anyway");
            CHECK_MSG(feq(m.read(0, 0, 100), 0.25f),
                      "a refused repoint left the medium pointing at nothing");
        }

        // ── An unwindowed medium is exactly what it was ──────────────────────
        //
        // `reelSamples = 0` is every existing caller, and the two lengths are
        // then one length. This is the assertion that says adding the window
        // cost them nothing.
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Linear;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;

            auto store = dirtyStorage<float>(chalkwalk::tape::Medium::storageSamples(cfg), 9.0f);
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});

            CHECK_MSG(m.reelLength() == 16, "an unwindowed reel is its own capacity");
            CHECK_MSG(m.resident(0) && m.resident(15) && ! m.resident(16),
                      "the window is the whole of it");
            m.setWindow(8);
            CHECK_MSG(m.windowOrigin() == 0,
                      "a window the size of its reel has nowhere to slide to");
        }

        // ── i16 depth: same medium, half the RAM ─────────────────────────────
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.topology = chalkwalk::tape::Topology::Circular;
            cfg.numSubTracks = 2;
            cfg.channelsPerSubTrack = 2;
            cfg.capacitySamples = 32;

            auto store = dirtyStorage<std::int16_t>(chalkwalk::tape::Medium::storageSamples(cfg),
                                                    std::int16_t{ 999 });
            chalkwalk::tape::Medium m;
            m.bind(cfg, chalkwalk::tape::Store{store.data(), store.size()});
            CHECK_MSG(m.bound() && m.depth() == chalkwalk::tape::Depth::I16, "an int16 store is an i16 medium");

            m.ensureCommitted(0, 32);
            CHECK_MSG(feq(m.read(0, 0, 5), 0.0f), "committing zeroes an i16 medium too");

            // Round-trip within one quantum. 1/32768 is the step; allow one.
            constexpr float kQ = 1.0f / 32767.0f;
            for (const float v : { -1.0f, -0.5f, -0.001f, 0.0f, 0.001f, 0.25f, 0.5f, 1.0f })
            {
                m.write(0, 0, 7, v);
                CHECK_MSG(feq(m.read(0, 0, 7), v, kQ), "i16 round-trips within one quantum");
            }

            // Full scale is exact in both directions, and beyond it clips rather
            // than wrapping — an i16 tape that wrapped would fold a loud take
            // inside out.
            m.write(0, 0, 8, 1.0f);
            CHECK_MSG(feq(m.read(0, 0, 8), 1.0f, 1e-6f), "+1.0 is exactly full scale");
            m.write(0, 0, 9, 2.0f);
            CHECK_MSG(feq(m.read(0, 0, 9), 1.0f, kQ), "an over-scale write clips positive");
            m.write(0, 0, 10, -2.0f);
            CHECK_MSG(m.read(0, 0, 10) < -0.999f && m.read(0, 0, 10) >= -1.001f,
                  "an over-scale write clips negative");

            // add() accumulates in the store, so it must read-modify-write.
            m.write(0, 0, 11, 0.25f);
            m.add(0, 0, 11, 0.25f);
            CHECK_MSG(feq(m.read(0, 0, 11), 0.5f, kQ), "i16 add accumulates");

            // Sub-tracks and channels are independent planes.
            m.ensureCommitted(1, 32);
            m.write(1, 1, 7, 0.75f);
            CHECK_MSG(feq(m.read(0, 0, 7), 1.0f, kQ), "sub-track 0 is untouched");
            CHECK_MSG(feq(m.read(1, 0, 7), 0.0f), "channel 0 of sub-track 1 is untouched");
            CHECK_MSG(feq(m.read(1, 1, 7), 0.75f, kQ), "the written plane holds the value");
        }
    }
