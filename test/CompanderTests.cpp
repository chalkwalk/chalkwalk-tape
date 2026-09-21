// The companding noise-reduction systems, and what the tape does to them.
//
// Written in Remanence (test/CoreTests.cpp) and moved here unchanged when
// `Compander` did. The round trip is the assertion with teeth: a complementary
// system is inaudible on a machine in alignment, so what these pin down is
// that it really is complementary -- and that the TAPE between the two halves
// is what makes it breathe.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/Compander.h>
#include <chalkwalk/tape/Hysteresis.h>
#include <chalkwalk/tape/LossEffects.h>
#include <chalkwalk/tape/TapeNoise.h>
#include <chalkwalk/tape/Medium.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

TEST_CASE("the compander round-trips, and the tape is what makes it breathe",
          "[compander][teeth]")
{
    constexpr double kPi = 3.14159265358979323846;
    // TWO INSTANCES, because the encoder and the decoder are two cards
    // (`Compander.h`). Sharing one would couple the decoder's gain to what the
    // encoder saw, which is the coupling the tape exists to break.
    constexpr double kRate = 48000.0;

    // ---- 1. WITH NOTHING BETWEEN THEM IT IS A WIRE ----
    //
    // 2:1 then 1:2 must return the level it was given, at every level, or the
    // system is not complementary and every other claim about it is void.
    for (double levelDb : { -60.0, -40.0, -20.0, -6.0 })
    {
        chalkwalk::tape::Compander enc, dec;
        enc.prepare(kRate); dec.prepare(kRate);

        const double amp = std::pow(10.0, levelDb / 20.0);
        double acc = 0.0; int n = 0;
        for (int i = 0; i < static_cast<int>(kRate); ++i)
        {
            const double x = amp * std::sin(2.0 * kPi * 300.0 * i / kRate);
            const double y = dec.decode(enc.encode(x));
            if (i > kRate / 2) { acc += y * y; ++n; }   // past the detector settling
        }
        const double outDb = 20.0 * std::log10(std::sqrt(acc / n) * std::sqrt(2.0));
        INFO("in " << levelDb << " dB, out " << outDb << " dB");
        REQUIRE(outDb == Approx(levelDb).margin(1.5));
    }

    // ---- 2. AND WITH A FLOOR BETWEEN THEM, THE FLOOR FOLLOWS THE MUSIC ----
    //
    // `SOURCES §53`: the expander raises its gain when the signal is loud and
    // the hiss is inside that gain, because it arrived between the two halves
    // and the decoder cannot tell it from signal. Nothing here models
    // breathing; this measures whether it happens.
    //
    // A LOW tone deliberately, because the halo is audible exactly when the
    // music has no high frequency content of its own to mask it.
    //
    // ---- MEASURED BY DIFFERENCE, AND THE FIRST ATTEMPT COULD NOT SEE IT ----
    //
    // That version took the first difference of the output as "the floor",
    // reasoning that it would cancel 80 Hz and pass the hiss. It does not: the
    // first difference of a sine is its derivative, which at amplitude A is
    // A * 2*pi*80/48000, and for a loud tone that is ten times the hiss. So it
    // measured the TONE and passed whether or not any companding was happening
    // -- which the teeth check caught by setting the ratio to 1:1 and watching
    // it pass anyway (`PRINCIPLES §7`).
    //
    // The honest measure is a DIFFERENCE OF TWO RUNS: identical signal, hiss
    // injected in one and not the other. What is left is exactly the hiss as it
    // arrives at the output, including whatever the expander did to it, and
    // nothing else.
    const auto haloAt = [&](double toneDb)
    {
        const auto run = [&](bool withHiss)
        {
            chalkwalk::tape::Compander enc, dec;
            enc.prepare(kRate); dec.prepare(kRate);
            std::uint64_t rng = 12345;
            const double amp = std::pow(10.0, toneDb / 20.0);
            std::vector<double> out;
            out.reserve(static_cast<std::size_t>(kRate));
            for (int i = 0; i < static_cast<int>(kRate); ++i)
            {
                const double x = amp * std::sin(2.0 * kPi * 80.0 * i / kRate);
                rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
                const double hiss = withHiss
                    ? 3.0e-4 * (static_cast<double>(rng >> 11) / 4503599627370496.0 - 1.0)
                    : 0.0;
                out.push_back(dec.decode(enc.encode(x) + hiss));
            }
            return out;
        };
        const auto with = run(true);
        const auto without = run(false);
        double acc = 0.0; int n = 0;
        for (std::size_t i = static_cast<std::size_t>(kRate / 2); i < with.size(); ++i)
        { const double d = with[i] - without[i]; acc += d * d; ++n; }
        return 10.0 * std::log10(acc / std::max(1, n) + 1.0e-30);
    };

    const double quiet = haloAt(-45.0);
    const double loud  = haloAt(-9.0);
    INFO("hiss at the output: quiet tone " << quiet << " dB, loud tone " << loud);

    // THE HALO. A loud low note must bring the hiss up with it -- this is the
    // artefact `PRINCIPLES §4` promises the switch has, and no code here
    // produces it.
    REQUIRE(loud > quiet + 12.0);
}

TEST_CASE("the compander round-trips at every FREQUENCY, not only every level",
          "[compander][teeth]")
{
    // ---- THE TEST THE LAST ONE COULD NOT BE ----
    //
    // The round trip above asks four LEVELS at one frequency, which is a
    // question about the DETECTOR. The emphasis pair is a question about
    // FREQUENCY, and it was wrong: the de-emphasis was built as the same shelf
    // with the reciprocal gain, which agrees with the inverse at DC and at the
    // top and is a +3.8 dB bump in between (`Compander.h`).
    //
    // Every level assertion passed the whole time, because a 300 Hz tone is two
    // octaves below where the error lives. So this sweeps, and it is tight: an
    // emphasis pair that does not cancel is not a tolerance, it is a tone
    // control nobody asked for.
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kRate = 48000.0;

    for (double hz : { 100.0, 1000.0, 3000.0, 5000.0, 10000.0, 15000.0 })
    {
        chalkwalk::tape::Compander enc, dec;
        enc.prepare(kRate); dec.prepare(kRate);

        const double amp = std::pow(10.0, -20.0 / 20.0);
        double in = 0.0, out = 0.0; int n = 0;
        for (int i = 0; i < static_cast<int>(kRate); ++i)
        {
            const double x = amp * std::sin(2.0 * kPi * hz * i / kRate);
            const double y = dec.decode(enc.encode(x));
            if (i > kRate / 2) { in += x * x; out += y * y; ++n; }
        }
        const double db = 10.0 * std::log10(out / in);
        INFO(hz << " Hz round-trips at " << db << " dB");
        REQUIRE(db == Approx(0.0).margin(0.25));
    }
}
