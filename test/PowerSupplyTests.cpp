// The machine's power supply (chalkwalk/tape/PowerSupply.h, SOURCES section 21).
//
// Two effects, one cause -- a reservoir capacitor that is not what it was --
// and the claims under test are about FREQUENCY and about TIME, both of which
// have closed forms to check against rather than buffers to compare with.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/PowerSupply.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    // How much energy a run of samples has at `hz`, by one bin of a DFT. The
    // question these tests ask is always "is it at the frequency the mains is
    // at", which is one bin and does not need a transform.
    [[nodiscard]] double binAt(const std::vector<double>& x, double hz)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double w = 2.0 * kPi * hz * double(i) / kRate;
            re += x[i] * std::cos(w);
            im -= x[i] * std::sin(w);
        }
        return 2.0 * std::sqrt(re * re + im * im) / double(x.size());
    }
}
TEST_CASE("a supply in good order is not there at all", "[supply][teeth]")
{
    // A MACHINE THAT IS WELL IS A WIRE. Every one of these defaults to zero, so
    // a new machine's electronics add nothing whatever -- which is what makes
    // the aged ones a DIFFERENCE rather than a permanent colour.
    tape::PowerSupply supply;
    supply.prepare(kRate, {});

    CHECK_FALSE(supply.active());
    CHECK(supply.rail() == Approx(1.0));

    for (int i = 0; i < 1000; ++i)
    {
        supply.draw(4.0);              // work it hard
        CHECK(supply.hum() == Approx(0.0));
        CHECK(supply.rail() == Approx(1.0));
    }
}

TEST_CASE("hum is at the mains frequency and at twice it", "[supply][teeth]")
{
    // THE CLASSIFICATION IS THE SOURCED PART (`SOURCES §21`): line frequency is
    // a grounding or shielding problem, twice line is the power supply. The
    // levels are declared and the FREQUENCIES are the claim, so the frequencies
    // are what is asserted.
    for (double mains : { 50.0, 60.0 })
    {
        tape::PowerSupply::Config config;
        config.lineHz = mains;
        config.humLine = 0.010;
        config.humRipple = 0.004;

        tape::PowerSupply supply;
        supply.prepare(kRate, config);

        std::vector<double> out(static_cast<std::size_t>(kRate));       // one second
        for (auto& v : out)
            v = supply.hum();

        CHECK(binAt(out, mains) == Approx(0.010).margin(2.0e-4));
        CHECK(binAt(out, 2.0 * mains) == Approx(0.004).margin(2.0e-4));

        // AND NOWHERE ELSE. A supply that hummed at the third harmonic would be
        // a different fault with a different cure, and this is two sinusoids
        // rather than a buzz.
        CHECK(binAt(out, 3.0 * mains) == Approx(0.0).margin(2.0e-4));
        CHECK(binAt(out, mains * 0.5) == Approx(0.0).margin(2.0e-4));
    }
}

TEST_CASE("the two hums are independent, because their causes are",
          "[supply][teeth]")
{
    // One runs on NEGLECT and the other on WEAR (`DESIGN.md` §7.2), so a
    // machine can have either without the other: a freshly serviced deck with
    // tired capacitors hums at 100 Hz and not at 50, and a neglected one with
    // good capacitors does the opposite. A single hum control could not say
    // that.
    tape::PowerSupply::Config shielding;
    shielding.humLine = 0.01;

    tape::PowerSupply::Config reservoir;
    reservoir.humRipple = 0.01;

    for (const auto& config : { shielding, reservoir })
    {
        tape::PowerSupply supply;
        supply.prepare(kRate, config);
        std::vector<double> out(static_cast<std::size_t>(kRate));
        for (auto& v : out)
            v = supply.hum();

        const bool isShielding = config.humLine > 0.0;
        CHECK(binAt(out, 50.0) == Approx(isShielding ? 0.01 : 0.0).margin(2.0e-4));
        CHECK(binAt(out, 100.0) == Approx(isShielding ? 0.0 : 0.01).margin(2.0e-4));
    }
}

TEST_CASE("hum is there whether or not anything is playing", "[supply]")
{
    // It is a voltage on the rail that gets into the signal path, so it does
    // not depend on the programme -- which is how you find it on a real
    // machine, by turning everything down and listening.
    tape::PowerSupply::Config config;
    config.humRipple = 0.01;

    tape::PowerSupply loaded, idle;
    loaded.prepare(kRate, config);
    idle.prepare(kRate, config);

    for (int i = 0; i < 4800; ++i)
    {
        loaded.draw(8.0);
        idle.draw(0.0);
        CHECK(loaded.hum() == Approx(idle.hum()).margin(1.0e-15));
    }
}

TEST_CASE("the rail sags under load and comes back", "[supply][teeth]")
{
    tape::PowerSupply::Config config;
    config.sagDepth = 0.2;          // a fifth of the rail at full load
    config.sagReference = 4.0;
    config.sagSeconds = 0.08;

    tape::PowerSupply supply;
    supply.prepare(kRate, config);

    // IDLE IS NOMINAL. A supply that sagged with nothing playing would be a
    // gain error, not a sag.
    CHECK(supply.rail() == Approx(1.0));

    // A LOUD PASSAGE PULLS IT DOWN, and to the depth the configuration says --
    // reached asymptotically, because a reservoir is a capacitor and not a
    // switch.
    for (int i = 0; i < int(kRate); ++i)
        supply.draw(4.0);
    CHECK(supply.rail() == Approx(0.8).margin(0.005));

    // ONE TIME CONSTANT IS 63 % OF THE WAY BACK, which is what makes this a
    // reservoir rather than a release curve somebody chose. From 0.8 towards
    // 1.0, after 80 ms, that is 0.8 + 0.632 * 0.2.
    for (int i = 0; i < int(0.08 * kRate); ++i)
        supply.draw(0.0);
    CHECK(supply.rail() == Approx(0.8 + 0.632 * 0.2).margin(0.01));

    // AND ALL THE WAY BACK, given time.
    for (int i = 0; i < int(kRate); ++i)
        supply.draw(0.0);
    CHECK(supply.rail() == Approx(1.0).margin(1.0e-3));
}

TEST_CASE("the sag is causal, and cannot see a note it has not passed",
          "[supply][teeth]")
{
    // A supply that sagged from a sample it had not yet reached would be a
    // LOOK-AHEAD compressor, which is a different machine and a much more
    // modern one. The rail a sample works against is what the preceding
    // programme left it at.
    tape::PowerSupply::Config config;
    config.sagDepth = 0.5;
    config.sagReference = 1.0;

    tape::PowerSupply supply;
    supply.prepare(kRate, config);

    const double before = supply.rail();
    supply.draw(1.0);                       // the loudest sample there is
    const double after = supply.rail();

    CHECK(before == Approx(1.0));
    CHECK(after < 1.0);                     // it has begun, from ONE sample
    CHECK(after > 0.99);                    // and only begun
}

TEST_CASE("a bigger reservoir sags more slowly", "[supply][teeth]")
{
    // The time constant is the reservoir, so this is the one knob that means
    // something physical: a machine with more capacitance rides a loud passage
    // for longer before the rail moves.
    auto reach = [](double seconds)
    {
        tape::PowerSupply::Config config;
        config.sagDepth = 0.3;
        config.sagReference = 1.0;
        config.sagSeconds = seconds;

        tape::PowerSupply supply;
        supply.prepare(kRate, config);
        for (int i = 0; i < int(0.05 * kRate); ++i)
            supply.draw(1.0);
        return 1.0 - supply.rail();
    };

    const double small = reach(0.02);
    const double large = reach(0.40);
    CHECK(small > large);
    CHECK(large < 0.5 * small);
}
