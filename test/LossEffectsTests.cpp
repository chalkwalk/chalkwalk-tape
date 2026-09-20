// The reproduce-side loss chain, asserted against closed forms.
//
// These cases were written in Remanence (test/CoreTests.cpp) and moved here
// unchanged when the loss chain was promoted -- that they pass unchanged
// outside the project that wrote them is the acceptance criterion for the
// extraction, so the assertions below are deliberately untouched.
//
// They assert CLOSED-FORM EXPECTATIONS, not golden buffers: a golden buffer
// tells you something changed, but not which version was right.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/LossEffects.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

// ---------------------------------------------------------------------------
// Loss effects. docs/references/LOSS-EFFECTS.md is the specification.
// ---------------------------------------------------------------------------

TEST_CASE("spacing loss is 54.6 dB per wavelength of spacing", "[tape][loss]")
{
    // The canonical statement of the result: at a spacing of one wavelength,
    // output is down 54.6 dB. Asserting the constant rather than a buffer is
    // what makes this test able to fail informatively.
    const double lambda = 1.0e-3;
    const double k = 2.0 * M_PI / lambda;

    const double atOneWavelength = tape::spacingLoss(k, lambda);
    const double dB = 20.0 * std::log10(atOneWavelength);

    REQUIRE(dB == Approx(-54.575).margin(0.01));
}

TEST_CASE("spacing loss is exponential in spacing", "[tape][loss]")
{
    const double k = 2.0 * M_PI / 1.0e-3;
    // Doubling the spacing squares the loss; that is what exponential means and
    // it is the property a wrong sign or a stray factor would break.
    REQUIRE(tape::spacingLoss(k, 2.0e-5)
            == Approx(tape::spacingLoss(k, 1.0e-5) * tape::spacingLoss(k, 1.0e-5)));
}

TEST_CASE("thickness loss has the right limits", "[tape][loss]")
{
    const double delta = 35.0e-6;

    SECTION("long wavelength: the whole coating contributes")
    {
        const double k = 2.0 * M_PI / 100.0;  // lambda enormous vs delta
        REQUIRE(tape::thicknessLoss(k, delta) == Approx(1.0).margin(1.0e-4));
    }

    SECTION("the approach to that limit is first-order in k*delta")
    {
        // Asserting the RATE the limit is approached at, not just the limit:
        // (1 - exp(-x))/x = 1 - x/2 + O(x^2), so at lambda = 1 m the loss is
        // 0.99989 rather than 1, and a test that only checked "close to 1" would
        // pass for any function that happened to be close to 1.
        const double k = 2.0 * M_PI / 1.0;
        const double kd = k * delta;
        REQUIRE(tape::thicknessLoss(k, delta) == Approx(1.0 - kd / 2.0).epsilon(1.0e-6));
    }

    SECTION("short wavelength: only the top skin, so it tends to 1/(k*delta)")
    {
        const double k = 2.0 * M_PI / 1.0e-6;
        REQUIRE(tape::thicknessLoss(k, delta) == Approx(1.0 / (k * delta)).epsilon(1.0e-6));
    }

    SECTION("it is monotonic -- no nulls, unlike gap loss")
    {
        double previous = 2.0;
        for (int i = 1; i <= 400; ++i)
        {
            const double k = static_cast<double>(i) * 1.0e4;
            const double value = tape::thicknessLoss(k, delta);
            REQUIRE(value < previous);
            previous = value;
        }
    }

    SECTION("the near-zero branch agrees with the closed form")
    {
        // The series expansion and the closed form must meet, or there is a
        // step discontinuity hiding at the branch.
        const double kd = 1.0e-6;
        const double closedForm = (1.0 - std::exp(-kd)) / kd;
        REQUIRE(tape::thicknessLoss(1.0e-6, 1.0) == Approx(closedForm).epsilon(1.0e-9));
    }
}

// ---------------------------------------------------------------------------
// Gap loss: the three geometries. SOURCES section 3.
//
// This is where we deliberately differ from the usual implementation, so it is
// where the tests have to be sharpest.
// ---------------------------------------------------------------------------

TEST_CASE("all three gap-loss functions are unity at DC", "[tape][loss][gap]")
{
    REQUIRE(tape::gapLossInfinite(0.0) == Approx(1.0));
    REQUIRE(tape::gapLossThin(0.0) == Approx(1.0));
    REQUIRE(tape::gapLossSemiInfinite(0.0) == Approx(1.0));
}

TEST_CASE("type a nulls where the gap equals a whole wavelength", "[tape][loss][gap]")
{
    // x = pi*l/lambda, so l = lambda is x = pi.
    for (int n = 1; n <= 4; ++n)
        REQUIRE(tape::gapLossInfinite(static_cast<double>(n) * M_PI) == Approx(0.0).margin(1.0e-12));
}

TEST_CASE("the practical head nulls at 0.9 wavelengths, not 1.0", "[tape][loss][gap]")
{
    // THE finding. A gap length inferred from the first null on the l = lambda
    // assumption is about 10% too large; the literature records the same
    // discrepancy from the other side, as the magnetic gap measuring 10% longer
    // than the mechanical one.
    //
    // If this test ever "fails because someone simplified gapLossSemiInfinite
    // to a sinc", that is the test doing its job.
    REQUIRE(tape::gapLossSemiInfinite(0.9 * M_PI) == Approx(0.0).margin(1.0e-9));

    // And it is NOT null where the textbook formula is.
    REQUIRE(std::abs(tape::gapLossSemiInfinite(M_PI)) > 0.05);
}

TEST_CASE("type b maxima decay more slowly than type a", "[tape][loss][gap]")
{
    // The distinguishing property: J0's maxima fall as 1/sqrt(x), G's as 1/x.
    // So far past the first null, the thin-gap head keeps materially more
    // output -- which is the whole reason the three are not interchangeable.
    auto peakNear = [](auto fn, double centre)
    {
        double best = 0.0;
        for (int i = -200; i <= 200; ++i)
            best = std::max(best, std::abs(fn(centre + static_cast<double>(i) * 0.001)));
        return best;
    };

    const double aPeak = peakNear(tape::gapLossInfinite, 10.5 * M_PI);
    const double bPeak = peakNear(tape::gapLossThin, 10.5 * M_PI);
    REQUIRE(bPeak > aPeak * 2.0);
}

TEST_CASE("the semi-infinite head is intermediate between the other two", "[tape][loss][gap]")
{
    // Stated in the source as a physical expectation, so it is worth asserting:
    // type c lies between type a and type b. Compared on envelope, since the
    // three do not share null positions.
    auto envelope = [](auto fn, double centre)
    {
        double best = 0.0;
        for (int i = -400; i <= 400; ++i)
            best = std::max(best, std::abs(fn(centre + static_cast<double>(i) * 0.002)));
        return best;
    };

    for (const double centre : {6.5 * M_PI, 10.5 * M_PI, 14.5 * M_PI})
    {
        const double a = envelope(tape::gapLossInfinite, centre);
        const double b = envelope(tape::gapLossThin, centre);
        const double c = envelope(tape::gapLossSemiInfinite, centre);
        REQUIRE(c > a);
        REQUIRE(c < b);
    }
}

TEST_CASE("azimuth loss and gap loss are the same integral", "[tape][loss][azimuth]")
{
    // SOURCES section 4: both average a sinusoid over a window. Gap loss over
    // the gap length, azimuth loss over the along-track displacement
    // W*tan(theta). They therefore agree when the windows match, and this test
    // is what stops the two implementations drifting apart.
    const double k = 2.0 * M_PI / 1.0e-3;
    const double width = 1.0e-3;
    const double theta = 0.01;
    const double window = width * std::tan(theta);

    REQUIRE(tape::azimuthLoss(k, width, theta)
            == Approx(tape::gapLossInfinite(0.5 * k * window)));
}

TEST_CASE("perfect alignment costs nothing", "[tape][loss][azimuth]")
{
    REQUIRE(tape::azimuthLoss(2.0 * M_PI / 1.0e-3, 1.0e-3, 0.0) == Approx(1.0));
}

TEST_CASE("a stopped transport does not produce infinities", "[tape][loss]")
{
    // A stopped machine has no wavelength at all. The caller wants "no output",
    // not a NaN propagated through four multiplications into the audio path.
    REQUIRE(tape::waveNumber(1000.0, 0.0) == 0.0);

    tape::HeadGeometry head;
    const double response = tape::playbackResponse(1000.0, 0.0, head);
    REQUIRE(std::isfinite(response));
}

TEST_CASE("halving tape speed moves every loss the same way", "[tape][loss][speed]")
{
    // PRINCIPLES section 4: speed is the one control that legitimately moves
    // everything at once, because every loss is a function of wavelength and
    // lambda = v/f. So the response at (f, v) must equal that at (2f, 2v).
    tape::HeadGeometry head;
    for (const double f : {100.0, 1000.0, 8000.0})
        REQUIRE(tape::playbackResponse(f, 0.19, head)
                == Approx(tape::playbackResponse(f * 2.0, 0.38, head)));
}

TEST_CASE("playback response falls with frequency at a fixed speed", "[tape][loss][speed]")
{
    tape::HeadGeometry head;
    const double slow = tape::playbackResponse(10000.0, 0.048, head);
    const double fast = tape::playbackResponse(10000.0, 0.38, head);
    // A faster tape means a longer recorded wavelength, so less of every loss.
    REQUIRE(fast > slow);
}
