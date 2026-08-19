// MarkerLaneTest — chalkwalk::tape::MarkerLane (DESIGN §40.4).
//
// Markers are dumb navigation points. The lane's whole job is: drop (merging a
// near-duplicate rather than stacking), delete by a stable ordinal, stay sorted,
// and answer nearest / next / prev for cueing. It fires nothing — there is no
// method here that could, which is the fence-#1 line expressed as an absence.

#include "LegacyCheck.h"
#include <chalkwalk/tape/MarkerLane.h>

TEST_CASE("marker lane") {
        // ── Drop keeps the lane sorted; ordinals are stable drop order ───────
        {
            chalkwalk::tape::MarkerLane lane;
            const int a = lane.drop(1000.0);   // ordinal 0
            const int b = lane.drop(200.0);    // ordinal 1, but sorts before a
            const int c = lane.drop(500.0);    // ordinal 2

            CHECK_MSG(a == 0 && b == 1 && c == 2, "ordinals are the drop order");
            CHECK_MSG(lane.count() == 3, "three distinct positions, three markers");
            CHECK_MSG(lane.at(0).positionSamples < lane.at(1).positionSamples
                      && lane.at(1).positionSamples < lane.at(2).positionSamples,
                  "the lane is kept sorted by position");
            // The ordinal is stable across the sort — index != ordinal.
            CHECK_MSG(lane.at(0).ordinal == b, "the earliest position is the second drop");
        }

        // ── A near-duplicate drop merges rather than stacks ──────────────────
        {
            chalkwalk::tape::MarkerLane lane;
            const int first = lane.drop(1000.0, 7);
            const int again = lane.drop(1000.4, 9);  // within mergeEps of 1000
            CHECK_MSG(lane.count() == 1, "two markers a fraction apart are one place");
            CHECK_MSG(again == first, "the merge returns the original ordinal");
            CHECK_MSG(lane.at(0).labelId == 9, "and updates the label to the newer drop");
        }

        // ── Delete by ordinal; ordinals are never reused ─────────────────────
        {
            chalkwalk::tape::MarkerLane lane;
            lane.drop(100.0);        // ord 0
            const int mid = lane.drop(200.0);  // ord 1
            lane.drop(300.0);        // ord 2
            CHECK_MSG(lane.remove(mid), "remove reports it deleted one");
            CHECK_MSG(lane.count() == 2, "the lane shrank");
            CHECK_MSG(! lane.remove(mid), "removing it again does nothing");

            const int fresh = lane.drop(250.0);  // ord 3, NOT 1
            CHECK_MSG(fresh == 3, "a new drop never reuses a deleted ordinal");
        }

        // ── nearest / next / prev for cueing ─────────────────────────────────
        {
            chalkwalk::tape::MarkerLane lane;
            lane.drop(1000.0);
            lane.drop(2000.0);
            lane.drop(3000.0);

            CHECK_MSG(lane.nearest(1900.0) == 1, "nearest picks the closest either side");
            CHECK_MSG(lane.nearest(2600.0) == 2, "...on the other side too");

            CHECK_MSG(lane.next(1500.0) == 1, "next = the first marker strictly after");
            CHECK_MSG(lane.next(2000.0) == 2, "strictly after — a marker on the spot does not count");
            CHECK_MSG(lane.next(3500.0) == -1, "no marker after the last");

            CHECK_MSG(lane.prev(2500.0) == 1, "prev = the first marker strictly before");
            CHECK_MSG(lane.prev(1000.0) == -1, "nothing before the first");

            CHECK_MSG(chalkwalk::tape::MarkerLane{}.nearest(0.0) == -1, "an empty lane has no nearest");
        }

        // ── Load restores ordinals so references survive a reload ────────────
        {
            chalkwalk::tape::MarkerLane lane;
            lane.load({ { 500.0, 5, 0 }, { 100.0, 2, 3 } });
            CHECK_MSG(lane.count() == 2, "loaded two markers");
            CHECK_MSG(lane.at(0).positionSamples == 100.0, "load sorts by position");
            CHECK_MSG(lane.at(0).ordinal == 2, "and keeps the saved ordinals");
            const int fresh = lane.drop(900.0);
            CHECK_MSG(fresh == 6, "the next drop continues past the highest loaded ordinal");
        }
    }
