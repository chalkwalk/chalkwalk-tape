// The sliding-band companding system (chalkwalk/tape/SlidingBand.h, SOURCES section 54).
//
// Every assertion here is against Dolby's own published figures or against an
// algebraic property of the dual-path topology. Nothing is a stored buffer.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/SlidingBand.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    // The encode response at one frequency, with a tone of the given level
    // present -- which is what Dolby's figures plot, since the band's position
    // depends on what it is being shown.
    double encodeDb(double hz, double levelDb)
    {
        tape::SlidingBand band;
        band.prepare(kRate);
        const double amp = band.referenceLevel() * std::pow(10.0, levelDb / 20.0);

        double in = 0.0, out = 0.0;
        const int n = static_cast<int>(kRate);
        for (int i = 0; i < n; ++i)
        {
            const double x = amp * std::sin(2.0 * kPi * hz * i / kRate);
            const double y = band.encode(x);
            if (i > n / 2) { in += x * x; out += y * y; }
        }
        return 10.0 * std::log10(out / in);
    }
}

TEST_CASE("the round trip holds over the whole plane, not at a few points",
          "[sliding][teeth]")
{
    // ---- THE TEST THAT WAS MISSING, AND WHAT IT COST ----
    //
    // The exactness above was asserted at a handful of levels and frequencies,
    // and the system LATCHED outside them. Above about 8 kHz, at input levels
    // inside the side-path limiter's window, the expander came back with errors
    // of thirty to a hundred and thirty PERCENT -- while every published fit in
    // this file still measured correctly, because none of those readings is
    // taken in that corner.
    //
    // The cause was a sign. The limiter's detector watched the variable
    // filter's output, so in the DECODER more side-path gain made `z` smaller,
    // which made the detector read lower, which raised the gain again: positive
    // feedback with a loop gain near 1.5. The encoder was unaffected, being
    // feed-forward, so the encode curves stayed right while the decode was
    // ruined. `SlidingBand.h` carries the fix -- the limiter now watches the
    // ENCODED signal, which both halves have and neither's loop contains.
    //
    // So the grid is the assertion. Coarse enough to run in a suite, dense
    // enough that a region cannot hide in it.
    for (double hz : { 60.0, 200.0, 440.0, 997.0, 3000.0, 8000.0,
                       11999.0, 12000.0, 12001.0, 16000.0, 19000.0 })
        for (double levelDb = -2.0; levelDb >= -60.0; levelDb -= 2.0)
        {
            tape::SlidingBand enc, dec;
            enc.prepare(kRate);
            dec.prepare(kRate);
            const double amp = enc.referenceLevel()
                             * std::pow(10.0, levelDb / 20.0);
            const int n = static_cast<int>(kRate / 4);
            double worst = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double x = amp * std::sin(2.0 * kPi * hz * i / kRate);
                const double z = dec.decode(enc.encode(x));
                if (i > n / 2) worst = std::max(worst, std::abs(z - x) / amp);
            }
            INFO(hz << " Hz at " << levelDb << " dB re Dolby level: worst "
                 << worst << " relative to amplitude");
            REQUIRE(worst < 1.0e-9);
        }
}

TEST_CASE("two stages in series reach the depths C-type publishes",
          "[sliding][teeth]")
{
    // ---- THREE PUBLISHED NUMBERS, ONE FITTED CONSTANT (`SOURCES §54`) ----
    //
    // Dolby give C-type's depth in three places -- "about 15 dB of noise
    // reduction around 400 Hz", "20 dB in the critical 2,000 to 10,000 Hz hiss
    // area", and action beginning "in the 100 Hz region". `kCornerMinHz` is the
    // only thing fitted to them, so two of the three are free checks.
    const auto boostDb = [](double hz, double levelDb)
    {
        tape::SlidingBandPair pair;
        pair.prepare(kRate);
        const double amp = tape::SlidingBand::kDolbyLevel
                         * std::pow(10.0, levelDb / 20.0);
        double in = 0.0, out = 0.0;
        const int n = static_cast<int>(kRate);
        for (int i = 0; i < n; ++i)
        {
            const double x = amp * std::sin(2.0 * kPi * hz * i / kRate);
            const double y = pair.encode(x);
            if (i > n / 2) { in += x * x; out += y * y; }
        }
        return 10.0 * std::log10(out / in);
    };

    // At full action, which is where the published depths are quoted.
    const double deep = -60.0;
    const double at400 = boostDb(400.0, deep);
    const double at2k = boostDb(2000.0, deep);
    const double at5k = boostDb(5000.0, deep);
    const double at10k = boostDb(10000.0, deep);
    const double at100 = boostDb(100.0, deep);
    INFO("C-type at full action: " << at100 << " dB at 100 Hz, " << at400
         << " at 400, " << at2k << " at 2 k, " << at5k << " at 5 k, "
         << at10k << " at 10 k");
    REQUIRE(at400 == Approx(15.0).margin(1.0));
    REQUIRE(at2k == Approx(20.0).margin(1.0));
    REQUIRE(at5k == Approx(20.0).margin(1.0));

    // ---- AND 10 kHz IS DELIBERATELY SHORT OF TWENTY ----
    //
    // Spectral skewing's corner is exactly there, so a first-order shelf is
    // already partway down at it, and the noise reduction available above
    // 10 kHz is reduced by however much the encoder was told to ignore. That is
    // not a miss: it is what skewing COSTS, and Dolby quote the 20 dB figure
    // for "the critical 2,000 to 10,000 Hz hiss area" -- up to the corner, not
    // past it. Reads 17.4.
    REQUIRE(at10k < at5k - 1.0);
    REQUIRE(at10k > 15.0);
    // "Begins to take effect in the 100 Hz region" -- so something, and not
    // much. B-type has under half a decibel here.
    REQUIRE(at100 > 2.0);
    REQUIRE(at100 < 6.0);

    // ---- AND THE TWO STAGES HAND OVER WITHOUT A PLATEAU ----
    //
    // "As one filter reaches the end of its sliding range, the other one
    // GRADUALLY takes over." A stage offset that is too large leaves a range of
    // input levels over which the first has finished and the second has not
    // started, and the system stops responding to level -- which is what
    // `kStageOffsetDb` is fitted to avoid. Measured at 15 dB it left a step of
    // 0.42 dB per 5 dB of level; at 10 it is never below 1.2.
    double previous = -1.0;
    for (double levelDb = -10.0; levelDb >= -45.0; levelDb -= 5.0)
    {
        const double now = boostDb(5000.0, levelDb);
        if (previous >= 0.0)
        {
            INFO("5 kHz: " << now << " dB at " << levelDb
                 << ", against " << previous << " five decibels louder");
            REQUIRE(now > previous + 1.0);
        }
        previous = now;
    }

    // ---- AND IT IS DEEPER THAN ONE STAGE, WHICH IS THE WHOLE POINT ----
    //
    // 10 dB against 20. If this ever fails by a small amount the offset has
    // drifted; if it fails by a large one the two stages are not in series.
    // Read at 5 kHz rather than 10, because at 10 the comparison is not
    // stage-count against stage-count: C's spectral skewing acts there and
    // B has none, so it would be measuring the network instead.
    REQUIRE(at5k > encodeDb(5000.0, deep) + 8.0);
}

TEST_CASE("the two-stage pair inverts itself, and only in the right order",
          "[sliding][teeth]")
{
    // ---- A CHAIN OF EXACT INVERSES IS EXACT ONLY IN REVERSE ----
    //
    // Encode is high-level then low-level, so decode must be low-level then
    // high-level. Getting that backwards still builds, still sounds like noise
    // reduction, and leaves a residual that looks exactly like mistracking --
    // which is the most expensive kind of bug this system can have, because
    // mistracking is a thing it is SUPPOSED to do.
    for (double hz : { 200.0, 997.0, 5000.0, 12000.0, 16000.0 })
        for (double levelDb : { -5.0, -15.0, -25.0, -35.0, -45.0, -55.0 })
        {
            tape::SlidingBandPair enc, dec;
            enc.prepare(kRate);
            dec.prepare(kRate);
            const double amp = tape::SlidingBand::kDolbyLevel
                             * std::pow(10.0, levelDb / 20.0);
            const int n = static_cast<int>(kRate / 2);
            double worst = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double x = amp * std::sin(2.0 * kPi * hz * i / kRate);
                const double z = dec.decode(enc.encode(x));
                if (i > n / 2) worst = std::max(worst, std::abs(z - x) / amp);
            }
            INFO(hz << " Hz at " << levelDb << " dB: worst " << worst);
            REQUIRE(worst < 1.0e-9);
        }
}

TEST_CASE("the dual path round-trips exactly, which is the topology's whole claim",
          "[sliding][teeth]")
{
    // ---- `z = y - F z`, THEREFORE `z = x` ----
    //
    // The patent's own proof, asserted on the implementation. This is a much
    // stronger claim than the compander's round trip: there, complementarity is
    // arithmetic and holds to a tolerance. Here it is STRUCTURAL -- any F at all
    // inverts, so the only thing that can break it is an arithmetic slip.
    //
    // AND IT NEEDS NO UNIT DELAY, which is the part that was not obvious: the
    // expander's dependence on its own output is affine, so it solves in closed
    // form. If that solve were wrong -- or if a delay had been inserted to dodge
    // it -- the error would show here and nowhere else.
    tape::SlidingBand enc, dec;
    enc.prepare(kRate);
    dec.prepare(kRate);

    std::uint64_t rng = 20260907;
    double worst = 0.0;
    for (int i = 0; i < 200000; ++i)
    {
        // Broadband and wide-ranging, so the band is moving throughout: a
        // signal that held the corner still would not test the control path.
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        const double noise =
            static_cast<double>(rng >> 11) / 4503599627370496.0 - 1.0;
        const double envelope = 0.5 + 0.5 * std::sin(2.0 * kPi * 0.7 * i / kRate);
        const double x = 2.0 * envelope * envelope * noise;

        const double z = dec.decode(enc.encode(x));
        if (i > 1000)
            worst = std::max(worst, std::abs(z - x));
    }
    INFO("worst round-trip error " << worst);
    REQUIRE(worst < 1.0e-9);
}

TEST_CASE("the low-level encode curve is the published one", "[sliding]")
{
    // ---- FITTED TO DOLBY'S FIGURE 6 (`SOURCES §54`) ----
    //
    // Read off the published graph, so the tolerance is the graph's: about a
    // decibel, and a third of an octave horizontally. Asserted anyway, because
    // "a fit to a published curve" is a claim that stops being true the moment
    // somebody moves a constant.
    struct Point { double hz; double read; };
    for (auto p : { Point{ 300.0, 1.3 }, Point{ 500.0, 2.8 }, Point{ 1000.0, 5.5 },
                    Point{ 2000.0, 8.3 }, Point{ 5000.0, 10.0 },
                    Point{ 10000.0, 10.0 } })
    {
        // Well below the lower knee, so the band is at the bottom of its slide
        // and this is the maximum-action curve Figure 6 plots.
        const double db = encodeDb(p.hz, -45.0);
        INFO(p.hz << " Hz: published " << p.read << " dB, model " << db);
        REQUIRE(db == Approx(p.read).margin(0.5));
    }
}

TEST_CASE("and the band slides with level, as the family of curves says",
          "[sliding][teeth]")
{
    // ---- DOLBY'S FIGURE 2, WHICH IS THE SLIDING LAW ITSELF ----
    //
    // One curve per input level in 5 dB steps, "OUTPUT LEVEL IN dB (re DOLBY
    // LEVEL)" on the vertical axis. Read at 5 kHz, which is above the corner at
    // every level and so reads the slide rather than the shelf.
    struct Row { double levelDb; double read; };
    for (auto r : { Row{ -10.0, 3.0 }, Row{ -15.0, 5.0 }, Row{ -20.0, 6.5 },
                    Row{ -25.0, 8.0 }, Row{ -30.0, 9.5 }, Row{ -40.0, 10.0 } })
    {
        const double db = encodeDb(5000.0, r.levelDb);
        INFO("input " << r.levelDb << " dB re Dolby level: published "
             << r.read << " dB, model " << db);
        REQUIRE(db == Approx(r.read).margin(0.6));
    }

    // ---- AND AT AND ABOVE THE UPPER KNEE IT IS A WIRE ----
    //
    // The top two curves of Figure 2 are flat. That is the unity-gain region of
    // the bilinear characteristic, and it is what makes this system leave loud
    // passages alone where a 2:1 compander cannot.
    //
    // It arrives by ARITHMETIC rather than by a switch: the corner slides past
    // Nyquist and the one-pole's output vanishes on its own. A switch there
    // would click, and the click would be recorded.
    // AND IT IS FLAT TO A HUNDREDTH OF A DECIBEL, which took a second
    // mechanism. The sliding corner alone left 0.73 dB of boost here: the
    // detector watches the filter's own output, so as the band slides up it
    // sees less and lets the band fall back, and the corner self-limits instead
    // of running away.
    //
    // What closes it is the patent's side-path limiter, realised as a
    // level-dependent GAIN rather than a waveshaper -- which is both what the
    // circuit is and the only form that leaves the expander's equation affine
    // in its own output (`SlidingBand.h`).
    const double atDolbyLevel = encodeDb(5000.0, 0.0);
    INFO("at Dolby level the system boosts by " << atDolbyLevel << " dB");
    REQUIRE(atDolbyLevel == Approx(0.0).margin(0.1));
}

TEST_CASE("the band slides out of the way of a dominant signal", "[sliding]")
{
    // ---- FIGURE 3, WHICH IS WHY THE DETECTOR WATCHES THE FILTER ----
    //
    // A loud bass drum must NOT shut the system down: the band slides up out of
    // its way and keeps working above it. A detector watching the input
    // broadband could not do that, and this is the observable that argues for
    // the choice (`SlidingBand.h` -- the topology inside the side path is not
    // published).
    tape::SlidingBand quiet, loud;
    quiet.prepare(kRate);
    loud.prepare(kRate);

    const double ref = quiet.referenceLevel();
    for (int i = 0; i < static_cast<int>(kRate); ++i)
    {
        const double t = double(i) / kRate;
        quiet.encode(0.0);
        // A loud 60 Hz tone, well above the upper knee: the sort of thing that
        // hides its own noise and needs no help.
        loud.encode(ref * std::sin(2.0 * kPi * 60.0 * t));
    }

    INFO("corner with silence " << quiet.cornerHz()
         << " Hz, with a loud bass tone " << loud.cornerHz());

    // Silence leaves the band at the bottom of its slide, working hardest.
    REQUIRE(quiet.cornerHz() == Approx(tape::SlidingBand::kCornerMinHz).margin(1.0));

    // AND A LOUD BASS TONE BARELY MOVES IT, because 60 Hz is far below the
    // band: the filter passes almost none of it, so the detector hardly sees
    // it. THAT is the sliding-band behaviour, and it is the thing a broadband
    // detector would get wrong.
    REQUIRE(loud.cornerHz() < 4.0 * tape::SlidingBand::kCornerMinHz);
}

TEST_CASE("a level error mistracks, and it is not a gain error", "[sliding][teeth]")
{
    // ---- THE DIFFERENCE FROM dbx, MEASURED ----
    //
    // `Compander.h`'s arithmetic says a gain error `g` between encoder and
    // decoder comes out as `2g` and NOTHING ELSE: a constant offset, the same at
    // every level and every frequency, which on a tape played back through the
    // same machine nobody would hear.
    //
    // A sliding-band system cannot do that. The side path's action is a
    // non-linear function of level, so an error puts the two bands on different
    // parts of the curve -- and the residue is FREQUENCY-DEPENDENT, which is
    // what a listener hears as dullness or brightness rather than as level.
    //
    // Dolby's own words: "If, for some reason, the level or the frequency
    // response of the encoded signal is changed before it reaches the decoder,
    // mistracking of the sliding bands will occur."
    const auto residueAt = [](double hz, double errorDb)
    {
        tape::SlidingBand enc, dec;
        enc.prepare(kRate);
        dec.prepare(kRate);
        const double amp = enc.referenceLevel() * std::pow(10.0, -25.0 / 20.0);
        const double error = std::pow(10.0, errorDb / 20.0);

        double in = 0.0, out = 0.0;
        const int n = 2 * static_cast<int>(kRate);
        for (int i = 0; i < n; ++i)
        {
            const double x = amp * std::sin(2.0 * kPi * hz * i / kRate);
            const double z = dec.decode(error * enc.encode(x));
            if (i > n / 2) { in += x * x; out += z * z; }
        }
        // The error itself is a plain gain and is divided out, so what is left
        // is the MISTRACKING and not the error.
        return 10.0 * std::log10(out / in) - errorDb;
    };

    // With no error the pair is a wire at every frequency.
    for (double hz : { 200.0, 1000.0, 6000.0 })
        REQUIRE(residueAt(hz, 0.0) == Approx(0.0).margin(0.1));

    const double low = residueAt(200.0, 3.0);
    const double mid = residueAt(1000.0, 3.0);
    const double high = residueAt(6000.0, 3.0);
    INFO("3 dB level error mistracks: 200 Hz " << low << ", 1 kHz " << mid
         << ", 6 kHz " << high);

    // IT MISTRACKS AT ALL, which a 2:1 compander would not.
    REQUIRE(std::abs(high) > 0.5);

    // AND THE RESIDUE IS NOT THE SAME ACROSS THE BAND, which is the claim that
    // separates this from a gain error. A tilt, not an offset.
    REQUIRE(std::abs(high - low) > 0.5);
}
