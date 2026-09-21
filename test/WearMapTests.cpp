// What a reel loses by being played (ROADMAP.md "Wear from passes",
// SOURCES section 23).
//
// SOURCES section 23 sources the MECHANISM and explicitly sources no RATE, so
// these assert SHAPE: monotonic with passes, accelerating rather than linear,
// lubricant an order of magnitude sooner than oxide, the top of the band first,
// and the same tape reading the same in either direction at any speed. A test
// that pinned a level here would be pinning a number nobody published.

#include <chalkwalk/tape/WearMap.h>
#include <chalkwalk/tape/LossEffects.h>
#include "MachineFixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <functional>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

namespace
{
    constexpr double kReelMetres = 100.0;

    // EVEN TAPE, DELIBERATELY. Real stock has a grain and these tests are
    // about the MECHANISMS -- direction, topology, persistence, the seam --
    // each of which has to hold whatever the tape is made of. Leaving the grain
    // in would put a second variable in every one of them and make "fresh tape
    // reads 1.0" a range instead of a number. The grain has tests of its own.
    tape::WearMap freshReel(double metres = kReelMetres, tape::WearConstants k = {},
                            bool circular = false, std::uint64_t seed = 0)
    {
        k.grainDepth = 0.0;
        tape::WearMap map;
        map.prepare(metres, k, circular, seed);
        return map;
    }

    // One whole trip along the tape, which is what "a pass" means.
    void playThrough(tape::WearMap& map, int times = 1)
    {
        for (int i = 0; i < times; ++i)
            map.pass(0.0, map.lengthMetres());
    }

    double dB(double ratio) { return 20.0 * std::log10(ratio); }
}

TEST_CASE("fresh stock is fresh everywhere", "[wear]")
{
    auto map = freshReel();
    REQUIRE(map.numRegions() > 1);
    for (double x = 0.0; x < kReelMetres; x += 1.7)
    {
        REQUIRE(map.at(x).coating == Approx(1.0));
        REQUIRE(map.at(x).lubricant == Approx(1.0));
    }
}

TEST_CASE("a reel is consumed by playing it, and the quoted lifetime closes", "[wear]")
{
    // THE ONE NUMBER THAT IS ALLOWED TO BE PINNED, because it is not a claim
    // about tape: `passesToBare` is an input, and this holds that the shed rate
    // is DERIVED from it rather than tuned until it looked right. Change the
    // constant and the lifetime moves with it; break the derivation and this
    // fails immediately.
    tape::WearConstants k;
    auto map = freshReel(kReelMetres, k);

    playThrough(map, static_cast<int>(0.5 * k.passesToBare));
    const double half = map.at(50.0).coating;
    REQUIRE(half > k.coatingFloor);
    REQUIRE(half < 1.0);

    playThrough(map, static_cast<int>(0.5 * k.passesToBare));
    REQUIRE(map.at(50.0).coating < 0.05);

    // Monotonic and bounded: it never comes back and never goes non-physical.
    playThrough(map, 5000);
    REQUIRE(map.at(50.0).coating == Approx(k.coatingFloor));
}

TEST_CASE("shedding accelerates, which is what gives disintegration a knee", "[wear]")
{
    // A scuffed region sheds faster than a fresh one. Measured with the
    // lubricant held out of it -- two reels at different coatings, each given
    // one pass -- because otherwise the drying would be doing the work and the
    // acceleration would be untested.
    tape::WearConstants k;
    k.passesToDry = 1.0e9;   // effectively lubricated throughout

    auto fresh = freshReel(kReelMetres, k);
    playThrough(fresh);
    const double firstLoss = 1.0 - fresh.at(50.0).coating;

    auto worn = freshReel(kReelMetres, k);
    for (auto& c : worn.coatingMap())
        c = 0.5f;
    playThrough(worn);
    const double lateLoss = 0.5 - worn.at(50.0).coating;

    // 1 + wornSheds * (1 - coating) at coating 0.5.
    REQUIRE(lateLoss / firstLoss == Approx(1.0 + k.wornSheds * 0.5).epsilon(0.02));
    REQUIRE(lateLoss > firstLoss);
}

TEST_CASE("lubricant goes an order of magnitude sooner than the oxide", "[wear]")
{
    // CLIR pub54: lubricant is "partially consumed every time the tape is
    // played". The middle phase is the point of the second slot -- a well-used
    // tape squeals and wears faster BEFORE it sounds any worse.
    tape::WearConstants k;
    auto map = freshReel(kReelMetres, k);

    playThrough(map, static_cast<int>(k.passesToDry));
    const auto s = map.at(50.0);
    REQUIRE(s.lubricant == Approx(0.0).margin(1.0e-6));
    REQUIRE(s.coating > 0.85);

    // Dry tape is higher friction, and friction is what excites scrape flutter.
    // Not exactly, because the same pass that dried it also roughened the
    // surface a little, and roughness drags too (`roughFriction`).
    REQUIRE(map.frictionScale(s) == Approx(k.dryFriction).epsilon(0.02));
    REQUIRE(map.frictionScale({ 1.0, 1.0, 1.0 }) == Approx(1.0));
}

TEST_CASE("hydrolysis collapses the timescale rather than causing the wear", "[wear]")
{
    // Fresh tape lasts hundreds of passes and a hydrolysed reel lasts a
    // handful, which is why Basinski's loops disintegrated during a single
    // transfer -- a transfer of a short loop being many passes rather than one.
    tape::WearConstants k;

    auto fresh = freshReel(kReelMetres, k);
    playThrough(fresh);
    const double fresh1 = 1.0 - fresh.at(50.0).coating;
    REQUIRE(fresh1 < 0.01);

    // ABRASION IS ONLY FIVE TIMES FASTER, and that is the correction.
    // Hydrolysis multiplying the abrasion rate by five hundred did destroy an
    // old reel quickly and destroyed it SMOOTHLY, because abrasion is a
    // continuous process and no multiplier turns it into a knee. The collapse
    // belongs to spallation, which is a different mechanism and can have an
    // inflection.
    //
    // Measured with spallation switched off, because that is the only way to
    // read the abrasion ratio at all once the other mechanism is in play.
    auto quiet = k;
    quiet.spallCapacityPasses = 0.0;
    auto abraded = freshReel(kReelMetres, quiet);
    abraded.setBinderAge(1.0);
    playThrough(abraded);
    const double abrasionOnly = 1.0 - abraded.at(50.0).coating;
    REQUIRE(abrasionOnly / fresh1 == Approx(k.binderCollapse).epsilon(0.01));

    // AND WITH IT ON, THE SAME PASS TAKES ORDERS OF MAGNITUDE MORE. A binder
    // with no capacity left spalls on contact, which is what "Basinski's loops
    // disintegrated during a single transfer" actually describes -- and it is
    // not a faster version of abrasion, it is the other mechanism.
    // READ AS LEVEL, because the two mechanisms write to different maps:
    // abrasion thins the coating and spallation takes the area, and only their
    // product is what a head hears.
    auto old = freshReel(kReelMetres, k);
    old.setBinderAge(1.0);
    playThrough(old);
    const double old1 = 1.0 - tape::WearMap::outputScale(old.at(50.0));
    INFO("one pass at full hydrolysis: " << old1 << " of level lost with "
         << "spallation, " << abrasionOnly << " of coating without");
    REQUIRE(old1 > 100.0 * abrasionOnly);

    // AND IT GOES DOWN GEOMETRICALLY, WHICH IS A CLOSED FORM RATHER THAN A
    // PASS COUNT. A binder with no capacity left spalls on every pass and each
    // event takes one flake out of a region's worth, so what is intact after
    // `n` of them is exactly `(1 - flake/region)^n` -- a tenth taken each time,
    // 0.9^n -- until the floor stops it. Asserting the sequence is what tells
    // "the layer lifts a flake at a time" apart from "the layer vanishes",
    // where a pass count only ever says the second one happened eventually.
    const double floor = old.spallFloor();
    const double kept = 1.0 - k.flakeMetres / k.regionMetres;
    REQUIRE(floor == Approx(1.0 - k.hydrolysisDepth));
    for (int n = 2; n <= 6; ++n)
    {
        playThrough(old);
        INFO("pass " << n << ": expecting " << std::pow(kept, n));
        REQUIRE(old.at(50.0).intact == Approx(std::pow(kept, n)).epsilon(0.001));
    }

    // AND THEN IT ARRESTS, WHICH IS THE OTHER HALF OF THE MECHANISM. Hydrolysis
    // reaches the depth its exposure bought and the binder under that was never
    // wetted, so the layer lifts and the shedding stops -- it does not run on to
    // bare backing. This used to assert `< 0.05`, and that was measurably wrong:
    // dlp 1.1 is 11.5 dB down at pass 409 and still plainly audible, where this
    // engine emptied a reel of any age inside three hundred passes (ROADMAP.md).
    //
    // 0.9^n crosses the floor between the fifteenth event and the sixteenth,
    // so this is DERIVED from the two constants and not a number that was
    // tried: `log(floor) / log(kept)` is 15.3.
    const int toFloor = static_cast<int>(std::ceil(std::log(floor) / std::log(kept)));
    INFO("floor " << floor << " reached at event " << toFloor);
    REQUIRE(toFloor == 16);
    playThrough(old, toFloor - 6);
    const auto& oi = old.intactMap();
    REQUIRE(*std::max_element(oi.begin(), oi.end()) == Approx(floor).epsilon(0.01));
    REQUIRE(*std::min_element(oi.begin(), oi.end()) == Approx(floor).epsilon(0.01));

    // AND IT STAYS THERE. Reaching the floor once and sitting on it are
    // different claims, and the second is the one that matters -- an arrest
    // that leaks is what `kLayerEpsilon` exists to prevent, and a reel that
    // spalls on every pass while being clamped back looks identical to this
    // one for exactly as long as nobody plays it further.
    playThrough(old, 44);
    const auto& settled = old.intactMap();
    REQUIRE(*std::min_element(settled.begin(), settled.end())
            == Approx(floor).epsilon(0.01));

    // Read as level, the reel is down but far from gone -- and BOTH bounds are
    // the test. Without the floor the lower one fails; without spallation at all
    // the upper one does.
    // Against the FLOOR rather than against a number, so this says something
    // when the depth is re-derived: the reel has reached the floor and no
    // further, and seven passes of abrasion have barely touched the coating.
    const double level = tape::WearMap::outputScale(old.at(50.0));
    REQUIRE(level < 1.01 * floor);
    REQUIRE(level > 0.90 * floor);

    // AND NOTHING HERE READS THE MACHINE'S AGE. Binder age is a property of the
    // tape; sitting in a drawer does not remove oxide, playing does.
    auto stored = freshReel(kReelMetres, k);
    stored.setBinderAge(1.0);
    REQUIRE(stored.at(50.0).coating == Approx(1.0));
}

TEST_CASE("a sticky reel sounds bad before anything has shed", "[wear]")
{
    // "Bad surface, full coating" -- Basinski's tape BEFORE it is played. One
    // stored quantity could not express this, which is the argument for the
    // reel-wide term.
    auto map = freshReel();
    const auto head = fixtures::capstanRepro();
    const double clean = map.spacingMetres({ 1.0, 1.0, 1.0 }, head.spacingMetres, head.thicknessMetres);

    map.setBinderAge(1.0);
    const double sticky = map.spacingMetres({ 1.0, 1.0, 1.0 }, head.spacingMetres, head.thicknessMetres);

    REQUIRE(sticky > clean);
    // Reversible: baking restores binder condition, and the oxide it never lost
    // is still there.
    map.setBinderAge(0.0);
    REQUIRE(map.spacingMetres({ 1.0, 1.0, 1.0 }, head.spacingMetres, head.thicknessMetres)
                == Approx(clean));
}

TEST_CASE("wear is addressed in space, so direction and block size do not move it", "[wear]")
{
    // Everything else in this engine is a function of wavelength and reads the
    // same forwards, backwards and at every speed. Wear has to be too, or the
    // same tape is a different tape depending on how it was played.
    const double span = 30.0;

    auto forwards = freshReel();
    forwards.pass(10.0, 10.0 + span);

    auto backwards = freshReel();
    backwards.pass(10.0 + span, 10.0);

    for (std::size_t i = 0; i < forwards.numRegions(); ++i)
        REQUIRE(forwards.coatingMap()[i] == backwards.coatingMap()[i]);

    // The same stretch in a hundred small steps -- which is what a small host
    // block is -- must wear the same tape by the same amount.
    auto chopped = freshReel();
    for (int i = 0; i < 100; ++i)
        chopped.pass(10.0 + span * i / 100.0, 10.0 + span * (i + 1) / 100.0);

    REQUIRE(chopped.at(20.0).coating == Approx(forwards.at(20.0).coating).epsilon(1.0e-3));

    // And tape nobody ran over is untouched: this is a MAP, not a level.
    REQUIRE(forwards.at(70.0).coating == Approx(1.0));
    REQUIRE(forwards.at(20.0).coating < 1.0);
}

TEST_CASE("topology decides how fast a reel dies, and nothing else does", "[wear]")
{
    // The same amount of tape passing the heads: 200 metres of it. On a linear
    // reel that is two passes spread over the whole length; on a four-second
    // loop it is the same stretch over and over. Capstan comes out clean BY
    // CONSTRUCTION rather than by a setting (`fence #2`).
    const double distance = 2.0 * kReelMetres;

    auto reel = freshReel(kReelMetres);
    reel.pass(0.0, distance);

    const double loopMetres = 4.0 * fixtures::ips(15.0);
    auto loop = freshReel(loopMetres);
    for (double x = 0.0; x < distance; x += loopMetres * 0.25)
        loop.pass(x, x + loopMetres * 0.25);

    // Two orders of magnitude, with no constant set anywhere: the reel saw two
    // passes and the loop saw a hundred and thirty.
    const double reelLoss = 1.0 - reel.at(50.0).coating;
    const double loopLoss = 1.0 - loop.at(0.5 * loopMetres).coating;
    REQUIRE(reelLoss > 0.0);
    REQUIRE(loopLoss / reelLoss > 50.0);
    REQUIRE(reel.at(50.0).coating > 0.999);
}

TEST_CASE("wear takes the top off first, and takes the level with it", "[wear]")
{
    // THE MECHANISM IS COATING THICKNESS AND SPACING, so there is no new DSP:
    // both terms are already in the loss chain and both are functions of
    // wavelength. That is why a worn tape goes dull and quiet rather than
    // merely quiet.
    auto map = freshReel();
    const auto head = fixtures::capstanRepro();
    const double speed = fixtures::kCapstanSpeed;

    auto responseAt = [&](double hz, tape::WearMap::State s)
    {
        auto worn = head;
        worn.spacingMetres = map.spacingMetres(s, head.spacingMetres, head.thicknessMetres);
        worn.thicknessMetres = map.thicknessMetres(s, head.thicknessMetres);
        return tape::WearMap::outputScale(s) * tape::playbackResponse(hz, speed, worn);
    };

    // Thickness, intact area, lubricant. `bare` is a THINNED coating that is
    // still all there -- which is abrasion's damage, and the only kind with a
    // wavelength in it. Spallation's is the intact fraction and is broadband.
    const tape::WearMap::State fresh{ 1.0, 1.0, 1.0 };
    const tape::WearMap::State bare{ 0.2, 1.0, 0.0 };

    const double lowLoss  = dB(responseAt(1000.0, bare)  / responseAt(1000.0, fresh));
    const double highLoss = dB(responseAt(10000.0, bare) / responseAt(10000.0, fresh));

    REQUIRE(lowLoss < 0.0);
    REQUIRE(highLoss < lowLoss);   // the top goes first, and harder

    // The broadband term is the one the chain did not have. Without it the
    // NORMALISED thickness loss would make a worn tape relatively BRIGHTER at
    // long wavelengths, which is the wrong story: thin coating is less flux.
    REQUIRE(tape::WearMap::outputScale(bare) == Approx(0.2));
    REQUIRE(map.thicknessMetres(bare, head.thicknessMetres)
                < head.thicknessMetres);
}

TEST_CASE("a reel's condition survives being written down", "[wear]")
{
    // Wear that is lost when the project closes is not permanent, it is a
    // session effect -- and permanence is the whole claim. The bytes are the
    // durable half; where they are kept is not (WearMap.h).
    auto map = freshReel();
    map.pass(10.0, 40.0);
    map.pass(10.0, 15.0);
    map.setBinderAge(0.4);

    const auto blob = tape::serialise(map);

    auto loaded = freshReel();
    REQUIRE(tape::deserialise(loaded, blob.data(), blob.size()));

    REQUIRE(loaded.binderAge() == Approx(0.4).margin(1.0e-4));
    for (double x = 0.0; x < kReelMetres; x += 0.37)
    {
        // Sixteen bits a region: one pass removes about 9e-4 of the coating and
        // a step is 1.5e-5, so the error is a sixtieth of the smallest thing
        // that can happen.
        REQUIRE(loaded.at(x).coating == Approx(map.at(x).coating).margin(2.0e-5));
        REQUIRE(loaded.at(x).lubricant == Approx(map.at(x).lubricant).margin(2.0e-5));
    }

    // The heavily-worn stretch really is in there, so this is not a test that a
    // fresh reel round-trips as a fresh reel.
    REQUIRE(loaded.at(12.0).coating < loaded.at(30.0).coating);
    REQUIRE(loaded.at(70.0).coating == Approx(1.0));
}

TEST_CASE("a saved reel goes onto whatever grid it is loaded into", "[wear]")
{
    // WHERE THE MAPS BEING IN METRES PAYS FOR ITSELF. A region is a piece of
    // tape, so a reel saved at one region size is the same tape at another --
    // it is resampled in space, exactly as `at()` reads it.
    tape::WearConstants fine;
    fine.grainDepth = 0.0;
    fine.regionMetres = 0.005;
    tape::WearMap saved;
    saved.prepare(kReelMetres, fine);
    saved.pass(20.0, 50.0);
    const auto blob = tape::serialise(saved);

    tape::WearConstants coarse;
    coarse.grainDepth = 0.0;
    coarse.regionMetres = 0.020;
    tape::WearMap loaded;
    loaded.prepare(kReelMetres, coarse);
    REQUIRE(loaded.numRegions() * 4 == saved.numRegions());
    REQUIRE(tape::deserialise(loaded, blob.data(), blob.size()));

    for (double x = 22.0; x < 48.0; x += 1.3)
        REQUIRE(loaded.at(x).coating == Approx(saved.at(x).coating).epsilon(1.0e-3));
    REQUIRE(loaded.at(80.0).coating == Approx(1.0));

    // A LONGER REEL COMES BACK PART FRESH, which is the honest reading: the
    // tape past what was saved is tape nobody has played.
    tape::WearMap longer;
    longer.prepare(2.0 * kReelMetres, fine);
    REQUIRE(tape::deserialise(longer, blob.data(), blob.size()));
    REQUIRE(longer.at(30.0).coating < 1.0);
    REQUIRE(longer.at(150.0).coating == Approx(1.0));
}

TEST_CASE("a blob that is not ours is refused rather than believed", "[wear][teeth]")
{
    auto map = freshReel();
    map.pass(0.0, kReelMetres);
    const double worn = map.at(50.0).coating;
    REQUIRE(worn < 1.0);

    auto blob = tape::serialise(map);
    REQUIRE_FALSE(tape::deserialise(map, nullptr, 0));
    REQUIRE_FALSE(tape::deserialise(map, blob.data(), 8));

    auto wrongMagic = blob;
    wrongMagic[0] = 0;
    REQUIRE_FALSE(tape::deserialise(map, wrongMagic.data(), wrongMagic.size()));

    auto wrongVersion = blob;
    wrongVersion[4] = 99;
    REQUIRE_FALSE(tape::deserialise(map, wrongVersion.data(), wrongVersion.size()));

    auto truncated = blob;
    truncated.resize(blob.size() / 2);
    REQUIRE_FALSE(tape::deserialise(map, truncated.data(), truncated.size()));

    // Refused means UNTOUCHED. A half-read reel would be a reel with a lie in
    // it, and this is the map a session is still playing.
    REQUIRE(map.at(50.0).coating == Approx(worn));
}

TEST_CASE("the bytes are the same bytes on any computer", "[wear]")
{
    // PRINCIPLES section 5: a project opens identically anywhere. Every field is
    // a fixed little-endian integer, including the doubles, which are carried
    // as scaled integers rather than as a memcpy of an IEEE bit pattern.
    tape::WearMap map;
    tape::WearConstants k;
    k.regionMetres = 0.01;
    map.prepare(1.0, k);
    map.setBinderAge(0.5);

    const auto blob = tape::serialise(map);
    REQUIRE(blob.size() == 38 + 6 * map.numRegions());

    // "RMWR", little-endian, then version 1.
    REQUIRE(blob[0] == 'R');
    REQUIRE(blob[1] == 'M');
    REQUIRE(blob[2] == 'W');
    REQUIRE(blob[3] == 'R');
    REQUIRE(blob[4] == 3);
    REQUIRE(tape::wear::getU32(blob.data() + 8) == map.numRegions());
    REQUIRE(tape::wear::getU16(blob.data() + 28) == 32768);
}

TEST_CASE("a reel can be loaded with passes already on it", "[wear]")
{
    // A DIAL NEEDS A VALUE, NOT A SIMULATION. Nobody is going to wait while a
    // knob plays a tape two hundred times, so the evolution is inverted in
    // closed form -- and the two have to agree, or the dial is a second model.
    tape::WearConstants k;
    auto map = freshReel(kReelMetres, k);

    REQUIRE(map.coatingAfter(0.0) == Approx(1.0));
    REQUIRE(map.coatingAfter(k.passesToBare) == Approx(k.coatingFloor).epsilon(0.01));
    REQUIRE(map.lubricantAfter(k.passesToDry) == Approx(0.0));

    // Against the real thing. They differ by the lubricated head of the reel's
    // life -- the first forty passes of five hundred run slower than the rate
    // `passesToBare` is calibrated at -- and that gap is what this pins.
    auto played = freshReel(kReelMetres, k);
    playThrough(played, 200);
    const double simulated = played.at(50.0).coating;
    const double closed = map.coatingAfter(200.0);
    REQUIRE(closed < simulated);                       // the closed form is the dry case
    REQUIRE(closed == Approx(simulated).epsilon(0.1));

    map.loadReel(200.0);
    for (double x = 0.0; x < kReelMetres; x += 7.3)
    {
        REQUIRE(map.at(x).coating == Approx(closed));
        REQUIRE(map.at(x).lubricant == Approx(0.0));
    }

    // Loading is not wearing: a fresher reel really is fresher, in both
    // directions, because you put a different tape on.
    map.loadReel(10.0);
    REQUIRE(map.at(50.0).coating > closed);
    REQUIRE(map.at(50.0).lubricant > 0.0);
}

TEST_CASE("a loop has no seam, because the ends of it are neighbours", "[wear][teeth]")
{
    // A LOOP RE-PASSES THE SAME TAPE, so anything systematic about the join
    // arrives once per lap -- which is what a periodic jump is. Two things were
    // wrong and both are here:
    //
    //   1. `ceil` left the last region PARTIAL, so the walk's boundary and the
    //      tape's end disagreed and spans crossing the join were charged to the
    //      wrong region. The grid divides the tape exactly now.
    //   2. `at()` clamped at the ends, holding the value flat across the last
    //      half-region and stepping at the join.
    tape::WearConstants k;                 // 10 mm regions
    k.grainDepth = 0.0;                    // even tape: the seam is the subject
    const double loop = 0.476;             // Splice: 10 s at 1.875 ips
    tape::WearMap map;
    map.prepare(loop, k, /*circular*/ true);

    // The grid closes: no region covers tape that is not there.
    REQUIRE(map.constants().regionMetres * double(map.numRegions())
                == Approx(map.lengthMetres()));
    REQUIRE(map.constants().regionMetres == Approx(k.regionMetres).epsilon(0.1));

    // Wear it as a transport does: block-sized spans, wrapping, never a whole
    // lap in one call -- which is the case `pass` handles by walking rather
    // than by counting laps, and the case the defect lived in.
    const double step = 0.00406;
    double x = 0.0;
    // Six hundred laps, not two hundred: abrasion is `passesToBare` slow now,
    // and this test needs enough of it to have something to be even about.
    for (int b = 0; b < int(600.0 * loop / step); ++b, x += step)
        map.pass(x, x + step);

    const auto& coating = map.coatingMap();
    const double lowest = *std::min_element(coating.begin(), coating.end());
    const double highest = *std::max_element(coating.begin(), coating.end());
    REQUIRE(lowest < 0.95);                      // it really did wear
    // Two hundred laps over the same tape: nowhere may be systematically
    // fresher. Before the fix the two regions at the join read 0.83 against
    // 0.77 everywhere else, which is 0.6 dB arriving once a lap.
    REQUIRE(highest - lowest < 0.002);

}

TEST_CASE("a loop's map reads across the join, not up to it", "[wear][teeth]")
{
    // The second half, and it needs an UNEVEN map to be visible at all: after
    // even wear a clamped read and a wrapped one give the same number, so a
    // test that only plays the loop cannot see the difference. Region 0 and the
    // last region are adjacent tape, so a ramp around the loop puts a real
    // step at the join for the read to cross.
    tape::WearConstants k;
    k.grainDepth = 0.0;
    const double loop = 0.476;
    tape::WearMap map;
    map.prepare(loop, k, /*circular*/ true);

    auto& coating = map.coatingMap();
    for (std::size_t i = 0; i < coating.size(); ++i)
        coating[i] = static_cast<float>(
            0.5 + 0.4 * double(i) / double(coating.size() - 1));

    // Walk from before the end round to after the start, finely.
    const double region = map.constants().regionMetres;
    double worst = 0.0;
    double previous = map.at(loop - 3.0 * region).coating;
    for (double at = loop - 3.0 * region; at < loop + 3.0 * region; at += 0.0002)
    {
        const double here = map.at(at).coating;
        worst = std::max(worst, std::abs(here - previous));
        previous = here;
    }

    // Across the join the map runs 0.9 down to 0.5 over ONE region, so the
    // steepest legitimate step at this resolution is about 0.008. Clamping
    // instead delivers the whole 0.4 in one sample.
    REQUIRE(worst < 0.02);

    // And the value halfway across the join is halfway between the ends, which
    // a clamped read cannot produce at all.
    REQUIRE(map.at(loop).coating == Approx(0.7).epsilon(0.02));
}

TEST_CASE("a reel's ends are ends, and are not joined to each other", "[wear]")
{
    // The other half of the same decision. A linear reel's start and finish are
    // metres of tape apart on a spool; interpolating between them would make
    // the head of a worn reel read as though the tail had been played.
    tape::WearConstants k;
    k.grainDepth = 0.0;
    tape::WearMap reel;
    reel.prepare(kReelMetres, k, /*circular*/ false);

    for (auto& c : reel.coatingMap())
        c = 1.0f;
    reel.coatingMap().front() = 0.2f;

    // Half a region from the very start, a circular map would already be
    // pulling in the far end. A reel holds its first region's value.
    REQUIRE(reel.at(0.0).coating == Approx(0.2));
    REQUIRE(reel.at(kReelMetres - 0.001).coating == Approx(1.0));
}

TEST_CASE("tape has a grain, and it is manufacture rather than wear", "[wear][teeth]")
{
    // WITHOUT THIS A LOOP FLAKES UNIFORMLY. Every region of a loop is passed
    // the same number of times, so even tape wears evenly: measured on a Splice
    // loop after forty-one laps, the coating ran 8.6% to 10.1% EVERYWHERE --
    // a spatial fade, with all the character in the spectrum and none of it on
    // the tape. What those recordings actually sound like is holes.
    tape::WearConstants k;
    const double loop = 0.476;

    tape::WearMap map;
    map.prepare(loop, k, /*circular*/ true, /*seed*/ 7);

    const auto& grain = map.grainMap();
    REQUIRE(grain.size() == map.numRegions());

    // The field spans its whole range, so `grainDepth` means what it says.
    const double lowest = *std::min_element(grain.begin(), grain.end());
    const double highest = *std::max_element(grain.begin(), grain.end());
    REQUIRE(lowest == Approx(0.0).margin(1.0e-6));
    REQUIRE(highest == Approx(1.0).margin(1.0e-6));

    // NEW TAPE IS NOT PERFECT TAPE, and the worst place is `grainDepth` down.
    const auto& coating = map.coatingMap();
    REQUIRE(*std::max_element(coating.begin(), coating.end()) == Approx(1.0));
    REQUIRE(*std::min_element(coating.begin(), coating.end())
                == Approx(1.0 - k.grainDepth));

    // 1/f, NOT WHITE. White noise at region resolution is a dither; what a
    // coating process leaves is structure at every scale. Neighbours are
    // therefore much more alike than distant regions are.
    double neighbour = 0.0, distant = 0.0;
    const auto n = grain.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        neighbour += std::abs(grain[i] - grain[(i + 1) % n]);
        distant += std::abs(grain[i] - grain[(i + n / 3) % n]);
    }
    REQUIRE(neighbour * 3.0 < distant);

    // PERIODIC, because a loop's ends are adjacent tape. A grain with a seam
    // would put a discontinuity there once per lap -- the same defect the map's
    // own interpolation was fixed for, arriving by a different route.
    REQUIRE(std::abs(grain[n - 1] - grain[0])
                < 3.0 * (neighbour / static_cast<double>(n)));

    // SEEDED, so a reel is the same reel on any computer (`PRINCIPLES §5`).
    tape::WearMap same;
    same.prepare(loop, k, true, 7);
    for (std::size_t i = 0; i < n; ++i)
        REQUIRE(same.grainMap()[i] == grain[i]);

    tape::WearMap other;
    other.prepare(loop, k, true, 8);
    double difference = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        difference += std::abs(other.grainMap()[i] - grain[i]);
    REQUIRE(difference / static_cast<double>(n) > 0.1);
}

TEST_CASE("a loop pulls apart along its grain instead of fading", "[wear][teeth]")
{
    tape::WearConstants k;
    const double loop = 0.476;
    const double step = 0.00406;

    auto laps = [&](tape::WearMap& map, int count)
    {
        double x = 0.0;
        for (int b = 0; b < static_cast<int>(count * loop / step); ++b, x += step)
            map.pass(x, x + step);
    };

    tape::WearMap grained;
    grained.prepare(loop, k, true, 7);
    grained.setBinderAge(16.0 / 40.0);
    laps(grained, 300);

    auto even = k;
    even.grainDepth = 0.0;
    tape::WearMap flat;
    flat.prepare(loop, even, true, 7);
    flat.setBinderAge(16.0 / 40.0);
    laps(flat, 300);

    // THE LEVEL, which is thickness times intact area -- because that is what
    // a head reads and because the two mechanisms write to different maps.
    const auto levelAt = [](const tape::WearMap& m, std::size_t i)
    {
        return static_cast<double>(m.coatingMap()[i]) * m.intactMap()[i];
    };
    const auto bare = [&](const tape::WearMap& m)
    {
        int count = 0;
        for (std::size_t i = 0; i < m.numRegions(); ++i)
            if (levelAt(m, i) < 0.02) ++count;
        return count;
    };
    const auto spread = [&](const tape::WearMap& m)
    {
        double lo = 1.0e9, hi = -1.0e9;
        for (std::size_t i = 0; i < m.numRegions(); ++i)
        {
            const double v = levelAt(m, i);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        return hi - lo;
    };

    INFO("grained: " << bare(grained) << " bare of " << grained.numRegions()
         << ", spread " << spread(grained)
         << " -- even: " << bare(flat) << " bare, spread " << spread(flat));

    // Even tape after the same three hundred laps is uniform to a per cent or
    // two:
    // that is the defect, and it is what this test would fail on if the grain
    // were removed.
    REQUIRE(spread(flat) < 0.05);

    // Grained tape is HOLES: most of a loop still near new while parts of it
    // have lost most of their coating. Asserted as a SPREAD rather than as a
    // count of bare regions, because bare is where the story ends and the
    // interesting state is on the way there.
    // READ AS LEVEL, not as coating. Spallation takes AREA, so a reel it has
    // been through has an even thickness and a ragged intact fraction; asking
    // `coatingMap` about it would find nothing at all.
    // THE SPREAD IS BOUNDED BY THE FLOOR NOW, and that is the point rather than
    // a weakening: a sixteen-year reel can only shed what hydrolysis reached, so
    // the range a grained loop opens up is against the LAYER's depth and not
    // against the whole coating. It was 0.5 when a region could go to nothing.
    REQUIRE(spread(grained) > 0.2);
    REQUIRE(spread(grained) > 20.0 * spread(flat));

    // Patchy means the two REGIMES coexist along one loop: some regions have
    // lost the layer and gone into sound binder, others have not been touched.
    // A uniform reel cannot do that at any depth.
    const auto& gi = grained.intactMap();
    // AT the floor, not below it: three hundred laps is enough for the weakest
    // regions to lift their whole layer and not yet enough to start on sound
    // binder, which is the interesting middle. The comparison is against the
    // float the map stores, because `intact_` is float and the floor is double
    // -- the same boundary that `kLayerEpsilon` exists for.
    REQUIRE(*std::min_element(gi.begin(), gi.end())
                <= static_cast<float>(grained.spallFloor()));
    REQUIRE(*std::max_element(gi.begin(), gi.end()) > 0.85);

    // THE MEAN RATE IS UNCHANGED, and that is the claim -- not that the
    // outcome is. `shedScale` is geometric about 1, so an even reel and a
    // grained one shed at the same average rate and `passesToBare` still means
    // what it says. An additive form would have doubled it, because the field's
    // mean is a half and `1 + shed * 0.5` is not 1; reinstating that is a teeth
    // check, and it is how this was found.
    double logSum = 0.0;
    for (std::size_t i = 0; i < grained.numRegions(); ++i)
        logSum += std::log(grained.shedScale(i));
    REQUIRE(std::exp(logSum / static_cast<double>(grained.numRegions()))
                == Approx(1.0).epsilon(0.02));
    REQUIRE(flat.shedScale(0) == Approx(1.0));

    // AND YET THE GRAINED REEL KEEPS MORE COATING ON AVERAGE while sounding far
    // worse, which is worth stating rather than hiding: shedding ACCELERATES
    // and stops at a floor, so the fast places run out and stop counting while
    // the slow ones are barely touched. Averages are the wrong instrument for a
    // tape with holes in it, which is most of the argument for a map.
    const auto mean = [&](const tape::WearMap& m)
    {
        double acc = 0.0;
        for (std::size_t i = 0; i < m.numRegions(); ++i) acc += levelAt(m, i);
        return acc / static_cast<double>(m.numRegions());
    };
    // AND THE GRAINED REEL NOW LOSES MORE, not less. Under abrasion alone the
    // fast places ran out and stopped counting while the slow ones were barely
    // touched, so a grained reel kept a HIGHER average while sounding worse.
    // Spallation reverses it: the grain decides which region fails first, and
    // failure spreads to its neighbours.
    INFO("mean coating: grained " << mean(grained) << ", even " << mean(flat));
    REQUIRE(mean(grained) < mean(flat));
}

TEST_CASE("the reel's grain travels with the reel", "[wear][teeth]")
{
    // A reel opened tomorrow must go on shedding along the pattern it has
    // already half worn through, so the seed is in the blob -- otherwise the
    // remaining coating would be a map of one grain being eaten by another.
    tape::WearConstants k;
    tape::WearMap map;
    map.prepare(0.476, k, true, 0xfeedfaceull);
    map.pass(0.0, 2.0);

    const auto blob = tape::serialise(map);

    tape::WearMap loaded;
    loaded.prepare(0.476, k, true, 1);          // a different reel entirely
    REQUIRE(loaded.grainMap()[3] != map.grainMap()[3]);
    REQUIRE(tape::deserialise(loaded, blob.data(), blob.size()));

    REQUIRE(loaded.reelSeed() == 0xfeedfaceull);
    for (std::size_t i = 0; i < map.numRegions(); ++i)
        REQUIRE(loaded.grainMap()[i] == map.grainMap()[i]);
}

TEST_CASE("passes decide a young reel and the binder decides an old one",
          "[wear][teeth]")
{
    // TWO REGIMES, AND THEY ARE THE RIGHT WAY ROUND. A fresh reel's coating is
    // near enough uniform that the TRANSPORT decides everything -- which is why
    // the top of a reel wears from being rewound to and why a loop wears evenly.
    // An old reel's does not: hydrolysis attacks the weakest binder first, so
    // age does not merely scale the shed rate, it pulls the reel apart. A badly
    // aged tape flakes in patches on the FIRST play where a new one would need
    // hundreds of passes to show any unevenness at all.
    tape::WearConstants k;
    const double loop = 0.476;
    const double step = 0.00406;

    auto play = [&](double years, int laps)
    {
        tape::WearMap map;
        map.prepare(loop, k, true, 7);
        map.setBinderAge(years / 40.0);
        double x = 0.0;
        for (int b = 0; b < static_cast<int>(laps * loop / step); ++b, x += step)
            map.pass(x, x + step);
        return map;
    };

    // The contrast itself grows with age, and that is where the two regimes
    // come from rather than from a rule about them.
    tape::WearMap probe;
    probe.prepare(loop, k, true, 7);
    REQUIRE(probe.ratioFor(0.0) == Approx(k.grainShedRatio));
    REQUIRE(probe.ratioFor(1.0)
                == Approx(std::pow(k.grainShedRatio, 1.0 + k.grainAgeContrast)));

    // A YOUNG REEL, PLAYED FORTY-ONE TIMES: no holes anywhere, and the worst
    // place is within a fifth of the best.
    const auto young = play(1.0, 41);
    std::vector<double> yl;
    for (std::size_t i = 0; i < young.numRegions(); ++i)
        yl.push_back(static_cast<double>(young.coatingMap()[i]) * young.intactMap()[i]);
    const double youngLow = *std::min_element(yl.begin(), yl.end());
    const double youngHigh = *std::max_element(yl.begin(), yl.end());
    REQUIRE(youngLow > 0.5);
    REQUIRE(youngHigh - youngLow < 0.25);

    // AN OLD REEL, PLAYED FIFTY TIMES: the layer has lifted, where a young reel
    // at forty-one is untouched.
    //
    // THIS SAID "PLAYED ONCE" AND THAT WAS AN OVER-READING OF THE ANECDOTE.
    // "Basinski's loops disintegrated during a single transfer" describes one
    // continuous SESSION -- an hour, and 576 laps of a 6.6 s loop -- not one
    // lap, and the measurement says so: dlp 1.1 takes 337 passes to lose 6.5 dB.
    // Requiring every region to fail on first contact is what forced
    // `spallBinderPower` to 6, and 6 left no fuse for the differential to swing
    // across (ROADMAP.md).
    const auto old = play(32.0, 50);
    std::vector<double> ol;
    for (std::size_t i = 0; i < old.numRegions(); ++i)
        ol.push_back(static_cast<double>(old.coatingMap()[i]) * old.intactMap()[i]);
    const double oldLow = *std::min_element(ol.begin(), ol.end());
    const double oldHigh = *std::max_element(ol.begin(), ol.end());

    INFO("young at 41 laps: " << youngLow << " to " << youngHigh
         << " -- old at 50 laps: " << oldLow << " to " << oldHigh);

    // FIFTY PASSES TAKE A LARGE BITE OUT OF IT, where a young reel needs
    // hundreds to lose as much -- and it takes them PATCHILY, which is the
    // regime difference this test is named for. Hydrolysis attacks the weakest
    // binder first, so an old reel does not fail uniformly: the worst places
    // have lifted their whole layer while the best are still near new.
    //
    // ASSERTED ON THE WORST PLACE, NOT THE BEST. The old form asserted the
    // MAXIMUM had reached the floor, which was only true when every region
    // failed at once -- an artefact of `spallBinderPower` at 6 and of reading
    // "disintegrated during a single transfer" as a single lap. With a fuse in
    // it the reel is patchy, so the max stays near new by construction and it
    // is the min that carries the claim.
    //
    // The floor itself is what catches a chunk size that overshoots it, which
    // the first version of the clamp did -- taking 55% straight through a floor
    // at 55% and making the floor do nothing at all.
    // At or past the floor: by fifty laps the weakest regions have lifted their
    // whole layer and the very worst has started on sound binder. That a chunk
    // cannot OVERSHOOT the floor while the layer lasts is asserted where it can
    // be seen cleanly, in `hydrolysis collapses the timescale`, one pass in.
    // THE FLOOR IS NO LONGER ONE NUMBER, so the old form of this claim has
    // gone. It asserted the WORST region had reached the reel's floor, which
    // was a sensible question only while every region had the same one.
    //
    // AND THE ARITHMETIC RUNS OPPOSITE TO INTUITION. The weakest binder is
    // hydrolysed DEEPEST (`WearMap::spallFloorFor`), so it has the most layer
    // to lift and takes the LONGEST to arrest: measured here the worst region's
    // floor is 0.043 where the reel's mean is 0.36, and after fifty laps it
    // rests at 0.387 -- most of the way down and nowhere near stopping. Under a
    // uniform floor it stopped at 0.36. That is the change, and the reel-level
    // trajectory it has to stay inside is pinned by `a hydrolysed reel ends up
    // degraded, not empty` against dlp 1.1.
    //
    // What this test keeps is the PATCHINESS, which is what it is named for.
    const auto worst = static_cast<std::size_t>(
        std::min_element(ol.begin(), ol.end()) - ol.begin());
    INFO("worst region " << worst << " rests at " << oldLow
         << ", its own floor " << old.spallFloorFor(worst)
         << ", the reel's " << old.spallFloor());
    REQUIRE(old.spallFloorFor(worst) < old.spallFloor());

    // AND THE DEPTH REALLY DOES VARY, which is the whole of the change: a reel
    // whose floors were all equal would read zero here.
    double floorLow = 1.0, floorHigh = 0.0;
    for (std::size_t i = 0; i < old.numRegions(); ++i)
    {
        floorLow = std::min(floorLow, old.spallFloorFor(i));
        floorHigh = std::max(floorHigh, old.spallFloorFor(i));
    }
    INFO("floors span " << floorLow << " to " << floorHigh);
    REQUIRE(floorHigh - floorLow > 0.05);
    REQUIRE(oldHigh > 0.9);                       // and the best is untouched
    REQUIRE(oldHigh - oldLow > 0.5);              // so it is PATCHY

    // Where the young reel is uniform: no region has spalled at all, so its
    // whole spread is abrasion's, and abrasion's is small.
    REQUIRE(youngHigh - youngLow < 0.1);
    REQUIRE(youngLow > 1.4 * oldLow);
}

TEST_CASE("abrasion is gradual and spallation has an inflection", "[wear][teeth]")
{
    // TWO MECHANISMS, AND THE SHAPE IS WHY THERE HAVE TO BE TWO.
    //
    // Measured against dlp 2.1: it holds within a few decibels for seventy
    // passes and then loses forty in thirteen. Accelerating abrasion declines
    // smoothly from the first pass at every age -- no value of `wornSheds` or
    // `binderCollapse` produces an inflection, because a continuous process
    // hurried is still a continuous process.
    //
    // Spallation can, because it is fatigue: stress accumulates against a
    // capacity hydrolysis has eaten away, and when the first region gives it
    // hands stress to its neighbours. That is autocatalytic, so the transition
    // is sharp without a threshold anybody chose.
    tape::WearConstants k;
    const double loop = 0.476, step = 0.00406;

    auto meanByPass = [&](double years, int passes, bool spallation)
    {
        auto c = k;
        if (! spallation) c.spallCapacityPasses = 0.0;   // abrasion alone
        tape::WearMap map;
        map.prepare(loop, c, true, 7);
        map.setBinderAge(years / 40.0);
        std::vector<double> out;
        double x = 0.0;
        for (int p = 0; p < passes; ++p)
        {
            for (int b = 0; b < static_cast<int>(loop / step); ++b, x += step)
                map.pass(x, x + step);
            double acc = 0.0;
            for (std::size_t i = 0; i < map.numRegions(); ++i)
                acc += static_cast<double>(map.coatingMap()[i]) * map.intactMap()[i];
            out.push_back(acc / static_cast<double>(map.numRegions()));
        }
        return out;
    };

    // A YOUNG REEL NEVER SPALLS. Its binder has capacity far beyond the
    // abrasion lifetime, so the two curves are the same curve.
    const auto youngWith = meanByPass(1.0, 90, true);
    const auto youngWithout = meanByPass(1.0, 90, false);
    REQUIRE(youngWith.back() == Approx(youngWithout.back()).epsilon(0.01));
    REQUIRE(youngWith.back() > 0.8);            // and it is barely touched

    // AN OLD REEL DOES, and that is the whole difference between the curves.
    const auto oldWith = meanByPass(16.0, 300, true);
    const auto oldWithout = meanByPass(16.0, 300, false);
    INFO("16-year reel at pass 300: " << oldWith.back() << " with spallation, "
         << oldWithout.back() << " without");
    REQUIRE(oldWithout.back() > 0.6);           // abrasion alone barely marks it
    REQUIRE(oldWith.back() < 0.85);             // spallation has taken the layer
    REQUIRE(oldWith.back() > 0.5);              // and stopped -- it is NOT bare

    // AND THE CURVE HAS AN INFLECTION, measured as the STEEPEST per-pass loss
    // against the loss it started at.
    //
    // Comparing thirds was the obvious instrument and it is the wrong one: with
    // the cascade running properly the reel reaches the floor before the last
    // third begins, so the last third loses nothing and the test reads it as no
    // inflection at all. The peak of the derivative is what an inflection
    // actually is, and it survives the floor.
    const auto steepest = [](const std::vector<double>& v)
    {
        double worst = 0.0;
        for (std::size_t i = 1; i < v.size(); ++i)
            worst = std::max(worst, v[i - 1] - v[i]);
        return worst;
    };
    const auto opening = [](const std::vector<double>& v)
    {
        return std::max(1.0e-9, (v[0] - v[4]) / 4.0);
    };

    const double burst = steepest(oldWith) / opening(oldWith);
    INFO("with spallation the steepest pass loses " << burst
         << " times what the opening passes did");
    REQUIRE(burst > 10.0);

    // The abrasion-only curve does NOT, which is what makes the comparison mean
    // something rather than being a property of any decaying exponential: a
    // continuous process hurried is still a continuous process.
    const double smooth = steepest(oldWithout) / opening(oldWithout);
    INFO("abrasion alone: " << smooth);
    REQUIRE(smooth < 4.0);
}

TEST_CASE("abrasion is the fuse: it barely dulls, and it raises friction",
          "[wear][teeth]")
{
    // THE HYPOTHESIS THE MEASUREMENT LED TO. dlp 2.1 shows no wavelength
    // signature at all for seventy passes -- at the point its level starts to
    // move, the 5-10 kHz band is inside 0.6 dB of the 100-300 Hz band, where
    // this engine at the same loss put the top 3 to 5 dB further down. A
    // thinning coating has a fingerprint and the recording does not carry it.
    //
    // So abrasion does very little to the SIGNAL inside a reel's playable life.
    // What it does is strip the lubricant and roughen the surface, and both of
    // those raise FRICTION -- which fatigues the binder, which is what
    // eventually lets go. The small loss nobody can hear is the fuse for the
    // large one everybody can.
    tape::WearConstants k;
    tape::WearMap map;
    map.prepare(kReelMetres, k);

    // A hundred passes of a fresh reel: the level has barely moved.
    for (int p = 0; p < 100; ++p)
        map.pass(0.0, map.lengthMetres());
    const auto after = map.at(50.0);
    const double lost = 1.0 - tape::WearMap::outputScale(after);
    INFO("a hundred passes cost " << 20.0 * std::log10(1.0 - lost) << " dB");
    REQUIRE(lost < 0.10);                       // under a decibel of level

    // AND YET THE FRICTION HAS RISEN A LOT, which is the point. The lubricant
    // is long gone and the surface is no longer polished.
    const double friction = map.frictionScale(after);
    INFO("friction after a hundred passes: x" << friction);
    REQUIRE(friction > 1.5 * map.frictionScale({ 1.0, 1.0, 1.0 }));

    // ROUGHENING IS PART OF IT, separately from the lubricant. A tape that is
    // thinner but still lubricated still drags more than a new one -- which is
    // the term that carries abrasion's effect through to spallation, and
    // removing it leaves the fuse unlit.
    const double roughOnly = map.frictionScale({ 0.7, 1.0, 1.0 });
    INFO("thinned but fully lubricated: x" << roughOnly);
    REQUIRE(roughOnly > 1.2);
}

TEST_CASE("spallation loses level flat and abrasion takes the top off",
          "[wear][teeth]")
{
    // THE TWO MECHANISMS SOUND DIFFERENT, and this is the assertion that says
    // so -- it is the measurement dlp 2.1 forced, turned into a test.
    //
    //   ABRASION thins the coating, so the surface recedes and spacing grows.
    //   Spacing loss is exp(-k d): wavelength-dependent, and it takes the top
    //   off first.
    //
    //   SPALLATION takes the coating away over an AREA, leaving bare backing
    //   beside full-thickness coating. A head averages flux across the two,
    //   which is a level with no wavelength in it at all.
    tape::WearMap map;
    map.prepare(kReelMetres, {});
    const auto head = fixtures::capstanRepro();
    const double speed = fixtures::kCapstanSpeed;

    auto lossAt = [&](double hz, tape::WearMap::State s)
    {
        auto worn = head;
        worn.spacingMetres = map.spacingMetres(s, head.spacingMetres,
                                               head.thicknessMetres);
        worn.thicknessMetres = map.thicknessMetres(s, head.thicknessMetres);
        const double fresh = tape::playbackResponse(hz, speed, head);
        return 20.0 * std::log10(tape::WearMap::outputScale(s)
                                 * tape::playbackResponse(hz, speed, worn) / fresh);
    };

    // Matched at 1 kHz, so the comparison is about SHAPE and not about depth:
    // abrasion thinned to 0.55, spallation with 0.55 of its area left.
    const tape::WearMap::State thinned{ 0.55, 1.0, 0.0 };
    const tape::WearMap::State flaked{ 1.0, 0.55, 0.0 };

    const double abrasionTilt = lossAt(10000.0, thinned) - lossAt(1000.0, thinned);
    const double spallTilt = lossAt(10000.0, flaked) - lossAt(1000.0, flaked);

    INFO("10 kHz against 1 kHz: abrasion " << abrasionTilt
         << " dB, spallation " << spallTilt << " dB");

    // THE TWO TILT IN OPPOSITE DIRECTIONS, and that is a sharper claim than the
    // `spallTilt == 0` this replaced.
    //
    // Abrasion is a receding, roughening surface, so it is a SPACING increase
    // and it takes short wavelengths first: the top goes down.
    //
    // Spallation thins, and a thinner coating loses the BOTTOM -- a long
    // wavelength reads the whole depth while a short one only ever reads the
    // surface -- so relative to 1 kHz the top comes UP.
    //
    // The old form asserted spallation was purely broadband, which was true
    // only while flakes took full depth and nothing else. dlp 1.1 says
    // otherwise: its differential runs -2.1 dB across the body and +2.6 dB by
    // pass 409, and a mechanism with no wavelength in it cannot produce a sign
    // change at any setting of any constant (`ROADMAP.md`).
    REQUIRE(abrasionTilt < -3.0);
    REQUIRE(spallTilt > 0.5);
    REQUIRE(abrasionTilt < 0.0);
    REQUIRE(spallTilt > 0.0);
}

TEST_CASE("new tape lasts about as long as studio practice says", "[wear][teeth]")
{
    // AN INDEPENDENT CHECK, AND NOTHING WAS FITTED TO IT.
    //
    // The rule of thumb among engineers is that new tape shows audible
    // degradation somewhere in the 200-500 play range and is unusably obtrusive
    // for studio work by about 1000. That is practitioner folklore rather than
    // a published measurement -- it is recorded in `SOURCES §23`'s "what this
    // does NOT source" for exactly that reason -- so it cannot calibrate
    // anything. What it can do is disagree, and it does not.
    //
    // Every constant this exercises came from somewhere else: `passesToBare`
    // from dlp 2.1's missing wavelength signature, `spallCapacityPasses` left
    // at the value it was first written with, and the roughening term added to
    // carry friction rather than to move a lifetime. The fresh-tape lifetime is
    // EMERGENT, and it is the roughening -> friction -> fatigue chain that
    // produces it.
    tape::WearConstants k;
    tape::WearMap map;
    map.prepare(0.476, k, /*circular*/ true, /*seed*/ 7);
    map.setBinderAge(0.0);                       // NEW TAPE

    const double loop = map.lengthMetres(), step = 0.00406;
    double x = 0.0;
    const auto playTo = [&](int target, int& from)
    {
        for (; from < target; ++from)
            for (int b = 0; b < static_cast<int>(loop / step); ++b, x += step)
                map.pass(x, x + step);
    };
    const auto worst = [&]
    {
        double lo = 1.0;
        for (std::size_t i = 0; i < map.numRegions(); ++i)
            lo = std::min(lo, static_cast<double>(map.coatingMap()[i]) * map.intactMap()[i]);
        return 20.0 * std::log10(lo + 1.0e-12);
    };
    const auto flaked = [&]
    {
        int n = 0;
        for (auto a : map.intactMap()) if (a < 0.99f) ++n;
        return n;
    };

    int at = 0;
    playTo(200, at);
    INFO("200 plays: worst region " << worst() << " dB, " << flaked() << " flaked");
    REQUIRE(worst() > -3.0);          // not yet obtrusive
    REQUIRE(flaked() == 0);           // and nothing has let go

    playTo(500, at);
    INFO("500 plays: worst region " << worst() << " dB, " << flaked() << " flaked");
    REQUIRE(worst() < -1.0);          // but no longer pristine either
    REQUIRE(worst() > -8.0);

    playTo(1000, at);
    INFO("1000 plays: worst region " << worst() << " dB, " << flaked() << " flaked");
    REQUIRE(flaked() > map.numRegions() / 2);   // most of it has gone
    REQUIRE(worst() < -20.0);

    // AND A REEL DOES NOT DIE OF ABRASION. If it did, this would be a smooth
    // thinning rather than a loop most of which has flaked -- which is the
    // regime separation the whole two-mechanism model exists for.
    // Measured on the AVERAGE thickness rather than the worst: the weakest
    // region has abraded a long way by a thousand passes, and the claim is
    // about which mechanism took the reel rather than about any one place.
    double thickness = 0.0;
    for (auto c : map.coatingMap()) thickness += c;
    thickness /= static_cast<double>(map.numRegions());
    INFO("the coating itself is still " << 20.0 * std::log10(thickness)
         << " dB on average");
    REQUIRE(thickness > 0.5);
}

TEST_CASE("a hydrolysed reel ends up degraded, not empty", "[wear][teeth]")
{
    // THE OTHER END OF THE CALIBRATION, and the one the model used to fail.
    // `new tape lasts about as long as studio practice says` pins a FRESH reel;
    // this pins an OLD one, and the two are independent domains -- studio
    // practice for the first, a measurement of dlp 1.1 for this.
    //
    // dlp 1.1's loop period is 6.624366 s and its 12-18 kHz floor does not move
    // by more than 1 dB from pass 1 to pass 337, which is proof that no gain was
    // applied over that stretch, which is what makes its level trajectory a
    // measurement of the TAPE rather than of a fader. On that clean window it is
    // 6.5 dB down by pass 337 and 11.5 dB down at pass 409, and audible
    // throughout. `ROADMAP.md` carries the method.
    //
    // THE MACHINE WAS A CONSUMER REVOX, which settles the speed and therefore
    // the loop's LENGTH. The domestic line (A77, B77) runs 3.75 and 7.5 ips;
    // 15 belongs to the semi-pro PR99. At 7.5 ips a 6.624 s loop is 1.259 m --
    // fifty inches, a natural hand-cut loop. At 15 ips it would be eight feet
    // and need a path no domestic deck has, and at 3.75 it would be
    // twenty-five inches, tight around a Revox head block. `Slipback` is
    // already dimensioned from the A77 (`MachineGeometry.h`), so the geometry
    // to measure through is its own.
    tape::WearConstants k;
    tape::WearMap map;
    map.prepare(1.259, k, /*circular*/ true, /*seed*/ 7);
    map.setBinderAge(0.65);

    const double loop = map.lengthMetres(), step = 0.00406;
    double x = 0.0;
    const auto levelDb = [&]
    {
        double mean = 0.0;
        for (std::size_t i = 0; i < map.numRegions(); ++i)
            mean += static_cast<double>(map.coatingMap()[i]) * map.intactMap()[i];
        return 20.0 * std::log10(mean / static_cast<double>(map.numRegions()));
    };

    double at174 = 0.0, at337 = 0.0;
    for (int p = 0; p < 409; ++p)
    {
        for (int b = 0; b < static_cast<int>(loop / step); ++b, x += step)
            map.pass(x, x + step);
        if (p == 173) at174 = levelDb();
        if (p == 336) at337 = levelDb();
    }
    const double db = levelDb();

    // ---- AND IT GETS THERE ALONG THE RECORD'S TRAJECTORY ----
    //
    // THE ENDPOINT ALONE IS NOT ENOUGH, and this is the assertion that says so.
    // A reel can sit nearly untouched for three hundred passes and then fall off
    // a cliff onto exactly the right final number; nothing above would notice.
    // When the spallation constants were re-solved against the endpoint and the
    // roughness alone, 115 of 432 candidate cells passed -- a basin that wide
    // means the anchors were not constraining the shape at all.
    //
    // dlp 1.1 has the whole curve, not just its end: on the same clean window it
    // is 6.26 dB down by pass 174 and 8.30 by pass 337. Bracketed, like the
    // endpoint and for the same reason -- the reel's age is not known -- but two
    // more points is the difference between pinning a trajectory and pinning a
    // destination. `~/Programming/basinski-reference/dlp11_db.npy` is the series.
    INFO("trajectory: " << at174 << " dB by pass 174, " << at337 << " by 337, "
         << db << " at 409 (dlp 1.1: -6.26, -8.30, -13.02)");
    REQUIRE(at174 < -3.0);
    REQUIRE(at174 > -9.0);
    REQUIRE(at337 < -5.5);
    REQUIRE(at337 > -11.0);

    // AND IT ONLY EVER GOES DOWN, so those two are points on a decline rather
    // than samples of something that wanders.
    REQUIRE(at337 < at174);
    REQUIRE(db < at337);

    INFO("409 passes at binder age 0.65: " << db << " dB, floor "
         << map.spallFloor());

    // BRACKETING dlp 1.1's -11.5 dB, not asserting it. The age is not known --
    // no recording says how long that tape sat or in what -- so what is being
    // held here is the ORDER OF MAGNITUDE of the endpoint, which is the thing
    // that was wrong: this engine used to read -41 dB at pass 288 for a reel of
    // ANY age, and a reel that has gone silent cannot be the one on a record
    // where the music is still plainly there.
    REQUIRE(db < -7.0);
    REQUIRE(db > -14.0);

    // AND THE UPPER BOUND IS A SEPARATE MECHANISM'S TEETH. Sticky-shed friction
    // is the hydrolysed SURFACE dragging; once the layer has lifted the head
    // runs on sound binder and the drag goes with it. Charge it to the reel's
    // age forever instead and the reel fatigues at four times the rate, reads
    // -17.3 dB here and is bare by pass 576.

    // THE LAYER IS GONE AND THE REEL HAS CROSSED INTO SOUND BINDER, which is
    // the arrest working rather than the reel merely sitting at the floor.
    //
    // THIS IS THE FLOAT-BOUNDARY TEETH CHECK. `intact_` is float and the floor
    // is double, so a region clamped exactly to the floor reads back fractions
    // above it; without `kLayerEpsilon` it stays in the hydrolysed branch, where
    // its capacity is near zero, so it spalls on EVERY pass and is clamped
    // straight back. The reel then sits at the floor forever looking perfectly
    // arrested -- this assertion is what tells the two apart.
    const auto& intact = map.intactMap();
    REQUIRE(*std::min_element(intact.begin(), intact.end()) < map.spallFloor());

    // ---- THE DIFFERENTIAL, RE-DERIVED ON CLEAN POSITIONS (`SOURCES §32`) ----
    //
    // THIS TEST USED TO CITE A NUMBER THAT IS A MEASUREMENT ARTEFACT. It read:
    // dlp 1.1's 5-10 kHz band is 2.1 dB below 100-300 Hz across the body and
    // 2.6 dB ABOVE it by pass 409, so the differential changes sign. Both
    // figures are WHOLE-LAP averages, and `SOURCES §32` shows they average two
    // populations of opposite sign -- clean frames at about -7.3 dB and dropout
    // interiors at +3 to +9 -- mixing in a proportion that moves with wear
    // because dropouts take over the lap. The sign change is that proportion
    // rising. The positive half is the hiss floor showing through a tape that
    // has lost its top, and the floor does not fall inside a dropout.
    //
    // SO THE MEASUREMENT IS TAKEN WHERE THE RECORDING'S IS NOW TAKEN: at
    // positions the head does not read as a dropout. In this engine that barely
    // moves the number -- the map carries no noise floor, so the artefact that
    // inflated the recording cannot arise here -- but it makes the two
    // comparable, which is the point.
    //
    // A spacing increase (abrasion, dirt) takes short wavelengths first, so it
    // is the negative half. Thinning takes the bottom, because a long
    // wavelength reads the whole depth and a short one only ever reads the
    // surface -- so it is the positive half, and it can only be reached because
    // spallation now thins the coating rather than only taking area. Read only
    // `coating` in `thicknessMetres` and spallation becomes a pure broadband
    // multiplier: the differential cannot go positive at ANY setting of ANY
    // constant, which is what made this a structural change rather than a tune.
    const auto head = fixtures::slipbackRepro();
    const double speed = fixtures::kSlipbackSpeed;

    // CLEAN POSITIONS ONLY. `realisedAt` is what the head delivers, so a
    // position it reads below the dropout threshold is exactly what the
    // recording's analysis excludes. The band energy is then taken from the
    // region's own state, as before.
    const auto bandLevel = [&](const tape::WearMap& m, double hz)
    {
        {
            double acc = 0.0;
            int n = 0;
            for (double x = 0.0; x < m.lengthMetres(); x += m.lengthMetres() / 256.0)
            {
                if (tape::WearMap::outputScale(m.realisedAt(x)) < 0.25)
                    continue;                       // in a dropout: excluded
                ++n;
                const double at = x;
                const auto st = m.at(at);
                auto worn = head;
                worn.spacingMetres = m.spacingMetres(st, head.spacingMetres,
                                                     head.thicknessMetres,
                                                     m.binderAge());
                worn.thicknessMetres = m.thicknessMetres(st, head.thicknessMetres);
                const double g = tape::WearMap::outputScale(st)
                               * tape::playbackResponse(hz, speed, worn);
                acc += g * g;
            }
            return n > 0 ? 10.0 * std::log10(acc / n + 1.0e-30) : 0.0;
        }
    };
    const auto differential = [&](const tape::WearMap& m)
    {
        return bandLevel(m, 7000.0) - bandLevel(m, 200.0);
    };

    // Pass 100 is inside the fuse: nothing has spalled, so all that has
    // happened is a receding, roughening surface -- and the top goes first.
    tape::WearMap body;
    body.prepare(1.259, k, /*circular*/ true, /*seed*/ 7);
    body.setBinderAge(0.65);
    tape::WearMap fresh;
    fresh.prepare(1.259, k, /*circular*/ true, /*seed*/ 7);
    fresh.setBinderAge(0.65);
    const double zero = differential(fresh);

    double z = 0.0;
    for (int p = 0; p < 100; ++p)
        for (int b = 0; b < static_cast<int>(body.lengthMetres() / step); ++b, z += step)
            body.pass(z, z + step);

    const double bodyDiff = differential(body) - zero;
    const double endDiff = differential(map) - zero;

    // The low band on its own, which is what the disagreement below is about:
    // the real reel's bass does not move and this engine's does.
    const double endLow = bandLevel(map, 200.0) - bandLevel(fresh, 200.0);
    const double endHigh = bandLevel(map, 7000.0) - bandLevel(fresh, 7000.0);

    INFO("clean-position differential: body " << bodyDiff << " dB, endpoint "
         << endDiff << " dB; endpoint bands " << endLow << " at 200 Hz and "
         << endHigh << " at 7 kHz");

    REQUIRE(bodyDiff < 0.0);          // the top goes first

    // AND THE SECOND HALF IS A MECHANISM CHECK, NOT A MEASUREMENT ANY MORE.
    //
    // Spallation thins the coating as well as taking area, so late damage
    // reaches the BOTTOM: a long wavelength reads the whole depth and a short
    // one only ever reads the surface. Read only `coating` in
    // `thicknessMetres` and spallation becomes a pure broadband multiplier that
    // cannot move the differential at any setting of any constant -- which is
    // what these two lines protect.
    //
    // THE RECORDING NO LONGER EVIDENCES IT, and that is said rather than
    // quietly kept: the +2.6 dB endpoint this was built to reach is the hiss
    // floor in a whole-lap average (`SOURCES §32`). If the thinning is ever
    // removed on physical grounds, these two assertions go with it and nothing
    // is lost, because there is no measurement asking for them.
    REQUIRE(endDiff > 1.0);
    REQUIRE(endDiff - bodyDiff > 1.5);

    // ---- AND THE DISAGREEMENT, RECORDED WHERE IT CAN BE SEEN ----
    //
    // On CLEAN frames dlp 1.1 loses its treble and NOT its bass: 0.03 dB at
    // 150-300 Hz against 2.43 dB at 5-10 kHz by lap 300, and 0.12 against 8.16
    // by lap 409. That is a pure spacing signature and nothing else.
    //
    // This engine does the opposite. At pass 409 on clean positions it reads
    // -9.03 dB at 200 Hz against -6.97 at 7 kHz: nine decibels of bass loss
    // where the real reel has none. Our clean-position loss is THINNING where
    // the measured one is SPACING, and no setting of the spallation constants
    // changes which of the two it is.
    //
    // What would close it is head contamination -- deposits arriving per pass
    // and read as a spacing increase (`SOURCES §23`, `§30`, and unbuilt in
    // `ROADMAP.md`). Until that exists there is nothing here to assert AGAINST,
    // so the disagreement is reported and not bounded: a number invented to
    // bracket it would be a constant fitted to a gap.
    INFO("clean-position bands at the endpoint: " << endLow << " dB at 200 Hz, "
         << endHigh << " at 7 kHz -- dlp 1.1's clean frames lose 8.16 dB of "
         "treble and 0.12 dB of bass, which is the opposite balance");
    REQUIRE(endLow < 0.0);            // it does lose bass, which is the disagreement
    REQUIRE(endHigh < 0.0);           // and treble, which is not

    // WHAT IS ASSERTED INSTEAD IS THAT "CLEAN" MEANS SOMETHING HERE. Excluding
    // dropouts is only a different measurement if the reel has dropouts to
    // exclude, and this reel must have them by the endpoint or the whole
    // re-derivation is measuring the same positions twice.
    int dropped = 0, total = 0;
    for (double x = 0.0; x < map.lengthMetres(); x += map.lengthMetres() / 256.0)
    {
        ++total;
        if (tape::WearMap::outputScale(map.realisedAt(x)) < 0.25)
            ++dropped;
    }
    INFO("at the endpoint " << dropped << " of " << total
         << " positions read as dropouts and are excluded");
    REQUIRE(dropped > total / 20);        // the exclusion is doing real work
    REQUIRE(dropped < (3 * total) / 4);   // but there is still a reel to measure

    // AND AGAIN AT AN AGE WHOSE FLOOR ROUNDS THE OTHER WAY, because whether the
    // bug bites depends on which side of the double `float(floor)` lands, and a
    // test that only tries one age is testing the rounding rather than the code.
    // 0.6175 rounds UP, which is where this was found: the reel sat at exactly
    // the floor for two thousand passes looking arrested while spalling on every
    // one of them and being clamped straight back.
    tape::WearMap sticky;
    sticky.prepare(0.5, k, /*circular*/ true, /*seed*/ 7);
    sticky.setBinderAge(0.85);
    REQUIRE(sticky.spallFloor() == Approx(1.0 - 0.85 * k.hydrolysisDepth));

    double y = 0.0;
    for (int p = 0; p < 300; ++p)
        for (int b = 0; b < static_cast<int>(sticky.lengthMetres() / step); ++b, y += step)
            sticky.pass(y, y + step);

    const auto& si = sticky.intactMap();
    INFO("age 0.85 after 300 passes: min intact "
         << *std::min_element(si.begin(), si.end()) << ", floor "
         << sticky.spallFloor());
    REQUIRE(*std::min_element(si.begin(), si.end()) < sticky.spallFloor());
}

TEST_CASE("the reel loses level in many small steps, not a few big ones",
          "[wear][teeth]")
{
    // ANCHOR 4, AND IT IS READ OFF THE RECORD RATHER THAN OFF OUR OWN RENDER.
    //
    // THE OBVIOUS FORM OF THIS TEST IS WRONG, and it was written and discarded
    // before this one. Bounding the per-pass drop fails the actual recording:
    // dlp 1.1's biggest single-lap fall is 10.13 dB. Most of that comes back --
    // 273 rises against 304 falls -- because it is the performance and the
    // fader, not the tape. Charge only the part that STAYS (a drop that is not
    // recovered within five laps) and the real reel still steps by 3.01 dB at
    // its worst, with a 99th percentile of 1.97. Our complained-of 1.80 dB step
    // is a p99 event on the real thing. Size does not condemn it.
    //
    // WHAT DOES IS CONCENTRATION. dlp 1.1 loses 34.4 dB to 48 separate
    // irreversible steps by pass 409, and NO SINGLE ONE carries more than 5.4%
    // of that; the largest five together carry 20.6%. The engine's render put
    // 1.80 dB into ONE pass out of an otherwise flat floor -- a staircase with
    // two risers, where the tape is rough everywhere. That is why it is audible
    // as a jump: not its height, but the flatness either side of it.
    //
    // The measure is a RATIO, so it holds whatever total decline the reel
    // reaches and cannot be satisfied by making the reel wear less. The series
    // is at `~/Programming/basinski-reference/dlp11_db.npy`; `ROADMAP.md`
    // carries the method.
    //
    // WHAT IT CATCHES: `spallChunk` taking over half a region's remaining layer
    // in one event while `spallSpread` fires a whole cluster together.
    tape::WearConstants k;
    tape::WearMap map;
    map.prepare(1.259, k, /*circular*/ true, /*seed*/ 7);
    map.setBinderAge(0.65);

    const double loop = map.lengthMetres(), step = 0.00406;
    const int passes = 409;                 // the reference's clean window
    double x = 0.0;

    std::vector<double> level;
    level.reserve(static_cast<std::size_t>(passes));
    for (int p = 0; p < passes; ++p)
    {
        for (int b = 0; b < static_cast<int>(loop / step); ++b, x += step)
            map.pass(x, x + step);

        // The same level the endpoint anchor reads, so the two are commensurable.
        double mean = 0.0;
        for (std::size_t i = 0; i < map.numRegions(); ++i)
            mean += static_cast<double>(map.coatingMap()[i]) * map.intactMap()[i];
        level.push_back(20.0 * std::log10(mean / static_cast<double>(map.numRegions())
                                          + 1.0e-12));
    }

    // The drop that STAYS. Nothing in this model can raise a level, so the
    // recovery window is a no-op here -- it is kept so that our series and the
    // reference are reduced by the same estimator rather than by two.
    const int recover = 5;
    std::vector<double> irreversible;
    for (std::size_t p = 0; p + recover + 1 < level.size(); ++p)
    {
        double best = level[p + 1];
        for (int j = 2; j <= recover; ++j)
            best = std::max(best, level[p + static_cast<std::size_t>(j)]);
        irreversible.push_back(std::max(0.0, level[p] - best));
    }

    double total = 0.0;
    for (double d : irreversible) total += d;
    INFO("total irreversible decline " << total << " dB (dlp 1.1: 34.4)");
    REQUIRE(total > 1.0);               // it has to wear at all for this to mean anything

    std::sort(irreversible.begin(), irreversible.end(), std::greater<double>());
    const double biggest = 100.0 * irreversible[0] / total;
    double top5 = 0.0;
    for (int i = 0; i < 5 && i < static_cast<int>(irreversible.size()); ++i)
        top5 += irreversible[static_cast<std::size_t>(i)];
    top5 = 100.0 * top5 / total;

    INFO("largest single step " << biggest << "% of the decline, top five "
         << top5 << "% (dlp 1.1: 5.4 and 20.6)");

    // BRACKETING, not matching. The real figures are 5.4% and 20.6%; these
    // allow half as much roughness again before calling it a staircase.
    REQUIRE(biggest < 8.0);
    REQUIRE(top5 < 30.0);
}

TEST_CASE("a dragging head fatigues the binder faster, and cannot run away",
          "[wear][teeth]")
{
    // THE MACHINE'S HALF OF FRICTION. The tape's own friction is already in
    // `frictionScale` -- lubricant gone, binder sticky, surface pitted -- and
    // this is the other contributor: deposits on the head face. IASA TC-05 has
    // tapes that "squeal during replay due to friction because of sticky
    // pigment and binder particles deposited on tape guides and audio and video
    // heads". Friction fatigues the binder, so a dirty head flakes a reel
    // sooner than a clean one does.
    const auto play = [](double drag, int passes)
    {
        tape::WearConstants k;
        tape::WearMap m;
        m.prepare(1.259, k, /*circular*/ true, /*seed*/ 7);
        m.setBinderAge(0.65);
        const double step = 0.00406;
        const int blocks = static_cast<int>(m.lengthMetres() / step);
        double at = 0.0;
        for (int p = 0; p < passes; ++p)
            for (int b = 0; b < blocks; ++b, at += step)
                m.pass(at, at + step, drag);
        return m.meanIntact();
    };

    const double clean = play(1.0, 150);
    const double dirty = play(2.0, 150);
    INFO("after 150 passes the reel keeps " << clean << " of its area on a clean"
         " head and " << dirty << " on a caked one");
    REQUIRE(dirty < clean);            // the dirty machine costs the tape more

    // A CLEAN HEAD CHANGES NOTHING AT ALL, which is what makes this safe to add
    // to an engine that already had a wear model: `drag` defaults to one and
    // one is the identity.
    tape::WearConstants k;
    tape::WearMap a, b;
    a.prepare(1.259, k, true, 7); a.setBinderAge(0.65);
    b.prepare(1.259, k, true, 7); b.setBinderAge(0.65);
    const double step = 0.00406;
    const int blocks = static_cast<int>(a.lengthMetres() / step);
    double at = 0.0;
    for (int p = 0; p < 40; ++p)
        for (int i = 0; i < blocks; ++i, at += step)
        {
            a.pass(at, at + step);          // the old call, no argument
            b.pass(at, at + step, 1.0);     // the new one, explicitly clean
        }
    for (std::size_t i = 0; i < a.numRegions(); ++i)
        REQUIRE(a.intactMap()[i] == b.intactMap()[i]);

    // ---- AND THE LOOP HAS A CEILING, WHICH IS WHY IT MAY BE WIRED AT ALL ----
    //
    // `ROADMAP.md` asked for this before the coupling was built: dirty head ->
    // more friction -> faster fatigue -> more flaking -> more deposit -> dirtier
    // head. The ceiling is STRUCTURAL rather than a clamp. Deposits saturate, so
    // the drag they can produce saturates with them, and the worst case is
    // simply `HeadDeposits::drag` applied forever -- which is a reel that wears
    // faster, not one that wears without bound.
    //
    // Held here by showing the damage stays a monotone, saturating function of
    // the drag rather than diverging in it.
    const double d1 = play(1.0, 100);
    const double d2 = play(2.0, 100);
    const double d4 = play(4.0, 100);
    INFO("area left at drag 1, 2, 4: " << d1 << ", " << d2 << ", " << d4);
    REQUIRE(d2 <= d1);
    REQUIRE(d4 <= d2);
    REQUIRE(d4 > 0.0);                 // it never eats the reel entirely
    // Doubling the drag again does LESS than the first doubling did: the reel
    // runs out of layer to lift, which is `spallFloor` arresting it.
    REQUIRE((d2 - d4) <= (d1 - d2) + 1.0e-9);
}

TEST_CASE("the damage sits where a flake is, not where a region is", "[wear][teeth]")
{
    // THE FIELD IS THE REALISATION AND THE REGION IS THE BOOKKEEPING, and the
    // whole scheme rests on those two agreeing. `intact` stops being read as a
    // level and becomes a THRESHOLD: with the field uniform on [0, 1],
    // thresholding at `s` removes an area fraction of exactly `s`.
    tape::WearConstants k;
    tape::WearMap map;
    map.prepare(1.259, k, /*circular*/ true, /*seed*/ 7);

    const auto& field = map.striationMap();
    REQUIRE(field.size() > 1000);

    // UNIFORM, BY RANK. A sum of octaves is roughly Gaussian and thresholding a
    // Gaussian at `s` does not remove `s` -- so the region's mean and what the
    // head hears would drift apart with nothing to show it. Ten equal bins.
    int bins[10] = { 0 };
    for (auto v : field)
        ++bins[std::min(9, static_cast<int>(v * 10.0f))];
    const auto expect = static_cast<double>(field.size()) / 10.0;
    for (int b = 0; b < 10; ++b)
        REQUIRE(std::abs(bins[b] - expect) < 0.02 * expect);

    // THE CLOSED FORM. `s = (1 - mean) - w/2` makes the realised mean exactly
    // the region's, with no lookup table -- and this is what fails if the field
    // stops being uniform or the band stops being symmetric.
    const int N = 100000;
    for (double mean : { 0.9, 0.75, 0.5, 0.25 })
    {
        double acc = 0.0;
        for (int i = 0; i < N; ++i)
            acc += map.realiseIntact(1.259 * i / N, mean);
        INFO("mean " << mean << " realised " << acc / N);
        REQUIRE(acc / N == Approx(mean).margin(0.002));
    }

    // AND THE DEFECTS ARE THE SIZE OF A FLAKE, WHICH IS WHERE THE BOUNDS COME
    // FROM. A region is 10 mm, and every dropout this engine made used to be
    // one region wide and sat on that lattice -- so it arrived at the same
    // place every lap, which is a mechanism and not a flake.
    //
    // MEASURED IN FLAKES RATHER THAN MILLISECONDS, because the answer must not
    // depend on the machine's speed or on any length but `flakeMetres`. These
    // bounds used to read "3 to 60 ms", which was 0.57 to 11.4 mm at Slipback's
    // speed and was written when the field's scale was a separate constant
    // eight times the flake (`SOURCES §32`).
    //
    // A DROPOUT READS SHORTER THAN THE FLAKE THAT MADE IT, which is why the
    // lower bound is a fraction. Coverage across the track peaks at the flake's
    // centre chord and falls away either side, so only the middle of a flake
    // crosses a threshold at all. dlp 1.1 measures 0.29 mm against a flake of
    // 1 mm, and this engine reads 0.43.
    std::vector<double> lengths;
    double run = 0.0;
    const double dx = 1.259 / N, speed = 0.19;
    for (int i = 0; i < N; ++i)
    {
        if (map.realiseIntact(i * dx, 0.75) < 0.5)
            run += dx;
        else if (run > 0.0) { lengths.push_back(run / speed * 1000.0); run = 0.0; }
    }
    std::sort(lengths.begin(), lengths.end());
    const double median = lengths.empty() ? 0.0 : lengths[lengths.size() / 2];
    INFO("at 25% loss: " << lengths.size() << " dropouts, median " << median
         << " ms, longest " << (lengths.empty() ? 0.0 : lengths.back()) << " ms");
    const double flakeMs = k.flakeMetres / speed * 1000.0;
    INFO("one flake is " << flakeMs << " ms at this speed");
    REQUIRE(lengths.size() > 20);
    REQUIRE(median > 0.15 * flakeMs);   // shorter than a flake, but not by much
    REQUIRE(median < 1.5 * flakeMs);    // and NOT a cluster of them
    REQUIRE(lengths.back() < 10.0 * flakeMs);   // nor is the worst one

    // DETERMINISTIC, and a different reel is a different reel.
    tape::WearMap same, other;
    same.prepare(1.259, k, true, 7);
    other.prepare(1.259, k, true, 8);
    for (std::size_t i = 0; i < field.size(); ++i)
        REQUIRE(same.striationMap()[i] == field[i]);
    double diff = 0.0;
    for (std::size_t i = 0; i < field.size(); ++i)
        diff += std::abs(other.striationMap()[i] - field[i]);
    REQUIRE(diff / static_cast<double>(field.size()) > 0.1);
}
