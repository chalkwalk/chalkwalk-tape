// The transport (DESIGN.md section 4.4, SOURCES section 19).
//
// What these hold is that the transport is a MECHANISM: every rate comes out of
// a diameter and the tape speed, and where a disturbance enters decides how much
// of it survives. A model that was two oscillators labelled "wow" and "flutter"
// would pass none of them.

#include "MachineFixtures.h"
#include <chalkwalk/tape/Transport.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

namespace
{
    constexpr double kRate = 48000.0;

    // Peak of tau over a run, in seconds.
    double peakTau(tape::Transport& t, int samples)
    {
        double peak = 0.0;
        for (int i = 0; i < samples; ++i)
            peak = std::max(peak, std::abs(t.next()));
        return peak;
    }
}
TEST_CASE("every rate comes out of a diameter and the tape speed", "[transport]")
{
    // DESIGN.md section 4.4's own table, which used to be prose beside the code
    // rather than something the code produced. A roller turns once per `pi d`,
    // a pack once per `2 pi r`.
    const auto g = fixtures::capstanTransport();
    const double fifteenIps = fixtures::kCapstanSpeed;

    REQUIRE(g.rollerRateHz(g.capstanDiameterMetres, fifteenIps)
            == Approx(20.2).epsilon(0.01));
    REQUIRE(g.rollerRateHz(g.pinchDiameterMetres, fifteenIps)
            == Approx(12.1).epsilon(0.01));
    REQUIRE(g.packRateHz(g.fullPackRadiusMetres, fifteenIps)
            == Approx(0.68).epsilon(0.02));
    REQUIRE(g.packRateHz(g.hubRadiusMetres, fifteenIps)
            == Approx(2.34).epsilon(0.02));

    // CAPSTAN AND PINCH LAND IN THE FLUTTER BAND, REELS IN THE WOW BAND, and
    // that separation is not asserted anywhere else. It is the whole reason the
    // traditional two-control split is a description of the symptom.
    REQUIRE(g.packRateHz(g.fullPackRadiusMetres, fifteenIps) < 4.0);
    REQUIRE(g.rollerRateHz(g.capstanDiameterMetres, fifteenIps) > 4.0);
}

TEST_CASE("every rate scales with tape speed, with no parameter involved",
          "[transport][teeth]")
{
    // The property that makes the speed selector do two jobs at once
    // (PRINCIPLES section 4): halving the speed halves the whole flutter
    // spectrum, exactly as it halves every loss frequency (section 4.3).
    //
    // A model with a "flutter rate" constant would pass every other test in this
    // file and fail this one.
    const auto g = fixtures::capstanTransport();
    const double fast = fixtures::kCapstanSpeed;
    const double slow = fast / 2.0;

    REQUIRE(g.rollerRateHz(g.capstanDiameterMetres, slow)
            == Approx(0.5 * g.rollerRateHz(g.capstanDiameterMetres, fast)));
    REQUIRE(g.packRateHz(g.hubRadiusMetres, slow)
            == Approx(0.5 * g.packRateHz(g.hubRadiusMetres, fast)));

    // AND THE ONE THAT DOES NOT. Motor ripple is at twice the line frequency
    // because torque follows the square of an alternating current, so it comes
    // from the mains and not from the tape path (SOURCES section 19). If a
    // refactor ever made it scale with speed it would have made it a tape-path
    // component, which it is not.
    REQUIRE(g.lineFrequencyHz == Approx(50.0));
}

TEST_CASE("the reel packs slide in opposite directions across a take",
          "[transport][teeth]")
{
    // Tape leaves the supply and arrives at the take-up, so the supply's wow
    // RISES in pitch across a take while the take-up's FALLS, crossing in the
    // middle. Nothing dials this: the radii come from how much tape has passed,
    // which the medium already knows because position is the tape.
    //
    // A model that gave both reels one rate would be a model of neither.
    const auto geometry = fixtures::capstanTransport();
    tape::Transport t;
    t.prepare(geometry, kRate, 0x1234);
    t.setSpeed(fixtures::kCapstanSpeed);

    const double capacity = geometry.capacityMetres();
    REQUIRE(capacity > 500.0);   // a seven-inch reel is hundreds of metres

    t.setTapePosition(0.0);
    const double supplyStart = t.supplyRateHz();
    const double takeupStart = t.takeupRateHz();

    t.setTapePosition(capacity);
    const double supplyEnd = t.supplyRateHz();
    const double takeupEnd = t.takeupRateHz();

    INFO("supply " << supplyStart << " -> " << supplyEnd
         << " Hz, take-up " << takeupStart << " -> " << takeupEnd << " Hz");

    REQUIRE(supplyEnd > supplyStart * 2.0);   // the supply pack empties, so faster
    REQUIRE(takeupEnd < takeupStart * 0.5);   // the take-up fills, so slower

    // They swap, and the sweep is the 3.4:1 that section 4.4 quotes between a
    // full seven-inch pack and its hub.
    REQUIRE(supplyEnd / supplyStart == Approx(3.42).epsilon(0.05));
}

TEST_CASE("the flywheel filters what is upstream of it and nothing else",
          "[transport][teeth]")
{
    // SOURCES section 19, and the reason the transport is a filter rather than a
    // sum of disturbances. Motor ripple enters BEFORE the flywheel and is
    // attenuated at 12 dB/octave; capstan runout enters AFTER it and is not
    // attenuated at all.
    //
    // Asserted through tau, at equal depth, so what is being compared is the
    // path and not the number.
    const auto geometry = fixtures::capstanTransport();
    const auto& g = geometry;

    tape::Transport::Depths ripple;
    ripple.motorRipple = 0.01;
    tape::Transport::Depths runout;
    runout.capstanRunout = 0.01;

    tape::Transport a, b;
    a.prepare(g, kRate, 7);
    b.prepare(g, kRate, 7);
    a.setSpeed(fixtures::kCapstanSpeed);
    b.setSpeed(fixtures::kCapstanSpeed);
    a.setTapePosition(100.0);
    b.setTapePosition(100.0);
    a.setDepths(ripple);
    b.setDepths(runout);

    const double rippleTau = peakTau(a, static_cast<int>(kRate));
    const double runoutTau = peakTau(b, static_cast<int>(kRate));
    INFO("ripple tau " << rippleTau << " s, runout tau " << runoutTau << " s");

    // The filter is real: at twice a 50 Hz line the ripple is above a 25 Hz
    // corner and comes down.
    REQUIRE(g.flywheelCornerHz < 2.0 * g.lineFrequencyHz);
    REQUIRE(a.flywheel().magnitudeAt(2.0 * g.lineFrequencyHz, kRate) < 0.2);

    // And the capstan is downstream, so it is not touched. The two also differ
    // by the 1/(2 pi f) that turns a speed error into a timing offset -- 100 Hz
    // against 20 Hz -- so this margin is large and both effects push the same
    // way, which is the point: a filtered high-rate disturbance is doubly quiet.
    REQUIRE(runoutTau > rippleTau * 10.0);
}

TEST_CASE("scrape flutter is a resonance, and the span is what sets it",
          "[transport][teeth]")
{
    // SOURCES section 19: it is a LONGITUDINAL standing wave in the tape, so
    // `f = c / 2L` over the span between ROTATING guides -- not a transverse
    // string resonance, which would go as sqrt(T/mu) and be set by TENSION.
    //
    // The distinction is load-bearing: a model that moved this frequency with
    // tape tension would be modelling the wrong wave, and would still produce a
    // plausible noise.
    auto g = fixtures::capstanTransport();
    REQUIRE(g.scrapeResonanceHz() == Approx(10000.0).epsilon(0.01));

    // Werner's cure, and it falls out: an idler halves the span and doubles the
    // frequency, out of the band and into where the tape's own losses damp it.
    const double longSpan = g.scrapeResonanceHz();
    g.unsupportedSpanMetres *= 2.0;
    REQUIRE(g.scrapeResonanceHz() == Approx(0.5 * longSpan));

    // The formula itself, pinned. `c` is sqrt(E/rho) for the base film, and
    // SOURCES section 19 cross-checks 1,700 m/s two ways: Werner's measured
    // 675-860 Hz on 1 m lengths, and polyester's own E and rho.
    REQUIRE(g.longitudinalWaveSpeedMps == Approx(1700.0));
    REQUIRE(g.scrapeResonanceHz()
            == Approx(g.longitudinalWaveSpeedMps / (2.0 * g.unsupportedSpanMetres)));

    // The structural half of the same claim is that `scrapeResonanceHz` takes no
    // speed argument at all: the wave is in the tape's material, so neither `c`
    // nor the span is a speed, and there is nowhere to pass one in. Every other
    // rate in this file scales with the transport; this one cannot.
}

TEST_CASE("a machine with no sourced transport says so instead of guessing",
          "[transport][teeth]")
{
    // Splice and Slipback have no transport dimensions yet, and the honest
    // reading of that is silence rather than Capstan's numbers applied to a
    // cassette. `modelled()` is what says which, and `next()` returns exactly
    // zero -- so a machine without a sourced mechanism is not quietly given one.
    REQUIRE(fixtures::capstanTransport().modelled());
    REQUIRE_FALSE(fixtures::unmodelledTransport().modelled());

    tape::Transport t;
    t.prepare(fixtures::unmodelledTransport(), kRate, 99);
    t.setSpeed(fixtures::ips(1.875));
    tape::Transport::Depths d;
    d.capstanRunout = d.reelEccentricity = d.scrapeFlutter = 0.5;
    t.setDepths(d);

    for (int i = 0; i < 4096; ++i)
        REQUIRE(t.next() == 0.0);
}

TEST_CASE("the transport is deterministic, and two machines are not in step",
          "[transport][teeth]")
{
    // PRINCIPLES section 5: a project opens identically on any computer, and the
    // transport is part of that. Same seed, same tau, sample for sample -- so
    // the noise-driven part cannot be a `rand()` and the phases cannot come from
    // a clock.
    const auto g = fixtures::capstanTransport();
    tape::Transport::Depths d;
    d.capstanRunout = d.scrapeFlutter = d.reelEccentricity = 0.002;

    auto runOf = [&](std::uint64_t seed)
    {
        tape::Transport t;
        t.prepare(g, kRate, seed);
        t.setSpeed(fixtures::kCapstanSpeed);
        t.setTapePosition(200.0);
        t.setDepths(d);
        std::vector<double> out(8192);
        for (auto& v : out) v = t.next();
        return out;
    };

    const auto first = runOf(0xABCDEF);
    const auto again = runOf(0xABCDEF);
    for (std::size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] == again[i]);

    // And a different machine is a different machine: seeded phases mean two
    // decks are not in step at the top of a take.
    const auto other = runOf(0x123456);
    bool differs = false;
    for (std::size_t i = 0; i < first.size() && ! differs; ++i)
        differs = std::abs(first[i] - other[i]) > 1.0e-12;
    REQUIRE(differs);
}

TEST_CASE("no disturbance moves the tape's average speed", "[transport][teeth]")
{
    // THE INVARIANT THIS WHOLE MODEL RESTS ON, and it is not obvious.
    //
    // `tau` is the integral of a speed error, so a component with any DC in it
    // integrates into a timing offset that grows WITHOUT BOUND -- the tape and
    // the host playhead drift apart forever, and everything addressed by
    // position (cue marks, punch points, the reproduce head's own offset) stops
    // meaning anything. DESIGN.md section 3.1 says the host playhead IS the tape
    // position, so a transport that can move the average is a transport that
    // contradicts the coordinate.
    //
    // The physics says the same thing: THE CAPSTAN SETS THE AVERAGE SPEED, and
    // nothing downstream of it can change the mean. A flat spot, a dry bearing,
    // an eccentric roller -- each is a momentary deviation the capstan's grip
    // pulls back. Real slip does lose distance, but that is absorbed into the
    // machine's speed calibration, not into its flutter.
    //
    // This caught a real one: the pinch flat spot was a ONE-SIDED bump, so it
    // read 83% DC -- a constant 0.667 ms lead with a 0.8 ms wobble on it -- and
    // the leaky integrator was hiding it by turning an unbounded ramp into a
    // plausible-looking constant.
    const auto geometry = fixtures::capstanTransport();

    struct Case { const char* name; tape::Transport::Depths depths; };
    tape::Transport::Depths runout, flat, reel, ripple, scrape, all;
    runout.capstanRunout = 0.002;
    flat.pinchFlatSpot = 0.002;
    reel.reelEccentricity = 0.002;
    ripple.motorRipple = 0.01;
    scrape.scrapeFlutter = 0.006;
    all.capstanRunout = all.pinchFlatSpot = all.reelEccentricity = 0.002;
    all.motorRipple = 0.01;
    all.scrapeFlutter = 0.006;

    const Case cases[] = {
        { "capstan runout", runout }, { "pinch flat spot", flat },
        { "reel eccentricity", reel }, { "motor ripple", ripple },
        { "scrape flutter", scrape }, { "everything at once", all },
    };

    for (const auto& c : cases)
    {
        tape::Transport t;
        t.prepare(geometry, kRate, 0xC0FFEE);
        t.setSpeed(fixtures::kCapstanSpeed);
        t.setDepths(c.depths);

        // Past the leak's own settling, which is a startup transient and not a
        // property of the disturbance.
        const int warmUp = static_cast<int>(20.0 * kRate);
        const int measured = static_cast<int>(20.0 * kRate);
        for (int i = 0; i < warmUp; ++i)
        {
            if (i % 4800 == 0)
                t.setTapePosition(fixtures::kCapstanSpeed * i / kRate);
            (void) t.next();
        }
        double sum = 0.0;
        for (int i = 0; i < measured; ++i)
        {
            if (i % 4800 == 0)
                t.setTapePosition(fixtures::kCapstanSpeed * (warmUp + i) / kRate);
            sum += t.next();
        }
        const double meanMs = 1000.0 * sum / measured;
        INFO(c.name << ": mean tau " << meanMs << " ms over 20 s");

        // ABSOLUTE, not a fraction of the peak: a ratio would punish a component
        // whose peak is legitimately tiny, and what matters is whether the tape
        // has walked. Ten microseconds is 3.8 um of tape at 15 ips -- around one
        // medium sample. The broken flat spot read 0.667 ms, sixty-seven times
        // this, so the margin is not a tuned threshold.
        REQUIRE(std::abs(meanMs) < 0.01);
    }
}
