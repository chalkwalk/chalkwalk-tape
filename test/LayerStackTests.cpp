// LayerStackTest — chalkwalk::tape::LayerStack (DESIGN §40.3).
//
// A take is committed content plus at most one fresh layer. Everything the deck
// calls overdub, undo, decay and punch is a way of folding that layer down:
//
//   * overdub  — an unbounded layer, folded with decay 1.
//   * decay    — the same fold, attenuating what was there.
//   * punch    — a layer gated to a span. Outside the span the committed take is
//                not read, not written, and not decayed, which is why punching
//                out is never destructive.
//   * undo     — the committed take as it stood when the layer opened.
//
// The stack owns no storage; three media of the same geometry are bound to it.

#include "LegacyCheck.h"
#include <chalkwalk/tape/LayerStack.h>

#include <vector>

    namespace
    {
        struct Bench
        {
            chalkwalk::tape::Medium::Config cfg;
            std::vector<float> a, b, c;
            chalkwalk::tape::Medium committed, layer, undo;
            chalkwalk::tape::LayerStack stack;

            explicit Bench(int cap)
            {
                cfg.topology = chalkwalk::tape::Topology::Circular;
                cfg.numSubTracks = 2;
                cfg.channelsPerSubTrack = 1;
                cfg.capacitySamples = cap;

                const std::size_t n = chalkwalk::tape::Medium::storageSamples(cfg);
                a.assign(n, 0.0f);
                b.assign(n, 0.0f);
                c.assign(n, 0.0f);
                committed.bind(cfg, chalkwalk::tape::Store{a.data(), a.size()});
                layer.bind(cfg, chalkwalk::tape::Store{b.data(), b.size()});
                undo.bind(cfg, chalkwalk::tape::Store{c.data(), c.size()});
                stack.bind(&committed, &layer, &undo);
            }

            // Lay `v` across the whole of a medium's sub-track.
            static void fill(chalkwalk::tape::Medium& m, int sub, float v)
            {
                m.ensureCommitted(sub, m.capacity());
                for (int k = 0; k < m.capacity(); ++k) m.write(sub, 0, k, v);
            }
        };
    }
TEST_CASE("layer stack") {
        // ── Overdub: a layer folds down onto the committed take ──────────────
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 0.5f);

            CHECK_MSG(! t.stack.layerActive(), "a fresh stack has no open layer");
            CHECK_MSG(! t.stack.canUndo(), "and nothing to undo");

            t.stack.beginLayer(0);
            CHECK_MSG(t.stack.layerActive() && t.stack.canUndo(),
                  "opening a layer snapshots the take");

            Bench::fill(t.layer, 0, 0.25f);
            CHECK_MSG(feq(t.stack.read(0, 0, 3), 0.75f),
                  "an open layer is audible over the committed take");

            t.stack.fold(0, 1.0f);
            CHECK_MSG(! t.stack.layerActive(), "folding closes the layer");
            CHECK_MSG(feq(t.committed.read(0, 0, 3), 0.75f), "and lands it in the take");
            CHECK_MSG(feq(t.layer.read(0, 0, 3), 0.0f), "leaving the layer clear for the next pass");
        }

        // ── Decay: the fold attenuates what was there ────────────────────────
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 1.0f);
            t.stack.beginLayer(0);
            Bench::fill(t.layer, 0, 0.5f);
            t.stack.fold(0, 0.5f);
            CHECK_MSG(feq(t.committed.read(0, 0, 3), 1.0f),
                  "committed * decay + layer — a loop that forgets");
        }

        // ── Discard: the take never saw it ───────────────────────────────────
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 0.5f);
            t.stack.beginLayer(0);
            Bench::fill(t.layer, 0, 0.25f);
            t.stack.discardLayer(0);
            CHECK_MSG(! t.stack.layerActive() && feq(t.committed.read(0, 0, 3), 0.5f),
                  "a discarded layer leaves the take untouched");
        }

        // ── Undo: one level, the take as it stood when the layer opened ──────
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 0.5f);

            t.stack.beginLayer(0);
            Bench::fill(t.layer, 0, 0.25f);
            t.stack.fold(0, 1.0f);
            CHECK_MSG(feq(t.committed.read(0, 0, 3), 0.75f), "the pass landed");

            t.stack.undoLast(0);
            CHECK_MSG(feq(t.committed.read(0, 0, 3), 0.5f), "undo restores the previous take");
            CHECK_MSG(! t.stack.canUndo(), "one level deep: the restored take is not itself undoable");

            // Undoing mid-pass undoes the pass.
            t.stack.beginLayer(0);
            Bench::fill(t.layer, 0, 0.25f);
            t.stack.undoLast(0);
            CHECK_MSG(! t.stack.layerActive() && feq(t.committed.read(0, 0, 3), 0.5f),
                  "undo mid-layer discards the layer and restores the take");
        }

        // ── Punch: a layer gated to a span ───────────────────────────────────
        // Outside the span nothing is read, written, or decayed. That is what
        // makes punch-out non-destructive, and undo's job possible.
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 1.0f);

            chalkwalk::tape::LayerStack::Span span;
            span.begin = 4;
            span.end = 8;
            span.bounded = true;
            t.stack.beginLayer(0, span);

            Bench::fill(t.layer, 0, 0.5f);  // the layer holds signal everywhere
            CHECK_MSG(feq(t.stack.read(0, 0, 5), 1.5f), "inside the span the layer is audible");
            CHECK_MSG(feq(t.stack.read(0, 0, 1), 1.0f), "outside it, only the committed take is");

            t.stack.fold(0, 0.0f);  // full replace inside the span
            CHECK_MSG(feq(t.committed.read(0, 0, 5), 0.5f), "the span holds the punched take");
            CHECK_MSG(feq(t.committed.read(0, 0, 3), 1.0f), "before the span, the take survives");
            CHECK_MSG(feq(t.committed.read(0, 0, 8), 1.0f), "after it, likewise (end is exclusive)");

            t.stack.undoLast(0);
            CHECK_MSG(feq(t.committed.read(0, 0, 5), 1.0f), "and the punch undoes");
        }

        // ── A punch across the loop seam is an ordinary punch ────────────────
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 1.0f);

            chalkwalk::tape::LayerStack::Span span;
            span.begin = 14;  // wraps: [14,16) + [0,2)
            span.end = 2;
            span.bounded = true;
            t.stack.beginLayer(0, span);
            Bench::fill(t.layer, 0, 0.5f);
            t.stack.fold(0, 0.0f);

            CHECK_MSG(feq(t.committed.read(0, 0, 15), 0.5f) && feq(t.committed.read(0, 0, 1), 0.5f),
                  "a wrapped span punches both sides of the seam");
            CHECK_MSG(feq(t.committed.read(0, 0, 8), 1.0f), "and nothing between");
        }

        // ── Sub-tracks are independent ───────────────────────────────────────
        {
            Bench t{ 16 };
            Bench::fill(t.committed, 0, 1.0f);
            Bench::fill(t.committed, 1, 1.0f);
            t.stack.beginLayer(0);
            Bench::fill(t.layer, 0, 0.5f);
            t.stack.fold(0, 1.0f);
            CHECK_MSG(feq(t.committed.read(0, 0, 3), 1.5f), "the folded sub-track took the layer");
            CHECK_MSG(feq(t.committed.read(1, 0, 3), 1.0f), "its neighbour did not");
        }
    }
