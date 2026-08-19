// DeckTest — chalkwalk::tape::Deck, the state machine behind Record / Loop / Tape (DESIGN §40.1).
//
// The three faces differ in the medium's topology and in what posts the verbs.
// They do NOT differ in arm, punch, overdub, undo, or the quantized edge, and
// this file is where that claim is stated: one table of transitions, ported
// one-for-one from the shipped looper so the re-seat cannot silently change a
// timing.
//
// The deck decides; the host does. A command returns a DeckEdge describing the
// work (start recording, fold the layer, restore the undo take) and the host —
// which owns the medium and the pool slot — performs it. That split is what lets
// this be a pure test with no plugin, no buffers, and no audio.

#include "LegacyCheck.h"
#include <chalkwalk/tape/Deck.h>

    namespace
    {
        chalkwalk::tape::TransportSnapshot rolling(double quantPeriod)
        {
            chalkwalk::tape::TransportSnapshot t;
            t.running = true;
            t.launchQuantPeriodSamples = quantPeriod;
            return t;
        }
    }
TEST_CASE("deck") {
        constexpr bool kTake = true;    // the medium holds a recorded take
        constexpr bool kEmpty = false;

        // ── Unquantized: verbs fire the instant they are pressed ─────────────
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(0.0);  // Free / Instant grid

            auto e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK_MSG(e.startRecording && d.state() == chalkwalk::tape::DeckState::Recording,
                  "record on an idle deck starts recording now");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kTake);
            CHECK_MSG(e.closeRecording && d.state() == chalkwalk::tape::DeckState::Playing,
                  "record again closes the take and plays it");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kTake);
            CHECK_MSG(e.beginOverdub && d.state() == chalkwalk::tape::DeckState::Overdubbing,
                  "record over a playing take opens an overdub layer");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kTake);
            CHECK_MSG(e.endOverdub && d.state() == chalkwalk::tape::DeckState::Playing,
                  "and again folds it down");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kTake);
            CHECK_MSG(! e.any() && d.state() == chalkwalk::tape::DeckState::Stopped, "play/stop stops");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kTake);
            CHECK_MSG(e.restartPlayback && d.state() == chalkwalk::tape::DeckState::Playing,
                  "and starts the take again from its head");
        }

        // ── A deck with nothing on it cannot play ────────────────────────────
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(0.0);
            d.setState(chalkwalk::tape::DeckState::Stopped);
            const auto e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kEmpty);
            CHECK_MSG(! e.restartPlayback && d.state() == chalkwalk::tape::DeckState::Stopped,
                  "play on an empty stopped deck does nothing");
        }

        // ── Quantized: the edge waits for the grid ───────────────────────────
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(22050.0);  // half a second of grid

            auto e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK_MSG(! e.any() && d.state() == chalkwalk::tape::DeckState::Armed && d.pendingEdge(),
                  "record arms rather than starting");

            e = d.firePending();
            CHECK_MSG(e.startRecording && d.state() == chalkwalk::tape::DeckState::Recording && ! d.pendingEdge(),
                  "the grid fires it");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kTake);
            CHECK_MSG(! e.any() && d.state() == chalkwalk::tape::DeckState::Recording && d.pendingEdge(),
                  "punch-out arms for the next boundary rather than cutting");

            e = d.firePending();
            CHECK_MSG(e.closeRecording && d.state() == chalkwalk::tape::DeckState::Playing,
                  "and closes on the boundary");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kTake);
            CHECK_MSG(! e.any() && d.state() == chalkwalk::tape::DeckState::Playing && d.pendingEdge(),
                  "stop is quantized too — the take keeps playing until the bar");
            e = d.firePending();
            CHECK_MSG(d.state() == chalkwalk::tape::DeckState::Stopped, "then stops");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kTake);
            CHECK_MSG(! e.any() && d.pendingEdge(), "re-play arms");
            e = d.firePending();
            CHECK_MSG(e.restartPlayback && d.state() == chalkwalk::tape::DeckState::Playing, "and fires on the bar");
        }

        // ── A stopped transport can still arm ────────────────────────────────
        // Arming while the transport is parked is how a take begins on the roll.
        // But nothing else quantizes against a grid that is not moving.
        {
            chalkwalk::tape::Deck d;
            auto t = rolling(22050.0);
            t.running = false;

            auto e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK_MSG(d.state() == chalkwalk::tape::DeckState::Armed, "record arms even with the transport stopped");

            d.setState(chalkwalk::tape::DeckState::Playing);
            d.cancelPending();
            e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kTake);
            CHECK_MSG(d.state() == chalkwalk::tape::DeckState::Stopped && ! d.pendingEdge(),
                  "but a stop does not wait for a grid that is not moving");
        }

        // ── The double-tap override (§25) ────────────────────────────────────
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(22050.0);

            d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kEmpty);  // → Armed
            auto e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, true, t, kEmpty);
            CHECK_MSG(e.startRecording && d.state() == chalkwalk::tape::DeckState::Recording && ! d.pendingEdge(),
                  "a double-tap on an armed deck starts it now");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, true, t, kTake);
            CHECK_MSG(e.closeRecording && d.state() == chalkwalk::tape::DeckState::Playing,
                  "and a double-tap punch-out cuts now, not on the bar");
        }

        // ── Cancelling an arm ────────────────────────────────────────────────
        // A single tap on an armed deck backs out. Where it lands depends on
        // whether there is a take to go back to.
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(22050.0);

            d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kEmpty);
            auto e = d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK_MSG(! e.any() && d.state() == chalkwalk::tape::DeckState::Idle && ! d.pendingEdge(),
                  "cancelling an arm on an empty deck returns it to idle");

            d.setState(chalkwalk::tape::DeckState::Stopped);
            d.applyCommand(chalkwalk::tape::DeckCmd::RecordCycle, false, t, kTake);
            CHECK_MSG(d.state() == chalkwalk::tape::DeckState::Armed, "arming over a take");
            e = d.applyCommand(chalkwalk::tape::DeckCmd::PlayStop, false, t, kTake);
            CHECK_MSG(d.state() == chalkwalk::tape::DeckState::Stopped && ! d.pendingEdge(),
                  "play/stop cancels the arm and leaves the take alone");
        }

        // ── Clear and undo ───────────────────────────────────────────────────
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(22050.0);
            d.setState(chalkwalk::tape::DeckState::Overdubbing);

            auto e = d.applyCommand(chalkwalk::tape::DeckCmd::Undo, false, t, kTake);
            CHECK_MSG(e.undo && d.state() == chalkwalk::tape::DeckState::Playing,
                  "undo drops the layer and leaves the take playing");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::Clear, false, t, kTake);
            CHECK_MSG(e.clear && d.state() == chalkwalk::tape::DeckState::Idle && ! d.pendingEdge(),
                  "clear empties the deck and cancels any pending edge");

            e = d.applyCommand(chalkwalk::tape::DeckCmd::Undo, false, t, kEmpty);
            CHECK_MSG(! e.any(), "there is nothing to undo on an empty deck");
        }

        // ── Halve / Double only make sense with a take, and not while recording ─
        {
            chalkwalk::tape::Deck d;
            const auto t = rolling(0.0);
            d.setState(chalkwalk::tape::DeckState::Recording);
            CHECK_MSG(! d.applyCommand(chalkwalk::tape::DeckCmd::Halve, false, t, kTake).halve,
                  "the loop window is not resized mid-record");

            d.setState(chalkwalk::tape::DeckState::Playing);
            CHECK_MSG(d.applyCommand(chalkwalk::tape::DeckCmd::Halve, false, t, kTake).halve, "halve on a take");
            CHECK_MSG(d.applyCommand(chalkwalk::tape::DeckCmd::Double, false, t, kTake).doubleLen, "double on a take");
            CHECK_MSG(! d.applyCommand(chalkwalk::tape::DeckCmd::Double, false, t, kEmpty).doubleLen,
                  "and neither on an empty deck");
        }

        // ── Monitoring ───────────────────────────────────────────────────────
        // Auto is state-aware for an insert source: the take replaces the live
        // input once it plays back. A tap source is already audible, so Auto never
        // doubles it. On/Off are absolute.
        {
            using M = chalkwalk::tape::Deck::Monitor;
            CHECK_MSG(chalkwalk::tape::Deck::resolveMonitor(M::Auto, true, chalkwalk::tape::DeckState::Recording),
                  "auto monitors an insert while recording");
            CHECK_MSG(! chalkwalk::tape::Deck::resolveMonitor(M::Auto, true, chalkwalk::tape::DeckState::Playing),
                  "and drops it once the take plays");
            CHECK_MSG(chalkwalk::tape::Deck::resolveMonitor(M::Auto, true, chalkwalk::tape::DeckState::Overdubbing),
                  "but restores it for an overdub");
            CHECK_MSG(! chalkwalk::tape::Deck::resolveMonitor(M::Auto, false, chalkwalk::tape::DeckState::Recording),
                  "auto never monitors a tap — you can already hear it");
            CHECK_MSG(chalkwalk::tape::Deck::resolveMonitor(M::On, false, chalkwalk::tape::DeckState::Playing),
                  "On is absolute");
            CHECK_MSG(! chalkwalk::tape::Deck::resolveMonitor(M::Off, true, chalkwalk::tape::DeckState::Recording),
                  "Off is absolute");
        }

        // ── Sub-tracks: four, defaulting to one ──────────────────────────────
        {
            // §40.11: the core declares no width. A deck is built at the capacity its
            // host asks for — Lockstep's Loop and Tape ask for four.
            chalkwalk::tape::Deck d{ 4 };
            CHECK_MSG(d.subTrackCapacity() == 4, "a deck is built at the host's capacity");
            CHECK_MSG(d.subTrackCount() == 1, "and still defaults to one sub-track in use");
            CHECK_MSG(d.subTrack(0).armed, "which is armed");
            // §40.3: only sub 0 is armed by default — the rest are disarmed so a
            // multi-sub overdub targets sub 0 alone until the console arms others.
            CHECK_MSG(! d.subTrack(1).armed && ! d.subTrack(2).armed && ! d.subTrack(3).armed,
                  "sub-tracks 1-3 are disarmed by default");
            d.setSubTrackCount(9);
            CHECK_MSG(d.subTrackCount() == 4, "and never exceeds its capacity");
            d.setSubTrackCount(0);
            CHECK_MSG(d.subTrackCount() == 1, "nor drops below one");
        }

        // §40.11: capacity is the host's to choose, and Record's choice is ONE — the
        // linear 1-track face is a capacity-1 deck, not a 4-deck with three unused
        // subs. A wider host (the mixer variant) is the same code at a bigger number.
        {
            chalkwalk::tape::Deck one;  // the default: Record's shape
            CHECK_MSG(one.subTrackCapacity() == 1, "a default deck is a 1-track deck");
            one.setSubTrackCount(4);
            CHECK_MSG(one.subTrackCount() == 1, "which cannot be widened from process()");
            // Out-of-range subs clamp rather than run off the end — under the old
            // fixed array this was unreachable; with runtime capacity it is not.
            CHECK_MSG(one.subTrack(3).armed, "an out-of-range sub clamps into the deck");

            chalkwalk::tape::Deck wide{ 24 };
            CHECK_MSG(wide.subTrackCapacity() == 24, "and a mixer-width deck just works");
            wide.setSubTrackCount(24);
            CHECK_MSG(wide.subTrackCount() == 24, "all 24 usable");
            CHECK_MSG(! wide.subTrack(23).armed, "with only sub 0 armed by default");
        }
    }
