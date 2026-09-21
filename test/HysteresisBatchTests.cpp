// Cross-track hysteresis (DESIGN.md section 4.2).
//
// The batch exists ONLY to be faster. It is the same physics, the same stock
// and the same ladder rung as Hysteresis.h, so the tests here are not about
// magnetic behaviour -- HysteresisTests.cpp owns that -- but about the batch
// being indistinguishable from the scalar solver it replaces.
//
// THE ASSERTION IS BIT-EXACT EQUALITY, not a tolerance, and that is a choice
// worth naming. The batch performs the same operations in the same order on
// the same values; nothing about lockstep licenses a different answer. A
// tolerance here would let a genuine transcription error -- a swapped
// midpoint, a dropped 0.5 -- hide underneath it, which is exactly the class of
// mistake a hand transcription makes.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/Hysteresis.h>
#include <chalkwalk/tape/HysteresisBatch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tape = chalkwalk::tape;

namespace
{
    constexpr double kRate = 768000.0;  // the engine rate at 16x (DESIGN 4.2)

    // Four lanes driven by DIFFERENT signals, which is the point: a batch that
    // fed every lane the same input would pass while silently sharing state
    // between lanes.
    double laneSignal(std::size_t lane, int n, double amplitude)
    {
        const double hz = 500.0 * static_cast<double>(lane + 1);
        const double phase = 0.37 * static_cast<double>(lane);
        return amplitude * std::sin(2.0 * M_PI * hz * n / kRate + phase);
    }
}

TEST_CASE("the batch matches the scalar solver exactly, lane by lane",
          "[hysteresis][batch]")
{
    const auto stock = tape::TapeStock::ferricOxide();
    const double amplitude = 6.0e5;

    for (const auto solver : {tape::Solver::RungeKutta2InField,
                              tape::Solver::RungeKutta4InField})
    {
        for (const auto evaluation : {tape::Evaluation::Exact,
                                      tape::Evaluation::Tabulated})
        {
            tape::HysteresisBatch batch;
            REQUIRE(batch.prepare(kRate, stock, solver, evaluation));

            std::array<tape::Hysteresis, tape::HysteresisBatch::kLanes> scalar;
            for (auto& h : scalar)
                h.prepare(kRate, stock, solver, evaluation);

            std::array<double, tape::HysteresisBatch::kLanes> in {}, out {};

            for (int n = 0; n < 4000; ++n)
            {
                for (std::size_t i = 0; i < tape::HysteresisBatch::kLanes; ++i)
                    in[i] = laneSignal(i, n, amplitude);

                batch.process(in.data(), out.data());

                for (std::size_t i = 0; i < tape::HysteresisBatch::kLanes; ++i)
                {
                    const double wanted = scalar[i].process(in[i]);
                    INFO("solver " << static_cast<int>(solver)
                         << " evaluation " << static_cast<int>(evaluation)
                         << " lane " << i << " sample " << n);
                    REQUIRE(out[i] == wanted);
                }
            }
        }
    }
}

TEST_CASE("a still lane is left alone while its neighbours are driven",
          "[hysteresis][batch]")
{
    // The batch gives up the scalar solver's early-out on a zero field change,
    // because lanes cannot diverge. What it must NOT give up is the result:
    // an idle lane has to hold its magnetisation exactly while the lanes
    // beside it are being driven hard.
    //
    // This has teeth for the lockstep itself. If a lane ever read another
    // lane's deltaH or direction -- the easiest possible indexing slip -- the
    // idle lane would move, and nothing in the equality test above would
    // necessarily catch it, since there every lane is moving anyway.
    const auto stock = tape::TapeStock::ferricOxide();

    tape::HysteresisBatch batch;
    REQUIRE(batch.prepare(kRate, stock, tape::Solver::RungeKutta2InField,
                          tape::Evaluation::Exact));

    std::array<double, tape::HysteresisBatch::kLanes> in {}, out {};

    // Drive lane 0 up to a real magnetisation first, then hold it still.
    for (int n = 0; n < 500; ++n)
    {
        in[0] = laneSignal(0, n, 6.0e5);
        for (std::size_t i = 1; i < tape::HysteresisBatch::kLanes; ++i)
            in[i] = laneSignal(i, n, 6.0e5);
        batch.process(in.data(), out.data());
    }

    const double held = batch.magnetisation(0);
    REQUIRE(std::abs(held) > 0.0);  // it really was magnetised

    const double lastField = in[0];
    for (int n = 500; n < 1500; ++n)
    {
        in[0] = lastField;  // still
        for (std::size_t i = 1; i < tape::HysteresisBatch::kLanes; ++i)
            in[i] = laneSignal(i, n, 6.0e5);
        batch.process(in.data(), out.data());

        INFO("sample " << n << ": lane 0 moved from " << held
             << " to " << batch.magnetisation(0));
        REQUIRE(batch.magnetisation(0) == held);
    }
}

TEST_CASE("Newton runs a fixed number of iterations rather than being refused",
          "[hysteresis][batch][newton]")
{
    // THE SCALAR SOLVER'S PROBLEM IS THE BATCH'S OPPORTUNITY.
    //
    // Newton iterates to a tolerance, so its count varies per sample and it
    // cannot branch per lane in lockstep. But a vector runs to the slowest lane
    // regardless, so the iterations a converged lane "wastes" are already paid
    // for. Fixing the count removes the convergence test, the per-lane branch
    // and the early exit together.
    //
    // What must be true is that a fixed count reaches the same answer the
    // converged scalar solver does. Not bit-exact -- the scalar breaks out
    // early and the batch takes further steps that move it by less than the
    // tolerance -- so this asserts agreement to well inside the solve
    // tolerance, and separately that MORE iterations do not move it, which is
    // what "converged" actually means.
    const auto stock = tape::TapeStock::ferricOxide();
    const double amplitude = 6.0e5;

    for (const auto evaluation : {tape::Evaluation::Exact,
                                  tape::Evaluation::Tabulated})
    {
        // FOUR, AND THE NUMBER IS MEASURED. Convergence is quadratic, and
        // the worst disagreement with the converged scalar solver over 20000
        // samples at 6e5 A/m runs 3.7e-1, 3.0e-3, 2.0e-7, 4.5e-15 of Ms for
        // one through four iterations, then flattens at the floor. Four is
        // where it stops improving; three is 2e-7 out and one is off by 37% of
        // saturation, so the one-shot Newton that ROADMAP.md floated as a
        // candidate is not one at this amplitude.
        tape::HysteresisBatch four, eight;
        REQUIRE(four.prepare(kRate, stock, tape::Solver::NewtonRaphson, evaluation, 4));
        REQUIRE(eight.prepare(kRate, stock, tape::Solver::NewtonRaphson, evaluation, 8));

        std::array<tape::Hysteresis, tape::HysteresisBatch::kLanes> scalar;
        for (auto& h : scalar)
            h.prepare(kRate, stock, tape::Solver::NewtonRaphson, evaluation);

        std::array<double, tape::HysteresisBatch::kLanes> in {}, a {}, b {};
        double worstScalar = 0.0, worstIteration = 0.0;

        for (int n = 0; n < 4000; ++n)
        {
            for (std::size_t i = 0; i < tape::HysteresisBatch::kLanes; ++i)
                in[i] = laneSignal(i, n, amplitude);

            four.process(in.data(), a.data());
            eight.process(in.data(), b.data());

            for (std::size_t i = 0; i < tape::HysteresisBatch::kLanes; ++i)
            {
                const double wanted = scalar[i].process(in[i]);
                worstScalar = std::max(worstScalar, std::abs(a[i] - wanted));
                worstIteration = std::max(worstIteration, std::abs(a[i] - b[i]));
            }
        }

        // Relative to saturation, which is the scale the tolerance is defined
        // against in the scalar solver.
        const double ms = stock.saturationMagnetisation;
        INFO("evaluation " << static_cast<int>(evaluation)
             << ": vs scalar " << worstScalar / ms
             << " of Ms, 4 vs 8 iterations " << worstIteration / ms);
        REQUIRE(worstScalar / ms < 1.0e-12);
        REQUIRE(worstIteration / ms < 1.0e-12);
    }
}

TEST_CASE("the batch still refuses a solver it cannot run in lockstep",
          "[hysteresis][batch]")
{
    // The time-domain RK4 needs a field RATE estimate, which the in-field rungs
    // do not carry and this batch does not keep. Refused rather than silently
    // substituted: quietly handing the caller a different rung than it asked
    // for would be a physics change disguised as an optimisation.
    tape::HysteresisBatch batch;
    REQUIRE_FALSE(batch.prepare(kRate, tape::TapeStock::ferricOxide(),
                                tape::Solver::RungeKutta4,
                                tape::Evaluation::Exact));
}
