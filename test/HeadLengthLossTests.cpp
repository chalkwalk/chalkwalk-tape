// Duinker and Geurst's Table II (chalkwalk/tape/HeadLengthLoss.h, SOURCES 17).
//
// A transcribed table cannot be checked against a derivation, so it is checked
// against the PAPER'S OWN PROSE: four properties its authors state in words,
// asserted against the six hundred numbers. That is the only independent
// evidence available that the transcription is right, and it is better evidence
// than a spot-check, because a typo anywhere in a column breaks a property.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/HeadLengthLoss.h>
#include <chalkwalk/tape/LossEffects.h>

#include <cmath>

using Catch::Approx;
namespace tape = chalkwalk::tape;

TEST_CASE("the sharp-edged head overshoots by about 20 per cent",
          "[headlength][sources]")
{
    // "the plate type of head shows an overshoot in its response of about
    // 20 per cent for the case of sharp edges (P = 0)."
    double peak = 0.0;
    double peakAt = 0.0;
    for (int i = 0; i < tape::kHeadLengthRows; ++i)
    {
        const double v = tape::kHeadLengthLossFactor[static_cast<std::size_t>(i)][0];
        if (v > peak) { peak = v; peakAt = 0.1 * (i + 1); }
    }
    INFO("peak " << peak << " at theta = " << peakAt);
    REQUIRE(peak == Approx(1.195).margin(0.001));
    REQUIRE(peak > 1.15);
    REQUIRE(peak < 1.25);
}

TEST_CASE("a fully rounded head has no fluctuations at all",
          "[headlength][sources][teeth]")
{
    // "Finally, for P = pi/2 [...] the fluctuations have disappeared altogether
    // and the response approaches the unity level monotonically."
    //
    // This is the strongest check on the transcription: a single mistyped digit
    // anywhere in a hundred-row column breaks monotonicity. It found one -- the
    // OCR read 0.874 where the page says 0.824.
    double previous = 0.0;
    for (int i = 0; i < tape::kHeadLengthRows; ++i)
    {
        const double v = tape::kHeadLengthLossFactor[static_cast<std::size_t>(i)][5];
        INFO("theta = " << 0.1 * (i + 1) << ": " << v << " after " << previous);
        REQUIRE(v >= previous - 1.0e-12);
        previous = v;
    }
    REQUIRE(previous == Approx(1.0));
}

TEST_CASE("rounding costs output at the longest wavelengths",
          "[headlength][sources]")
{
    // The first row falls across the columns: at theta = 0.1 a rounder head
    // collects less, because "the contact area between the now semi-circularly
    // shaped head and the rectilinear tape is reduced".
    const auto& first = tape::kHeadLengthLossFactor[0];
    for (std::size_t j = 0; j + 1 < first.size(); ++j)
    {
        INFO("column " << j << ": " << first[j] << " then " << first[j + 1]);
        REQUIRE(first[j] > first[j + 1]);
    }
    REQUIRE(first[0] == Approx(0.701));
    REQUIRE(first[5] == Approx(0.643));
}

TEST_CASE("every shape converges on unity, and the sharpest converges slowest",
          "[headlength][sources]")
{
    // "For wavelengths short enough with respect to the overall head dimensions
    // the differences in response due to the different head shapes gradually
    // disappear and the head-length loss factors consequently tend to unity in
    // all cases."
    //
    // With the ordering the paper also states: rounding makes the fluctuations
    // "die down more quickly", so at the right-hand edge of the table the sharp
    // column is still further from unity than the rounded ones.
    const auto& last = tape::kHeadLengthLossFactor[tape::kHeadLengthRows - 1];
    for (std::size_t j = 0; j < last.size(); ++j)
    {
        INFO("column " << j << " at theta = 10: " << last[j]);
        REQUIRE(last[j] == Approx(1.0).margin(0.03));
    }

    double sharpest = 0.0, roundest = 0.0;
    for (int i = tape::kHeadLengthRows - 10; i < tape::kHeadLengthRows; ++i)
    {
        const auto& row = tape::kHeadLengthLossFactor[static_cast<std::size_t>(i)];
        sharpest = std::max(sharpest, std::abs(row[0] - 1.0));
        roundest = std::max(roundest, std::abs(row[5] - 1.0));
    }
    INFO("last decade: sharp " << sharpest << ", rounded " << roundest);
    REQUIRE(sharpest > roundest);
    REQUIRE(sharpest > 0.03);     // still visibly rippling, as figure 5 shows
}

TEST_CASE("the interpolant hits the tabulated points and holds the edges",
          "[headlength]")
{
    // Exactly on a grid point it must return that point, or the table is being
    // smoothed away by its own accessor.
    REQUIRE(tape::headLengthLossFactor(0.0, 0.1) == Approx(0.701));
    REQUIRE(tape::headLengthLossFactor(0.0, 0.7) == Approx(1.195));
    REQUIRE(tape::headLengthLossFactor(M_PI / 2.0, 10.0) == Approx(1.000));
    REQUIRE(tape::headLengthLossFactor(M_PI / 8.0, 2.0) == Approx(0.912));

    // Between them it interpolates rather than jumping.
    const double mid = tape::headLengthLossFactor(0.0, 0.65);
    REQUIRE(mid == Approx(0.5 * (1.179 + 1.195)).margin(1.0e-9));

    // Outside, it holds the edge -- and above theta = 10 that edge IS the
    // answer, because the factor has converged on unity.
    REQUIRE(tape::headLengthLossFactor(0.0, 40.0)
            == Approx(tape::headLengthLossFactor(0.0, 10.0)));
    REQUIRE(tape::headLengthLossFactor(0.0, 0.001)
            == Approx(tape::headLengthLossFactor(0.0, 0.1)));

    // A rounding beyond the tabulated range clamps rather than extrapolating
    // into nonsense.
    REQUIRE(tape::headLengthLossFactor(10.0, 3.0)
            == Approx(tape::headLengthLossFactor(M_PI / 2.0, 3.0)));
}

TEST_CASE("the wavelength form puts the head length in wavelengths",
          "[headlength]")
{
    // theta is L/lambda, so this is where faceLengthMetres enters -- and it must
    // scale with speed like everything else in section 4.3.
    constexpr double L = 14.6e-3;
    const double hz = 0.381 / (L / 2.0);          // theta = 2 at 15 ips
    REQUIRE(tape::headLengthLossFactor(tape::waveNumber(hz, 0.381), L, 0.0)
            == Approx(tape::headLengthLossFactor(0.0, 2.0)).margin(1.0e-6));

    // Halve the speed and halve the frequency: the same answer, which is the
    // standing invariant of the whole loss chain.
    REQUIRE(tape::headLengthLossFactor(tape::waveNumber(hz, 0.381), L, 0.0)
            == Approx(tape::headLengthLossFactor(
                          tape::waveNumber(0.5 * hz, 0.1905), L, 0.0)));

    // A stopped tape or a head with no length is not a division by zero.
    REQUIRE(tape::headLengthLossFactor(0.0, L, 0.0) == Approx(0.701));
    REQUIRE(tape::headLengthLossFactor(100.0, 0.0, 0.0) == Approx(0.701));
}

TEST_CASE("the table is smooth, which is what catches a mistyped digit",
          "[headlength][teeth]")
{
    // The properties above constrain the monotonic column and the peak, and
    // leave the oscillating middle of the table unguarded -- an injected 0.040
    // error at theta = 6 passed every one of them.
    //
    // What guards it is smoothness. `H` is an analytic function of theta sampled
    // every 0.1, so adjacent values cannot jump. Measured on the transcription,
    // the largest real step is 0.150 across the steep rise below theta = 0.3,
    // 0.050 above theta = 1, and 0.028 above theta = 3. The bounds below are
    // those with a little room, which is tight enough that a single wrong digit
    // in the third decimal place of the oscillating region shows up.
    struct Band { double from; double limit; };
    const Band bands[] = { { 0.1, 0.16 }, { 1.0, 0.06 }, { 3.0, 0.035 } };

    for (const auto& band : bands)
    {
        const auto first = static_cast<std::size_t>(band.from / 0.1) - 1;
        for (std::size_t j = 0; j < 6; ++j)
        {
            for (std::size_t i = first; i + 1 < tape::kHeadLengthRows; ++i)
            {
                const double a = tape::kHeadLengthLossFactor[i][j];
                const double b = tape::kHeadLengthLossFactor[i + 1][j];
                INFO("column " << j << ", theta " << 0.1 * (i + 1) << " -> "
                     << 0.1 * (i + 2) << ": " << a << " -> " << b);
                REQUIRE(std::abs(b - a) < band.limit);
            }
        }
    }

    // AND CURVATURE, WHICH IS THE ONE THAT ACTUALLY FINDS TYPOS. A wrong digit
    // is a spike, and a spike shows up in the second difference at twice its own
    // size while barely moving the first. Injecting a 0.030 error into the
    // middle of the table passed the step check above and fails this one.
    //
    // The real table's largest second difference is 0.028, on the steep rise at
    // theta = 0.3, and 0.014 above theta = 1. The bound is 0.035, which is
    // comfortably above the data and comfortably below a mistyped third decimal.
    for (std::size_t j = 0; j < 6; ++j)
    {
        for (std::size_t i = 1; i + 1 < tape::kHeadLengthRows; ++i)
        {
            const double a = tape::kHeadLengthLossFactor[i - 1][j];
            const double b = tape::kHeadLengthLossFactor[i][j];
            const double c = tape::kHeadLengthLossFactor[i + 1][j];
            const double curvature = std::abs(a - 2.0 * b + c);
            INFO("column " << j << " at theta " << 0.1 * (i + 1)
                 << ": " << a << ", " << b << ", " << c
                 << " -> curvature " << curvature);
            REQUIRE(curvature < 0.035);
        }
    }
}
