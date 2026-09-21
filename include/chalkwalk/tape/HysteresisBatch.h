#pragma once

// Cross-track hysteresis: N tracks solved in lockstep (DESIGN.md section 4.2).
//
// RECORD SIDE (PRINCIPLES section 2). This is the same physics as Hysteresis.h
// and commits to the same medium; it exists only to be faster, and a result
// that differs from the scalar solver is a bug rather than a trade.
//
// WHY ACROSS TRACKS AND NOT ACROSS SAMPLES. The solver is serial in its own
// state -- M(n) needs M(n-1) -- so there is no parallelism along the time axis
// to find, at any oversampling factor. But sixteen armed tracks are sixteen
// INDEPENDENT integrations sharing one bias oscillator and one stock, and that
// is a real axis. It is also the axis the machine already has: PRINCIPLES
// section 3 puts the transport and the bias on the machine rather than the
// track, so every lane sees the same tau(t) and the same constants by
// construction, and the only per-lane state is M and the previous field.
//
// TWO EFFECTS, AND THE SMALLER ONE IS THE OBVIOUS ONE. Lane width is worth
// whatever the target's vector registers give. The larger effect is that four
// lanes are four INDEPENDENT dependency chains: a single track's step is
// k1 -> k2 -> M, each waiting on the last, so the scalar solver is
// latency-bound rather than throughput-bound in exactly the way
// PlaybackFilter's single accumulator was.
//
// RK2 AND RK4 IN FIELD ONLY, AND NEWTON IS EXCLUDED ON PURPOSE. Lockstep
// requires every lane to do the same work. The in-field Runge-Kutta rungs do a
// fixed two or four slope evaluations per step and qualify; Newton iterates to
// convergence and its iteration count varies per lane, so a batch would run
// every lane to the slowest one's count and change results at the tolerance.
// prepare() rejects it rather than silently substituting.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <chalkwalk/tape/Hysteresis.h>
#include <chalkwalk/dsp/Simd.h>


#include <array>
#include <cmath>
#include <cstddef>

namespace chalkwalk::tape
{
    class HysteresisBatch
    {
    public:
        // EIGHT LANES, AND THE NUMBER IS MEASURED RATHER THAN ARGUED FROM
        // REGISTER WIDTH. It was four on the reasoning that four doubles is a
        // whole AVX register; that reasoning is wrong here, because the slope
        // loop does not vectorise at all (see slopes()) and the win is
        // INSTRUCTION-LEVEL PARALLELISM -- independent dependency chains the
        // out-of-order engine can overlap. Swept against the scalar solver:
        //
        //     kLanes      2      3      4      6      8     12     16
        //     speedup  0.99x  1.35x  1.61x  1.99x  2.20x  1.79x  1.91x
        //
        // Two buys nothing: the core already overlaps two chains unaided and
        // the batch's indexing costs exactly what it adds, which is why a
        // listener with two armed tracks saw no improvement and why the deck
        // keeps a scalar path below three. It turns over past eight, on
        // register pressure.
        //
        // Sixteen tracks is two batches, eight is one.
        static constexpr std::size_t kLanes = 8;

        // MESSAGE THREAD ONLY: allocates the Langevin table.
        //
        // Returns false for a solver that cannot be run in lockstep, leaving
        // the batch unprepared. The caller falls back to the scalar solver
        // rather than being given a different answer than it asked for.
        [[nodiscard]] bool prepare(double sampleRate,
                                   TapeStock stock = TapeStock::ferricOxide(),
                                   Solver solver = Solver::RungeKutta2InField,
                                   Evaluation evaluation = Evaluation::Exact,
                                   int iterations = 3)
        {
            if (solver != Solver::RungeKutta2InField
                && solver != Solver::RungeKutta4InField
                && solver != Solver::NewtonRaphson)
                return false;

            // NEWTON IS ACCEPTED HERE AND REFUSED IN THE SCALAR SENSE, and the
            // difference is the whole reason it is worth batching.
            //
            // The scalar solver iterates to a tolerance, so its iteration count
            // varies per sample -- 2.86 on average at 8x. A batch cannot branch
            // per lane, which looks like a disqualification and is in fact the
            // opportunity: in lockstep the vector runs to the SLOWEST lane
            // regardless, so the iterations a converged lane "wastes" are
            // already paid for. Fixing the count removes the convergence test,
            // the per-lane branch and the early exit all at once, and turns a
            // variable cost into a constant one.
            //
            // The count is the caller's, because it is an accuracy dial rather
            // than an implementation detail.
            //
            // MEASURED, AND THE ANSWER IS THAT BATCHING DOES NOT RESCUE NEWTON.
            // The idea is sound in the abstract -- if lanes run in lockstep the
            // iterations a converged lane wastes are already paid for -- but it
            // rests on the win being WIDTH, and here it is ILP (see slopes()).
            // Nothing rides free, so fixing the count at the worst case instead
            // of the scalar solver's 2.86 average simply adds work.
            //
            // Convergence against the converged scalar solver, worst over 20000
            // samples at 6e5 A/m, with the batched cost per lane beside it:
            //
            //   iterations   worst error      batch      vs scalar
            //       1        3.7e-01 of Ms      --       unusable
            //       2        3.0e-03            69.8 ns    1.17x
            //       3        2.0e-07            95.2 ns    0.86x
            //       4        4.5e-15           119.7 ns    0.68x
            //
            // Convergence is quadratic and flattens at four. The usable settings
            // are the slow ones: two is fast and 50 dB out, three is ample at
            // -134 dB of saturation and already slower than the scalar Newton it
            // would replace. Against rk2-field tabulated at 22.5 ns batched,
            // Newton at its cheapest usable setting is over four times the cost.
            //
            // KEPT ANYWAY, for two reasons. It records the negative result so
            // the idea is not re-derived from first principles a third time; and
            // Newton has the most arithmetic per branch of any rung, so it is
            // the one that would gain most if the slope loop is ever made to
            // vectorise properly (ROADMAP.md). If that lands, re-measure this
            // before assuming the ranking still holds.
            //
            // ROADMAP.md's "one iteration agrees to about ten significant
            // figures at 220 Hz" does NOT generalise: at this amplitude one
            // iteration is off by 37% of saturation.
            newtonIterations_ = iterations < 1 ? 1 : iterations;

            stock_ = stock;
            solver_ = solver;
            evaluation_ = evaluation;

            if (evaluation_ == Evaluation::Tabulated && !table_.isBuilt())
                table_.build();

            reset();
            return true;
        }

        void reset() noexcept
        {
            magnetisation_.fill(0.0);
            previousField_.fill(0.0);
        }

        // One sample for every lane. `fields` and `out` are kLanes long.
        //
        // NO EARLY-OUT ON A STILL FIELD, and that is the one thing this gives
        // up. The scalar solver returns immediately when deltaH is zero, which
        // is the common case for an idle armed track. A batch cannot: the lanes
        // would diverge. The zero-field lane instead computes a slope it
        // multiplies by a zero deltaH, which is the same answer at full cost.
        // With four armed tracks that is a loss; the batch is for the case
        // where tracks are actually being written, which is the case that does
        // not fit.
        // NO CHALKWALK_DSP_MULTIVERSION HERE, AND THAT IS A MEASUREMENT RATHER
        // THAN AN OVERSIGHT. Cloning this for AVX2 changed the batch figure by
        // nothing at all (1.31x either way), because the slope loop does not
        // vectorise in the first place -- see slopes() below. Wider registers
        // cannot widen a loop the vectoriser declined. The attribute was tried
        // and removed rather than left as a no-op that costs binary size and
        // implies a win.
        void process(const double* fields, double* out) noexcept
        {
            if (evaluation_ == Evaluation::Tabulated)
                processWith(fields, out, table_);
            else
                processWith(fields, out, ExactLangevin{});
        }

        [[nodiscard]] double magnetisation(std::size_t lane) const noexcept
        {
            return magnetisation_[lane];
        }

        // LANES ARE NOT TRACKS, and this pair is what keeps it that way.
        //
        // The caller loads a lane from whichever track it is running, takes one
        // engine sample's worth of steps, and stores the result back to that
        // track's own scalar solver, which stays the authority. So the lane
        // assignment can be repacked as freely as the arming changes, and a
        // batch that is only part full costs nothing to leave idle -- an unused
        // lane is loaded with a still field and integrates a zero deltaH, which
        // is the same answer the scalar solver's early-out gives, at full price.
        void setLane(std::size_t lane, double m, double field) noexcept
        {
            magnetisation_[lane] = m;
            previousField_[lane] = field;
        }

    private:
        template <class Langevin>
        void processWith(const double* fields, double* out, const Langevin& lang) noexcept
        {
            std::array<double, kLanes> deltaH {};
            std::array<double, kLanes> direction {};
            std::array<double, kLanes> midField {};

            for (std::size_t i = 0; i < kLanes; ++i)
            {
                deltaH[i] = fields[i] - previousField_[i];
                // A SELECT, NOT A BRANCH. Every conditional in this file is
                // written as arithmetic on a comparison so the lanes stay in
                // lockstep; a branch here would serialise them.
                direction[i] = deltaH[i] >= 0.0 ? 1.0 : -1.0;
                midField[i] = 0.5 * (previousField_[i] + fields[i]);
            }

            std::array<double, kLanes> k1 {};
            slopes(magnetisation_.data(), previousField_.data(), direction.data(),
                   k1.data(), lang);

            if (solver_ == Solver::NewtonRaphson)
            {
                stepNewton(fields, deltaH.data(), direction.data(), lang);
                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    previousField_[i] = fields[i];
                    out[i] = magnetisation_[i];
                }
                return;
            }

            if (solver_ == Solver::RungeKutta2InField)
            {
                std::array<double, kLanes> mid {};
                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    k1[i] *= deltaH[i];
                    mid[i] = magnetisation_[i] + 0.5 * k1[i];
                }

                std::array<double, kLanes> k2 {};
                slopes(mid.data(), midField.data(), direction.data(), k2.data(), lang);

                // GUARDED AS THE SCALAR RUNG IS: divergence allowed, infinity
                // not. See `Hysteresis::finiteOrSaturated` for why this does
                // NOT bound magnitude. The two solvers must agree bit for bit,
                // so a guard added to one is added to both.
                const double ms = stock_.saturationMagnetisation;
                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    const double v = magnetisation_[i] + k2[i] * deltaH[i];
                    magnetisation_[i] = std::isfinite(v)
                                      ? v : (magnetisation_[i] < 0.0 ? -ms : ms);
                }
            }
            else
            {
                // RK4 in the field: four slope evaluations over the interval
                // [previousField, field], which is the same ladder rung as the
                // scalar solver's stepRungeKuttaInField.
                std::array<double, kLanes> m2 {}, m3 {}, m4 {}, k2 {}, k3 {}, k4 {};

                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    k1[i] *= deltaH[i];
                    m2[i] = magnetisation_[i] + 0.5 * k1[i];
                }
                slopes(m2.data(), midField.data(), direction.data(), k2.data(), lang);

                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    k2[i] *= deltaH[i];
                    m3[i] = magnetisation_[i] + 0.5 * k2[i];
                }
                slopes(m3.data(), midField.data(), direction.data(), k3.data(), lang);

                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    k3[i] *= deltaH[i];
                    m4[i] = magnetisation_[i] + k3[i];
                }
                slopes(m4.data(), fields, direction.data(), k4.data(), lang);

                // THE ASSOCIATION IS THE SCALAR SOLVER'S, DELIBERATELY.
                // (k1 + 2k2 + 2k3 + k4)/6 is the same number in algebra and a
                // different one in floating point: written that way this
                // disagreed with Hysteresis.h by one ulp, which the bit-exact
                // batch test caught immediately. Sum in the same order and to
                // the same intermediate values as stepRungeKuttaInField.
                const double ms = stock_.saturationMagnetisation;
                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    k4[i] *= deltaH[i];
                    const double v = magnetisation_[i] + k1[i] / 6.0
                                   + k2[i] / 3.0 + k3[i] / 3.0 + k4[i] / 6.0;
                    magnetisation_[i] = std::isfinite(v)
                                      ? v : (magnetisation_[i] < 0.0 ? -ms : ms);
                }
            }

            for (std::size_t i = 0; i < kLanes; ++i)
            {
                previousField_[i] = fields[i];
                out[i] = magnetisation_[i];
            }
        }

        // Trapezoidal in the field, solved by a FIXED number of Newton steps.
        //
        // Transcribed from Hysteresis.h's stepNewton with exactly one change:
        // the convergence test and its `break` are gone. Everything the scalar
        // version does for safety is kept, because none of it is about
        // convergence -- the slope guard against a bad stock, the step damping
        // that stops Newton cycling across the corner where delta_M switches,
        // and the hard saturation clamp are all still here and all still
        // written as clamps rather than branches out of the loop.
        template <class Langevin>
        void stepNewton(const double* field, const double* deltaH,
                        const double* direction, const Langevin& lang) noexcept
        {
            std::array<double, kLanes> previous {}, base {}, m {};
            std::array<double, kLanes> slope {};

            for (std::size_t i = 0; i < kLanes; ++i)
                previous[i] = magnetisation_[i];

            slopes(previous.data(), previousField_.data(), direction, slope.data(), lang);

            for (std::size_t i = 0; i < kLanes; ++i)
            {
                base[i] = previous[i] + 0.5 * deltaH[i] * slope[i];
                // Explicit Euler as the opening guess, as in the scalar solver.
                m[i] = previous[i] + deltaH[i] * slope[i];
            }

            const double maxStep = kMaxStepFraction * stock_.saturationMagnetisation;
            std::array<double, kLanes> g {}, j {};

            for (int iteration = 0; iteration < newtonIterations_; ++iteration)
            {
                slopes(m.data(), field, direction, g.data(), lang);
                jacobians(m.data(), field, direction, j.data(), lang);

                for (std::size_t i = 0; i < kLanes; ++i)
                {
                    const double residual = m[i] - base[i] - 0.5 * deltaH[i] * g[i];
                    double denominator = 1.0 - 0.5 * deltaH[i] * j[i];

                    constexpr double kMinSlope = 1.0e-6;
                    if (denominator > -kMinSlope && denominator < kMinSlope)
                        denominator = denominator < 0.0 ? -kMinSlope : kMinSlope;

                    double step = residual / denominator;
                    if (step > maxStep) step = maxStep;
                    else if (step < -maxStep) step = -maxStep;

                    m[i] -= step;
                }
            }

            const double ms = stock_.saturationMagnetisation;
            for (std::size_t i = 0; i < kLanes; ++i)
                magnetisation_[i] = m[i] < -ms ? -ms : (m[i] > ms ? ms : m[i]);
        }

        // d(dM/dH)/dM for every lane, the Newton denominator. Transcribed from
        // magnetisationSlopeJacobianWith.
        template <class Langevin>
        void jacobians(const double* M, const double* H, const double* direction,
                       double* out, const Langevin& lang) const noexcept
        {
            const double alpha = stock_.meanFieldCoupling;
            const double a = stock_.anhystericShape;
            const double ms = stock_.saturationMagnetisation;
            const double c = stock_.susceptibilityRatio;
            const double k = stock_.coercivity;
            const double msOverA = ms / a;
            const double dQdM = alpha / a;

            for (std::size_t i = 0; i < kLanes; ++i)
            {
                const double q = (H[i] + alpha * M[i]) / a;
                const double lp = lang.lPrime(q);
                const double ld = lang.lDoublePrime(q);

                const double gap = ms * lang.l(q) - M[i];
                const double dGapdM = ms * lp * dQdM - 1.0;

                const double deltaS = direction[i] >= 0.0 ? 1.0 : -1.0;
                const double deltaM = (deltaS > 0.0) == (gap > 0.0) ? 1.0 : 0.0;

                double pinning = (1.0 - c) * deltaS * k - alpha * gap;
                constexpr double kMinPinning = 1.0e-12;
                if (pinning > -kMinPinning && pinning < kMinPinning)
                    pinning = pinning < 0.0 ? -kMinPinning : kMinPinning;

                const double irreversible = ((1.0 - c) * deltaM * gap) / pinning;
                const double reversible = c * msOverA * lp;
                const double coupling = 1.0 - c * alpha * msOverA * lp;

                const double dIrreversible = (1.0 - c) * deltaM * dGapdM
                                           * ((1.0 - c) * deltaS * k)
                                           / (pinning * pinning);
                const double dReversible = c * msOverA * ld * dQdM;
                const double dCoupling = -c * alpha * msOverA * ld * dQdM;

                out[i] = ((dIrreversible + dReversible) * coupling
                          - (irreversible + reversible) * dCoupling)
                       / (coupling * coupling);
            }
        }

        // dM/dH for every lane. The scalar original is magnetisationSlopeWith
        // in Hysteresis.h, and this must agree with it exactly -- the batch
        // tests assert that lane by lane rather than trusting the transcription.
        template <class Langevin>
        void slopes(const double* M, const double* H, const double* direction,
                    double* outSlope, const Langevin& lang) const noexcept
        {
            const double alpha = stock_.meanFieldCoupling;
            const double a = stock_.anhystericShape;
            const double ms = stock_.saturationMagnetisation;
            const double c = stock_.susceptibilityRatio;
            const double k = stock_.coercivity;
            const double msOverA = ms / a;

            std::array<double, kLanes> q {}, lp {}, lv {};

            for (std::size_t i = 0; i < kLanes; ++i)
                q[i] = (H[i] + alpha * M[i]) / a;

            // The two table reads are the only gathers here, and they are kept
            // in their own loops so the arithmetic around them still
            // vectorises even where the gather does not.
            for (std::size_t i = 0; i < kLanes; ++i)
                lp[i] = lang.lPrime(q[i]);
            for (std::size_t i = 0; i < kLanes; ++i)
                lv[i] = lang.l(q[i]);

            for (std::size_t i = 0; i < kLanes; ++i)
            {
                const double gap = ms * lv[i] - M[i];
                const double deltaS = direction[i] >= 0.0 ? 1.0 : -1.0;
                const double deltaM = (deltaS > 0.0) == (gap > 0.0) ? 1.0 : 0.0;

                double pinning = (1.0 - c) * deltaS * k - alpha * gap;
                constexpr double kMinPinning = 1.0e-12;
                // Guarded because a stock is data and data can be wrong, and a
                // division by zero would reach the audio thread as a NaN
                // written permanently onto the tape.
                //
                // THIS LOOP DOES NOT VECTORISE, and rewriting the guard as a
                // branchless ternary does not change that -- it was tried,
                // -fopt-info-vec still reported nothing for this loop, and the
                // measurement got slightly worse rather than better. The
                // blocker is upstream: lPrime() and l() carry their own
                // out-of-range branches, so the two lookup loops above are
                // "unsupported control flow" and this one inherits scalar
                // inputs. Recorded so the ternary is not reinvented as an
                // obvious improvement.

                const double irreversible = ((1.0 - c) * deltaM * gap) / pinning;
                const double reversible = c * msOverA * lp[i];
                const double coupling = 1.0 - c * alpha * msOverA * lp[i];

                outSlope[i] = (irreversible + reversible) / coupling;
            }
        }

        std::array<double, kLanes> magnetisation_ {};
        std::array<double, kLanes> previousField_ {};

        TapeStock stock_ = TapeStock::ferricOxide();
        Solver solver_ = Solver::RungeKutta2InField;
        int newtonIterations_ = 3;
        // The same damping cap the scalar solver uses, and it must STAY the
        // same: see Hysteresis.h's note on why the non-convergence curve is not
        // monotonic, so this is not a "safer is smaller" dial.
        static constexpr double kMaxStepFraction = 1.0;
        Evaluation evaluation_ = Evaluation::Exact;
        LangevinTable table_;
    };
}
