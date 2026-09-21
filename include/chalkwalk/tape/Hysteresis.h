#pragma once

// Jiles-Atherton magnetic hysteresis (SOURCES section 5, DESIGN.md section 4.2).
//
// RECORD SIDE (PRINCIPLES section 2). What this computes is the magnetisation
// that gets WRITTEN TO THE TAPE. It runs once, when signal passes the record
// head, and its result is stored -- so a change here alters what is committed
// and cannot be undone by playing the tape back differently.
//
// That is also what makes sixteen tracks affordable: this cost is paid per
// ARMED track, not per playing track.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <algorithm>
#include <cmath>
#include <vector>

namespace chalkwalk::tape
{
    // Below this, the closed forms are replaced by their series expansions.
    //
    // THE THRESHOLD IS SET BY CANCELLATION, NOT BY THE SINGULARITY, and the
    // difference between those two ideas is four orders of magnitude of
    // accuracy. Both closed forms subtract two large nearly-equal quantities:
    // L is coth(x) - 1/x, of order 1/x each against an answer of order x/3;
    // L' is 1/x^2 - coth^2(x) + 1, of order 1/x^2 each against an answer of
    // 1/3. Either way about 3/x^2 worth of significance is destroyed, so at
    // x = 2e-3 six digits are already gone -- long before anything is 0/0.
    //
    // The source paper uses 1e-4, which is roughly where the expression becomes
    // undefined rather than where it becomes inaccurate. Measured against
    // 60-digit references, 1e-4 leaves a relative error of 1.3e-8; at 2e-2 both
    // branches reach about 5e-13. That noise is not academic: in the rate
    // equation a 1e-8 error on L' lands as +/-1.8e-4 on a value of 1.8e6, which
    // is invisible to every structural test and swamps a numerical derivative
    // taken for comparison.
    //
    // Raising the threshold REQUIRES the extra series terms below. One term
    // (x/3) is only good to x^2/5 relative, which at 2e-2 is 8e-5 -- far worse
    // than what it replaces.
    inline constexpr double kSmallArgument = 2.0e-2;

    // The Langevin function, L(x) = coth(x) - 1/x.
    //
    // This is the shape of the ANHYSTERETIC curve -- what the magnetisation
    // would be with no domain-wall pinning. It is odd, it saturates at 1, and
    // near the origin it is x/3.
    //
    // Saturating rather than clipping is why tape compresses.
    [[nodiscard]] inline double langevin(double x) noexcept
    {
        if (std::abs(x) < kSmallArgument)
        {
            // x/3 - x^3/45 + 2x^5/945; the next term is x^7/4725, which at the
            // threshold is 4e-14 relative.
            const double x2 = x * x;
            return x * (1.0 / 3.0 + x2 * (-1.0 / 45.0 + x2 * (2.0 / 945.0)));
        }
        return 1.0 / std::tanh(x) - 1.0 / x;
    }

    // dL/dx = 1/x^2 - coth^2(x) + 1. Even, as the derivative of an odd function
    // must be, and 1/3 at the origin.
    [[nodiscard]] inline double langevinPrime(double x) noexcept
    {
        if (std::abs(x) < kSmallArgument)
        {
            // 1/3 - x^2/15 + 2x^4/189; next term x^6/675, 3e-13 at threshold.
            const double x2 = x * x;
            return 1.0 / 3.0 + x2 * (-1.0 / 15.0 + x2 * (2.0 / 189.0));
        }
        const double coth = 1.0 / std::tanh(x);
        return 1.0 / (x * x) - coth * coth + 1.0;
    }


    // The Langevin second derivative needs its OWN threshold, a hundred times
    // larger than kSmallArgument, and this is not fussiness.
    //
    // The closed form's terms are of order 2/x^3 while the answer is of order
    // 2x/15, so evaluating it cancels about 15/x^4 worth of significance. At
    // x = 1e-4 that is seventeen orders -- more than a double has -- and the
    // closed form comes back wrong by a factor of twenty. Reusing
    // kSmallArgument here is the obvious thing to do and it lands in that hole.
    //
    // L'' cancels about 15/x^4 -- worse than L and L', because each derivative
    // order raises the power of the diverging terms -- so its threshold is
    // larger again. Measured crossover with the three-term series below: both
    // branches reach roughly 1e-9 at 5e-2.
    inline constexpr double kSmallArgumentSecond = 5.0e-2;

    // d2L/dx2 = -2/x^3 - 2coth(x) + 2coth^3(x); near zero, -2x/15 + 8x^3/189.
    //
    // Used only by the Newton-Raphson rung's Jacobian. Worth knowing when
    // judging how accurate it must be: the Jacobian sets the RATE at which
    // Newton converges, not the root it converges to, so an error here costs
    // iterations rather than correctness (DESIGN.md section 4.2).
    [[nodiscard]] inline double langevinDoublePrime(double x) noexcept
    {
        if (std::abs(x) < kSmallArgumentSecond)
        {
            // -2x/15 + 8x^3/189 - 2x^5/225.
            const double x2 = x * x;
            return x * (-2.0 / 15.0 + x2 * (8.0 / 189.0 + x2 * (-2.0 / 225.0)));
        }
        const double coth = 1.0 / std::tanh(x);
        return -2.0 / (x * x * x) - 2.0 * coth + 2.0 * coth * coth * coth;
    }

    // A NOTE FOR WHOEVER OPTIMISES THIS.
    //
    // The source paper approximates tanh with a Gaussian continued fraction,
    //   tanh(x) ~= x / (1 + x^2/(3 + x^2/(5 + x^2/7)))
    // for speed. It is a real speed-up and it is also a trap: that expression
    // DIVERGES FROM tanh FOR LARGE x. As x grows it tends to 10/x, which goes
    // to zero, while tanh goes to 1 -- so a saturating input silently stops
    // saturating and the tape's compression disappears exactly where it matters
    // most.
    //
    // If it is adopted it needs a clamp with a measured crossover, and a test
    // that drives the model hard enough to cross it. It belongs to the third
    // rung of the ladder, not here: this is the REFERENCE implementation and
    // its job is to be right (ROADMAP.md, "The solver ladder").

    // Rung 3 of the ladder: evaluate the Langevin functions from a table
    // instead of from coth (DESIGN.md section 4.2).
    //
    // WHAT GETS TABULATED CHANGED WHEN THE SLOPE BECAME THE PRIMITIVE. The
    // original plan was a table of f(H, H', M) -- three dimensions, because the
    // rate was the primitive and hysteresis is stateful so only the derivative
    // may be tabulated. Factoring the field rate out (see magnetisationSlope)
    // left a function that depends on Q only through L(Q) and L'(Q). Everything
    // else in the slope is a few multiplies and one divide, and is not worth
    // approximating.
    //
    // So this is two smooth one-dimensional functions plus a third for the
    // Jacobian, which is cheaper, more accurate per byte, and -- the part that
    // actually matters -- LEAVES THE delta_M GATE EXACT. The gate is a sign
    // comparison performed after the lookup, so it stays sharp. A table of the
    // whole slope would interpolate straight across it, smearing the mechanism
    // that opens the hysteresis loop, and rung 1 established that no structural
    // test would notice.
    class LangevinTable
    {
    public:
        // Message thread only: this allocates.
        //
        // Defaults measured: 4096 points over |Q| <= 16 gives L to about 1e-6
        // absolute and L' to about 5e-6 relative, in 48 KB across the three
        // tables held as float. Float rather than double because the storage
        // precision is already below the interpolation error, and half the
        // footprint is worth more than digits nobody can use.
        void build(int points = 4096, double qMax = 16.0)
        {
            points = points < 4 ? 4 : points;
            qMax_ = qMax > 0.0 ? qMax : 16.0;
            step_ = 2.0 * qMax_ / (points - 1);
            invStep_ = 1.0 / step_;

            l_.resize(static_cast<std::size_t>(points));
            lPrime_.resize(static_cast<std::size_t>(points));
            lDoublePrime_.resize(static_cast<std::size_t>(points));

            for (int i = 0; i < points; ++i)
            {
                const double q = -qMax_ + i * step_;
                l_[static_cast<std::size_t>(i)] = static_cast<float>(langevin(q));
                lPrime_[static_cast<std::size_t>(i)] = static_cast<float>(langevinPrime(q));
                lDoublePrime_[static_cast<std::size_t>(i)] =
                    static_cast<float>(langevinDoublePrime(q));
            }
        }

        [[nodiscard]] bool isBuilt() const noexcept { return !l_.empty(); }

        [[nodiscard]] double l(double q) const noexcept
        {
            // Outside the table the closed form IS its asymptote: coth(q)
            // differs from 1 by 2*exp(-2q), which at q = 16 is 1e-14. So this
            // branch is the cheapest accurate answer rather than a fallback,
            // and clamping to the end of the table instead -- the obvious thing
            // to write -- would put a hard error floor on every hard-driven
            // sample.
            if (q > qMax_)  return 1.0 - 1.0 / q;
            if (q < -qMax_) return -1.0 - 1.0 / q;
            return lookup(l_, q);
        }

        [[nodiscard]] double lPrime(double q) const noexcept
        {
            if (q > qMax_ || q < -qMax_)
                return 1.0 / (q * q);
            return lookup(lPrime_, q);
        }

        [[nodiscard]] double lDoublePrime(double q) const noexcept
        {
            if (q > qMax_ || q < -qMax_)
                return -2.0 / (q * q * q);
            return lookup(lDoublePrime_, q);
        }

    private:
        [[nodiscard]] double lookup(const std::vector<float>& table, double q) const noexcept
        {
            const double x = (q + qMax_) * invStep_;
            auto index = static_cast<std::size_t>(x);
            const std::size_t last = table.size() - 2;
            if (index > last)
                index = last;
            const double frac = x - static_cast<double>(index);
            const double a = table[index];
            const double b = table[index + 1];
            return a + frac * (b - a);
        }

        std::vector<float> l_, lPrime_, lDoublePrime_;
        double qMax_ = 16.0;
        double step_ = 0.0;
        double invStep_ = 0.0;
    };

    // The tape stock: five constants that ARE the tape.
    //
    // Machine-wide, never per-track (PRINCIPLES section 3) -- one reel is one
    // stock. Getting one wrong changes the sound of everything without failing
    // anything that looks like a correctness test, which is why they are
    // asserted directly.
    // ---- THE IEC CASSETTE POSITIONS, WHICH ARE WHAT A SELECTOR SELECTS ----
    //
    // A FORMULATION AND A POSITION ARE NOT THE SAME THING, and the whole tape
    // selector rests on the difference. Four formulations appear in
    // `SOURCES §34` and they occupy THREE positions: chromium dioxide and
    // cobalt-adsorbed ferric are both Type II, by different chemistry and at
    // different coercivities. A deck's switch has a position, not a chemistry.
    //
    // TYPE III IS REAL AND IS NOT HERE. Ferrichrome is an IEC position and a
    // handful of decks had it, but it is a DUAL-LAYER tape -- a ferric layer
    // under a chrome one, the first taking the long wavelengths and the second
    // the short -- so it is not another row in a table of constants and cannot
    // be got by interpolating between two that are. It needs two coupled layers
    // in the record path. Deferred deliberately rather than forgotten.
    //
    // THERE IS NO TYPE V. The classification stops at IV.
    enum class IecType
    {
        TypeI,      // gamma-ferric oxide. 120 us
        TypeII,     // chrome and cobalt-ferric. 70 us
        TypeIV      // metal particle. 70 us
    };

    struct TapeStock
    {
        double saturationMagnetisation = 3.5e5;   // M_s, A/m -- the ceiling
        double coercivity              = 27.0e3;  // k, A/m  -- the loop's width
        double anhystericShape         = 22.0e3;  // a, A/m  -- the knee
        double susceptibilityRatio     = 0.17;    // c -- reversible fraction
        double meanFieldCoupling       = 1.6e-3;  // alpha -- inter-domain

        // WHICH REPRODUCE CONSTANT THE CASSETTE SPEED WANTS, in seconds.
        //
        // NOT A MAGNETIC PROPERTY -- an EQUALISATION one, and it lives here
        // because it travels with the tape rather than with the machine. The
        // IEC cassette standard is 120 us for Type I and 70 us for Type II and
        // Type IV (`SOURCES §34`), which is the whole reason a deck has a tape
        // selector: the same machine must reproduce two different curves
        // depending on what you threaded into it.
        //
        // It is read ONLY at cassette speed. Above that the reel-to-reel table
        // governs and the formulation does not change the curve, because the
        // reel standards are written against speed alone (`SOURCES §16`).
        double cassetteHighSeconds = 120.0e-6;

        // WHICH POSITION ON THE SWITCH THIS FORMULATION BELONGS TO. Not the
        // same question as what its constants are: two of the four share a
        // position. It is what a deck that senses the cassette shell's notches
        // would read, and what a person setting the switch by hand is trying to
        // match.
        IecType type = IecType::TypeI;

        // Ferric oxide, the stock a domestic reel-to-reel runs
        // (SOURCES section 5). IEC Type I where it appears in a cassette.
        [[nodiscard]] static TapeStock ferricOxide() noexcept { return {}; }

        // ---- THE OTHER THREE FORMULATIONS (`SOURCES §34`) ----
        //
        // WHAT IS SOURCED AND WHAT IS NOT, because the split is not obvious and
        // pretending otherwise would be the easy mistake here.
        //
        //   SOURCED: coercivity and saturation. Both are published per
        //   formulation, and coercivity is very nearly the DEFINITION of the
        //   IEC types -- a "high bias" tape is one that needs the bias its
        //   coercivity implies. So is the equalisation above.
        //
        //   NOT SOURCED: `a`, `c` and `alpha`. Nobody publishes Jiles-Atherton
        //   parameters for a cassette formulation; what is published is loops,
        //   and fitting three constants to a printed loop is a project rather
        //   than a lookup. They are therefore SCALED FROM FERRIC at a constant
        //   ratio to coercivity -- `a/k` is held at 0.815 throughout -- which
        //   says "the knee sits in the same place relative to the loop's
        //   width" and asserts nothing else. Where that is wrong it will be
        //   wrong in the shape of the knee, not in where the tape saturates.
        //
        // The consequence a listener meets is bias: `nominalBiasAmplitude` is
        // 2.25 x coercivity, so a metal tape asks for two and a half times the
        // carrier a ferric one does. A machine that cannot deliver it is
        // UNDERBIASED, audibly, and that is the correct behaviour rather than a
        // failure -- it is why a deck without a Type IV position makes a poor
        // job of metal tape.
        [[nodiscard]] static TapeStock cobaltFerric() noexcept
        {
            TapeStock t;
            t.saturationMagnetisation = 3.9e5;
            t.coercivity              = 48.0e3;    // ~600 Oe, IEC Type II
            t.anhystericShape         = 39.1e3;    // 0.815 k
            t.cassetteHighSeconds     = 70.0e-6;
            t.type                    = IecType::TypeII;
            return t;
        }

        [[nodiscard]] static TapeStock chromiumDioxide() noexcept
        {
            TapeStock t;
            t.saturationMagnetisation = 3.8e5;
            t.coercivity              = 40.0e3;    // ~500 Oe, IEC Type II
            t.anhystericShape         = 32.6e3;
            t.cassetteHighSeconds     = 70.0e-6;
            t.type                    = IecType::TypeII;
            return t;
        }

        [[nodiscard]] static TapeStock metalParticle() noexcept
        {
            TapeStock t;
            // PURE IRON RATHER THAN AN OXIDE, which is the whole of why this
            // one is different in kind: no oxygen in the lattice diluting the
            // moment, so saturation is roughly double and coercivity triple.
            t.saturationMagnetisation = 7.0e5;
            t.coercivity              = 88.0e3;    // ~1100 Oe, IEC Type IV
            t.anhystericShape         = 71.7e3;
            t.cassetteHighSeconds     = 70.0e-6;
            t.type                    = IecType::TypeIV;
            return t;
        }
    };

    // ---- WHAT THE MACHINE IS ALIGNED FOR, AS AGAINST WHAT IS LOADED ----
    //
    // A cassette deck's tape selector sets TWO things and neither of them is
    // the tape: the bias current the record amplifier delivers, and which
    // reproduce time constant the replay amplifier uses. The tape decides
    // neither -- it just lies there being whatever it is.
    //
    // So the selector needs a REFERENCE FORMULATION per position: the stock the
    // deck's engineer set that position up against. Type I is ferric and Type IV
    // is metal, which is not a choice -- each position has one formulation.
    // TYPE II HAS TWO, and this takes cobalt-adsorbed ferric as the reference
    // because it is the one that outlived chromium dioxide and is what a late
    // deck's Type II position was aligned on. The consequence is real and worth
    // having: a CrO2 tape on a Type II setting comes out slightly OVERBIASED,
    // at 40 kA/m against the 48 the position delivers, which is what happened
    // to people who kept using chrome after the decks moved on.
    [[nodiscard]] inline TapeStock alignmentStockFor(IecType type) noexcept
    {
        switch (type)
        {
            case IecType::TypeI:  return TapeStock::ferricOxide();
            case IecType::TypeII: return TapeStock::cobaltFerric();
            case IecType::TypeIV: return TapeStock::metalParticle();
        }
        return TapeStock::ferricOxide();
    }

    // AND WHICH REPRODUCE CONSTANT THE POSITION USES (`SOURCES §34`). 120 us
    // for Type I and 70 for the other two, at the cassette speed and nowhere
    // else -- above it the reel standards govern and are written against speed
    // alone.
    [[nodiscard]] inline constexpr double cassetteHighSecondsFor(IecType type) noexcept
    {
        return type == IecType::TypeI ? 120.0e-6 : 70.0e-6;
    }

    // Evaluating the Langevin functions exactly. The default, and what the
    // table above is checked against.
    struct ExactLangevin
    {
        [[nodiscard]] double l(double q) const noexcept { return langevin(q); }
        [[nodiscard]] double lPrime(double q) const noexcept { return langevinPrime(q); }
        [[nodiscard]] double lDoublePrime(double q) const noexcept
        {
            return langevinDoublePrime(q);
        }
    };

    // How the Langevin functions are evaluated -- an axis ORTHOGONAL to the
    // solver (DESIGN.md section 4.2).
    //
    // This is not what the ladder was expected to look like. It was drawn as
    // one sequence, RK4 then Newton then tabulation, each rung replacing the
    // last. Tabulation turns out not to replace anything: it changes how L and
    // L' are evaluated, and every solver calls them. So the choice is a grid of
    // (solver x evaluation) rather than a rung, and the bench measures the grid.
    enum class Evaluation
    {
        Exact,
        Tabulated
    };

    // dM/dH for the Jiles-Atherton model (SOURCES section 5).
    //
    // THE SLOPE, NOT THE RATE, IS THE PRIMITIVE HERE, and that is a deliberate
    // correction to the shape of the source paper.
    //
    // Jiles-Atherton is originally stated as dM/dH. The paper converts it to
    // dM/dt so that time-domain solvers can be used, which forces it to
    // estimate dH/dt from a sampled signal -- and that estimate is where all
    // the trouble lives (see the note in process()). But the rate is EXACTLY
    // proportional to the field rate:
    //
    //     dM/dt = (dH/dt) * g(M, H)
    //
    // so g is the real content and the field rate is a multiplier. Exposing g
    // directly lets a solver integrate with respect to H, where the step is an
    // exact difference of two samples rather than a reconstructed derivative.
    //
    // `direction` is the sign of the field's travel: +1 rising, -1 falling.
    //
    // A free function taking all its state as arguments, deliberately: every
    // rung of the ladder evaluates the SAME g, so keeping it out of the solvers
    // is what stops them quietly diverging in the physics while appearing to
    // differ only in the integration.
    template <class Langevin>
    [[nodiscard]] inline double magnetisationSlopeWith(double M,
                                                       double H,
                                                       double direction,
                                                       const TapeStock& stock,
                                                       const Langevin& langevinSource) noexcept
    {
        const double Q = (H + stock.meanFieldCoupling * M) / stock.anhystericShape;
        const double Lprime = langevinSource.lPrime(Q);

        // The anhysteretic magnetisation: where M would settle with no pinning.
        const double anhysteretic = stock.saturationMagnetisation * langevinSource.l(Q);
        const double gap = anhysteretic - M;

        // delta_S is the direction the field is moving. delta_M gates
        // IRREVERSIBLE domain motion: walls only move when the field is pushing
        // magnetisation the way it already wants to go, so when the two
        // disagree the irreversible term vanishes entirely and only the
        // reversible one remains. That gate is what opens the loop.
        const double deltaS = direction >= 0.0 ? 1.0 : -1.0;
        const double deltaM = (deltaS > 0.0) == (gap > 0.0) ? 1.0 : 0.0;

        const double c = stock.susceptibilityRatio;
        const double msOverA = stock.saturationMagnetisation / stock.anhystericShape;

        // Non-zero for any physical stock -- (1-c)k dominates alpha*gap by
        // orders of magnitude -- but guarded, because a stock is data and data
        // can be wrong, and a division by zero here would reach the audio
        // thread as a NaN written permanently onto the tape.
        double pinning = (1.0 - c) * deltaS * stock.coercivity
                       - stock.meanFieldCoupling * gap;
        constexpr double kMinPinning = 1.0e-12;
        if (std::abs(pinning) < kMinPinning)
            pinning = pinning < 0.0 ? -kMinPinning : kMinPinning;

        const double irreversible = ((1.0 - c) * deltaM * gap) / pinning;
        const double reversible = c * msOverA * Lprime;

        // The implicit term: M appears on both sides through Q, and this is the
        // algebraic solution rather than an iteration.
        const double coupling = 1.0 - c * stock.meanFieldCoupling * msOverA * Lprime;

        return (irreversible + reversible) / coupling;
    }

    // The exact evaluation, which is what everything else is measured against.
    [[nodiscard]] inline double magnetisationSlope(double M,
                                                   double H,
                                                   double direction,
                                                   const TapeStock& stock) noexcept
    {
        return magnetisationSlopeWith(M, H, direction, stock, ExactLangevin{});
    }

    template <class Langevin>
    [[nodiscard]] inline double magnetisationRateWith(double M,
                                                      double H,
                                                      double fieldRate,
                                                      const TapeStock& stock,
                                                      const Langevin& langevinSource) noexcept
    {
        return fieldRate * magnetisationSlopeWith(M, H, fieldRate, stock, langevinSource);
    }

    // dM/dt, for solvers that integrate in time. Exactly the slope scaled by
    // the field rate -- see the note above.
    [[nodiscard]] inline double magnetisationRate(double M,
                                                  double H,
                                                  double fieldRate,
                                                  const TapeStock& stock) noexcept
    {
        return fieldRate * magnetisationSlope(M, H, fieldRate, stock);
    }

    // d(dM/dH)/dM -- the Jacobian Newton-Raphson needs (DESIGN.md section 4.2).
    //
    // Derived analytically rather than differenced numerically, because a
    // difference costs two more evaluations and this is on the audio thread of
    // every armed track.
    //
    // One simplification is worth pointing out because it looks like a slip:
    // in the irreversible term the quotient rule produces
    // (pinning + alpha*gap), and pinning is DEFINED as
    // (1-c)*deltaS*k - alpha*gap, so that sum collapses to (1-c)*deltaS*k
    // exactly. The gap dependence cancels.
    //
    // ACCURACY HERE COSTS ITERATIONS, NOT CORRECTNESS. Newton converges to the
    // root of the residual whatever the Jacobian says, provided it points
    // roughly the right way; a wrong Jacobian shows up as a slower solve, not
    // as a wrong magnetisation. That is why the second-derivative series above
    // does not need to be exact -- but it is also why an error here would be
    // easy to miss, hence the numerical cross-check in the tests.
    template <class Langevin>
    [[nodiscard]] inline double magnetisationSlopeJacobianWith(
        double M, double H, double direction, const TapeStock& stock,
        const Langevin& langevinSource) noexcept
    {
        const double a = stock.anhystericShape;
        const double c = stock.susceptibilityRatio;
        const double alpha = stock.meanFieldCoupling;
        const double ms = stock.saturationMagnetisation;
        const double msOverA = ms / a;
        const double dQdM = alpha / a;

        const double Q = (H + alpha * M) / a;
        const double Lprime = langevinSource.lPrime(Q);
        const double Ldouble = langevinSource.lDoublePrime(Q);

        const double gap = ms * langevinSource.l(Q) - M;
        const double dGapdM = ms * Lprime * dQdM - 1.0;

        const double deltaS = direction >= 0.0 ? 1.0 : -1.0;
        const double deltaM = (deltaS > 0.0) == (gap > 0.0) ? 1.0 : 0.0;

        double pinning = (1.0 - c) * deltaS * stock.coercivity - alpha * gap;
        constexpr double kMinPinning = 1.0e-12;
        if (std::abs(pinning) < kMinPinning)
            pinning = pinning < 0.0 ? -kMinPinning : kMinPinning;

        const double irreversible = ((1.0 - c) * deltaM * gap) / pinning;
        const double reversible = c * msOverA * Lprime;
        const double coupling = 1.0 - c * alpha * msOverA * Lprime;

        // (pinning + alpha*gap) == (1-c)*deltaS*k, per the note above.
        const double dIrreversible = (1.0 - c) * deltaM * dGapdM
                                   * ((1.0 - c) * deltaS * stock.coercivity)
                                   / (pinning * pinning);
        const double dReversible = c * msOverA * Ldouble * dQdM;
        const double dCoupling = -c * alpha * msOverA * Ldouble * dQdM;

        return ((dIrreversible + dReversible) * coupling
                - (irreversible + reversible) * dCoupling)
             / (coupling * coupling);
    }

    [[nodiscard]] inline double magnetisationSlopeJacobian(double M,
                                                           double H,
                                                           double direction,
                                                           const TapeStock& stock) noexcept
    {
        return magnetisationSlopeJacobianWith(M, H, direction, stock, ExactLangevin{});
    }

    // d(dM/dt)/dM, for solvers that integrate in time.
    [[nodiscard]] inline double magnetisationRateJacobian(double M,
                                                          double H,
                                                          double fieldRate,
                                                          const TapeStock& stock) noexcept
    {
        return fieldRate * magnetisationSlopeJacobian(M, H, fieldRate, stock);
    }

    // The rungs of the solver ladder (DESIGN.md section 4.2, ROADMAP.md).
    //
    // Selected at prepare(), so the audio path branches on a value that never
    // changes during a block and predicts perfectly.
    enum class Solver
    {
        // Rung 1. Explicit, fourth-order. The reference the literature
        // validates against a physical machine, and what every other rung is
        // measured against. Not the most robust: being explicit, it goes
        // unstable when the field moves fast enough, which is exactly the
        // failure the next rung exists to fix.
        RungeKutta4,

        // Rung 2. Trapezoidal integration solved by Newton-Raphson. Implicit
        // and A-stable, second-order.
        //
        // NOT CHEAPER PER SAMPLE. Three Newton iterations cost three rate
        // evaluations plus three Jacobians, against RK4's four rates. The prize
        // is that it stays stable where RK4 does not, and stability is what
        // permits lower oversampling -- and oversampling, not the per-sample
        // cost, is what actually drives the CPU bill.
        NewtonRaphson,

        // Explicit, fourth order, integrating in THE FIELD rather than in time.
        //
        // The combination that measurement picked out. It needs no field-rate
        // estimate -- so none of the reconstruction's bias reaches it -- and
        // where the field does not reverse it is near-exact: on a monotonic ramp
        // it reaches machine precision at 48 kHz, five orders better than the
        // trapezoidal scheme.
        //
        // Two things stop that being a free win, and both are measured:
        //
        //   * ON REAL SIGNALS THE ADVANTAGE MOSTLY EVAPORATES. Every field
        //     reversal switches delta_S, and no high-order method integrates
        //     across a branch switch at high order. A sine reverses twice a
        //     cycle, so both schemes land within 1e-3 of each other.
        //   * IT IS STILL EXPLICIT. At a full-scale field step every sample it
        //     is past its stability bound and reaches 1.5e8 times saturation,
        //     where the implicit rung stays inside saturation.
        //
        // So it is the cheapest and the most accurate where the step is small,
        // and it is the one that breaks first when the step is not. Which of
        // those matters depends on oversampling, which is what the bench
        // measures.
        RungeKutta4InField,

        // Explicit midpoint, second order, integrating in the field.
        //
        // Added on a prediction: if the branch switch at each field reversal is
        // what caps the achievable order on real signals -- and the ramp
        // measurement says it is -- then a second-order method gives up nothing
        // there, because second order is all any method is getting. Two slope
        // evaluations instead of four.
        RungeKutta2InField
    };

    // The reference solver: fourth-order Runge-Kutta (SOURCES section 6).
    //
    // THIS IS RUNG ONE OF THE LADDER AND IT STAYS IN THE TREE PERMANENTLY,
    // whether or not it ships as the default. It is the implementation the
    // literature validates against a physical machine, so it is what every
    // cheaper rung is measured against (DESIGN.md section 4.2). Replacing it
    // rather than adding beside it would leave nothing to be right.
    class Hysteresis
    {
    public:
        void prepare(double sampleRate,
                     TapeStock stock = TapeStock::ferricOxide(),
                     Solver solver = Solver::RungeKutta4,
                     Evaluation evaluation = Evaluation::Exact)
        {
            evaluation_ = evaluation;
            stock_ = stock;
            solver_ = solver;
            samplePeriod_ = 1.0 / (sampleRate > 0.0 ? sampleRate : 48000.0);

            // Message thread: this allocates, which is why prepare() is where
            // it happens and why process() cannot change evaluation mode
            // (PRINCIPLES section 8).
            if (evaluation_ == Evaluation::Tabulated && !table_.isBuilt())
                table_.build();

            reset();
        }

        // A blank tape: no magnetisation, no field, no history.
        void reset() noexcept
        {
            magnetisation_ = 0.0;
            previousField_ = 0.0;
            previousFieldRate_ = 0.0;
        }

        // One sample: field in A/m, magnetisation out in A/m.
        [[nodiscard]] double process(double field) noexcept
        {
            // One branch on a value that cannot change during a block, so it
            // predicts perfectly, and the templated body below is specialised
            // for each side -- no indirect call on the audio thread.
            if (evaluation_ == Evaluation::Tabulated)
                return processWith(field, table_);
            return processWith(field, ExactLangevin{});
        }

    private:
        template <class Langevin>
        [[nodiscard]] double processWith(double field, const Langevin& lang) noexcept
        {
            if (solver_ == Solver::NewtonRaphson)
            {
                // Integrates in H, so it needs no field-rate estimate at all --
                // see stepNewton.
                stepNewton(field, lang);
            }
            else if (solver_ == Solver::RungeKutta4InField)
            {
                stepRungeKuttaInField(field, lang);
            }
            else if (solver_ == Solver::RungeKutta2InField)
            {
                stepRungeKutta2InField(field, lang);
            }
            else
            {
                // The RK4 reference integrates in time, so it needs dH/dt from
                // a sampled signal. The trapezoidal rule (SOURCES section 6)
                // recovers it from the current and previous samples plus the
                // previous derivative.
                //
                // MEASURED, AND WORSE THAN IT LOOKS. This recursion has a pole
                // at z = -1, so it is only marginally stable: an error
                // alternating in sign persists forever at constant amplitude
                // instead of decaying. Starting from rest injects exactly such
                // an error. Compared against an analytic derivative the
                // reconstructed rate carries an RMS error of 141% of the true
                // RMS -- at every frequency, not only near Nyquist -- and it
                // disagrees in SIGN with the sample-to-sample difference about
                // half the time.
                //
                // Worse, at EXACTLY Nyquist it does not merely ring, it
                // DIVERGES: 2*dH/T alternates in sign and so does -Hdot(n-1),
                // so the two add constructively every sample and the estimate
                // grows without bound. That is the pole being driven at its
                // resonant frequency, and no integrator downstream can rescue
                // it -- RK4 and Newton alike were unstable there until Newton
                // stopped using this estimate.
                const double fieldRate =
                    2.0 * (field - previousField_) / samplePeriod_ - previousFieldRate_;

                // RK4 needs the input at the half-step, which a sampled signal
                // does not have; linear interpolation supplies it. This is the
                // accuracy floor of the whole scheme -- the integrator is
                // fourth order but its input is second.
                //
                // IT IS ALSO WHAT RESCUES THE DERIVATIVE ABOVE at ordinary
                // frequencies, and that is load-bearing rather than incidental.
                // `midFieldRate` is the MEAN of two consecutive field rates,
                // and averaging two consecutive samples of an alternating
                // sequence cancels it EXACTLY. The two midpoint stages carry
                // two thirds of the RK4 weight, so the artefact largely
                // disappears; what survives from the outer stages measures
                // 3.8e-3 of output RMS and sits at Nyquist.
                //
                // So DO NOT "simplify" this to use `fieldRate` directly. There
                // is a test that fails when you try ("the solver rejects the
                // trapezoidal rule's alternating error").
                const double midField = 0.5 * (previousField_ + field);
                const double midFieldRate = 0.5 * (previousFieldRate_ + fieldRate);

                stepRungeKutta(field, fieldRate, midField, midFieldRate, lang);
                previousFieldRate_ = fieldRate;
            }

            previousField_ = field;
            return magnetisation_;
        }

    public:
        [[nodiscard]] double magnetisation() const noexcept { return magnetisation_; }

        // THE SOLVER'S WHOLE STATE, for `HysteresisBatch` and nothing else.
        //
        // The batch is an accelerator, not a second model: it holds kLanes
        // worth of (M, previousField) while it runs one engine sample and this
        // object stays the authority between samples. Handing the pair across
        // is what lets a lane belong to whichever track is writing this block
        // rather than to a fixed track index, so arming two tracks and arming
        // the odd-numbered eight cost the same packing.
        //
        // ONLY THE IN-FIELD SOLVERS. `previousFieldRate_` is state too, and it
        // is NOT copied here, because the batch refuses the time-domain rungs
        // that use it (`HysteresisBatch::prepare`). If one is ever admitted,
        // this pair stops being the whole state and this comment is the thing
        // that should have stopped it.
        [[nodiscard]] double previousField() const noexcept { return previousField_; }

        void setState(double m, double field) noexcept
        {
            magnetisation_ = m;
            previousField_ = field;
        }

        // Newton iterations taken on the last sample; zero under RK4.
        //
        // Instrumentation rather than test scaffolding: choosing the shipping
        // default is a decision that has to be made on measured cost
        // (PRINCIPLES section 7, section 15), and iteration count IS the cost of
        // this rung.
        [[nodiscard]] int lastIterations() const noexcept { return iterations_; }

        // Whether the last Newton solve reached its tolerance rather than
        // exhausting the iteration cap.
        //
        // This is the property that says the loop is doing real work. One
        // iteration is bit-identical to eight at 220 Hz -- Newton lands on the
        // root immediately from the Euler guess -- so an accuracy test at
        // ordinary frequencies cannot tell a one-iteration solver from a
        // converged one. At 8 kHz the same cap is catastrophically wrong.
        // Asserting convergence catches that where asserting accuracy does not.
        [[nodiscard]] bool lastSolveConverged() const noexcept { return converged_; }

    private:
        template <class Langevin>
        void stepRungeKutta(double field, double fieldRate,
                            double midField, double midFieldRate,
                            const Langevin& lang) noexcept
        {
            const double T = samplePeriod_;
            const double m = magnetisation_;

            const double k1 =
                T * magnetisationRateWith(m, previousField_, previousFieldRate_, stock_, lang);
            const double k2 =
                T * magnetisationRateWith(m + 0.5 * k1, midField, midFieldRate, stock_, lang);
            const double k3 =
                T * magnetisationRateWith(m + 0.5 * k2, midField, midFieldRate, stock_, lang);
            const double k4 = T * magnetisationRateWith(m + k3, field, fieldRate, stock_, lang);

            magnetisation_ += k1 / 6.0 + k2 / 3.0 + k3 / 3.0 + k4 / 6.0;
            iterations_ = 0;
            converged_ = true;  // explicit scheme: nothing to converge
        }

        // RK4 over dM/dH, stepping by the exact sample difference in H.
        //
        // Structurally the same four stages as the time-domain version, with two
        // differences that matter: the step is dH rather than T*dHdt, so no
        // derivative is estimated; and the midpoint field is a linear
        // interpolation, which is second-order.
        //
        // That interpolation was suspected of capping the achievable order and
        // it does not: supplying the EXACT midpoint changes nothing, because
        // what actually limits the order on real signals is the branch switch at
        // each field reversal. Recorded so the interpolation is not "improved"
        // in pursuit of an order it cannot deliver.
        template <class Langevin>
        void stepRungeKuttaInField(double field, const Langevin& lang) noexcept
        {
            const double deltaH = field - previousField_;
            iterations_ = 0;
            converged_ = true;

            if (deltaH == 0.0)
                return;

            const double direction = deltaH >= 0.0 ? 1.0 : -1.0;
            const double midField = 0.5 * (previousField_ + field);
            const double m = magnetisation_;

            const double k1 =
                deltaH * magnetisationSlopeWith(m, previousField_, direction, stock_, lang);
            const double k2 =
                deltaH * magnetisationSlopeWith(m + 0.5 * k1, midField, direction, stock_, lang);
            const double k3 =
                deltaH * magnetisationSlopeWith(m + 0.5 * k2, midField, direction, stock_, lang);
            const double k4 =
                deltaH * magnetisationSlopeWith(m + k3, field, direction, stock_, lang);

            // ---- NON-FINITE ONLY, AND DELIBERATELY NOT A MAGNITUDE CLAMP ----
            //
            // The first attempt at this clamped to +/-saturation, and it broke
            // *"RK4 in the field is explicit, and says so at Nyquist"* -- which
            // is the point. This rung IS unstable past its stability bound and
            // reaches 1.5e8 times saturation at a full-scale field step every
            // sample; that is why the implicit rung still earns its place and
            // why the ladder choice is a trade rather than a free win. A clamp
            // would make an unstable solver read as stable and hide the reason
            // the bench exists.
            //
            // WHAT MUST NOT HAPPEN IS A NON-FINITE VALUE. A huge magnetisation
            // is a loud, ugly, RECOVERABLE sound; a NaN is a dead machine. It
            // is written to the medium permanently (`PRINCIPLES §2`), and it
            // poisons every recursive filter downstream that reads it -- the
            // reproduce one-pole holds a NaN for ever, so the output does not
            // come back until something resets it.
            //
            // So: divergence is allowed and infinity is not.
            magnetisation_ = finiteOrSaturated(
                m + k1 / 6.0 + k2 / 3.0 + k3 / 3.0 + k4 / 6.0, m);
        }

        // A NON-FINITE MAGNETISATION IS THE ONE OUTCOME THAT CANNOT BE
        // ALLOWED, and this is the whole of the guard. It does NOT bound
        // magnitude -- see the note at the RK4 rung on why bounding it would
        // hide an unstable solver -- it only replaces a value that has stopped
        // being a number, with saturation in the direction the state was last
        // heading.
        [[nodiscard]] double finiteOrSaturated(double next, double previous) const noexcept
        {
            if (std::isfinite(next))
                return next;
            const double ms = stock_.saturationMagnetisation;
            return previous < 0.0 ? -ms : ms;
        }

        // Explicit midpoint over dM/dH: evaluate the slope at the start, step
        // half way on it, and take the whole step on the slope found there.
        template <class Langevin>
        void stepRungeKutta2InField(double field, const Langevin& lang) noexcept
        {
            const double deltaH = field - previousField_;
            iterations_ = 0;
            converged_ = true;

            if (deltaH == 0.0)
                return;

            const double direction = deltaH >= 0.0 ? 1.0 : -1.0;
            const double midField = 0.5 * (previousField_ + field);
            const double m = magnetisation_;

            const double k1 =
                deltaH * magnetisationSlopeWith(m, previousField_, direction, stock_, lang);
            const double k2 =
                deltaH * magnetisationSlopeWith(m + 0.5 * k1, midField, direction, stock_, lang);

            // Guarded for the same reason as the RK4 rung above, and in the
            // same way: divergence allowed, infinity not.
            magnetisation_ = finiteOrSaturated(m + k2, m);
        }

        // Trapezoidal integration IN THE FIELD, implicit in M(n):
        //
        //   M(n) = M(n-1) + (dH/2) * [ g(M(n), H(n)) + g(M(n-1), H(n-1)) ]
        //
        // solved by Newton on the residual, with g = dM/dH.
        //
        // INTEGRATING IN H RATHER THAN IN t IS THE POINT OF THIS RUNG, and it
        // is a bigger result than "implicit is more stable".
        //
        // The time-domain form needs dH/dt reconstructed from samples, and that
        // reconstruction diverges outright at Nyquist (see process()). No
        // integrator can fix an input that is already infinite, so a
        // time-domain Newton is unstable there for exactly the same reason RK4
        // is. Integrating in H removes the reconstruction from the problem
        // instead of trying to survive it: dH is the difference of two samples,
        // which is exact at every frequency, and the field rate never appears.
        //
        // Jiles-Atherton is stated as dM/dH in the first place; the conversion
        // to dM/dt was the source paper's accommodation to time-domain solvers,
        // and this rung simply declines to make it.
        template <class Langevin>
        void stepNewton(double field, const Langevin& lang) noexcept
        {
            const double previous = magnetisation_;
            const double deltaH = field - previousField_;

            iterations_ = 0;
            converged_ = false;

            // No field change, no magnetisation change.
            //
            // AN OPTIMISATION, NOT A GUARD. With deltaH zero the residual is
            // identically zero and Newton makes no move, so removing this
            // changes no result -- verified by planting its removal and
            // watching every test stay green. It is here because an idle armed
            // track is the common case and this skips the whole solve.
            if (deltaH == 0.0)
            {
                converged_ = true;
                return;
            }

            const double direction = deltaH >= 0.0 ? 1.0 : -1.0;

            const double previousSlope =
                magnetisationSlopeWith(previous, previousField_, direction, stock_, lang);

            // Everything in the residual that does not depend on M(n).
            const double base = previous + 0.5 * deltaH * previousSlope;

            // Explicit Euler as the opening guess. One order worse than the
            // scheme being solved, which is what a starting point should be:
            // close enough for Newton to converge quadratically, cheap enough
            // to be free.
            double m = previous + deltaH * previousSlope;

            const double tolerance = kSolveTolerance * stock_.saturationMagnetisation;

            for (int i = 0; i < kMaxIterations; ++i)
            {
                const double residual =
                    m - base
                    - 0.5 * deltaH * magnetisationSlopeWith(m, field, direction, stock_, lang);
                double slope =
                    1.0 - 0.5 * deltaH
                        * magnetisationSlopeJacobianWith(m, field, direction, stock_, lang);

                // Well conditioned in practice, but a stock is data and data can
                // be wrong; a Newton step divided by zero would write an
                // infinity onto the tape.
                constexpr double kMinSlope = 1.0e-6;
                if (std::abs(slope) < kMinSlope)
                    slope = slope < 0.0 ? -kMinSlope : kMinSlope;

                double step = residual / slope;

                // DAMPING. Newton on this residual can cycle, because the
                // residual has a CORNER in m: delta_M switches the irreversible
                // term off when `gap` changes sign. The term is proportional to
                // gap, so it switches on exactly where it is zero and the
                // residual stays continuous -- but its DERIVATIVE jumps, and
                // Newton, which is built on that derivative, can oscillate
                // across the corner indefinitely. No number of iterations
                // resolves it. Past the convergence limit --
                // field steps per sample near the largest possible -- an
                // undamped iterate wanders far outside saturation and the
                // solver returns an unphysical magnetisation.
                //
                // A cap on the step keeps the iterate in a sane region without
                // affecting normal operation: measured steps in the operating
                // regime are orders of magnitude below this, so it never binds
                // there and the iteration counts are unchanged.
                const double maxStep = kMaxStepFraction * stock_.saturationMagnetisation;
                if (step > maxStep)
                    step = maxStep;
                else if (step < -maxStep)
                    step = -maxStep;

                m -= step;
                ++iterations_;

                if (std::abs(step) < tolerance)
                {
                    converged_ = true;
                    break;
                }
            }

            // Saturation is a hard physical bound, not a soft one: |M| cannot
            // exceed M_s, because the anhysteretic curve it tracks is M_s times
            // a Langevin and |L| < 1. In the converged regime the solver
            // respects that on its own; past the convergence limit it is what
            // stops a non-converged solve writing an impossible magnetisation
            // onto the tape.
            magnetisation_ = std::clamp(m, -stock_.saturationMagnetisation,
                                        stock_.saturationMagnetisation);
        }

        // Converged when a Newton step moves magnetisation by less than this
        // fraction of saturation. Well below anything representable in the
        // audio that follows.
        static constexpr double kSolveTolerance = 1.0e-10;

        // The largest fraction of saturation one Newton step may move the
        // iterate.
        //
        // CHOSEN BY MEASUREMENT, and the curve is not monotonic, so it is not a
        // "safer is smaller" dial. Measured non-convergences over 24000 samples
        // at 19 kHz and 8e5 A/m, with mean iterations at 8 kHz in brackets:
        //
        //   undamped  12027  (3.33)
        //   0.10      15000  (5.67)   over-damped: cannot reach the root in 16
        //   0.25          0  (4.33)
        //   0.50          0  (3.67)
        //   1.00          0  (3.33)   <- converges everywhere, costs nothing
        //   2.00          0  (3.33)   no further gain
        //
        // One saturation per step is enough to stop the iterate wandering off,
        // and loose enough that it never binds in the operating regime -- the
        // iteration counts there are identical to undamped.
        static constexpr double kMaxStepFraction = 1.0;

        // A backstop against a pathological stock, not a working limit: the
        // measured worst case is far below it. Set high enough that the test
        // asserting a small iteration count is asserting something.
        static constexpr int kMaxIterations = 16;

        TapeStock stock_{};
        Solver solver_ = Solver::RungeKutta4;
        Evaluation evaluation_ = Evaluation::Exact;
        LangevinTable table_;
        int iterations_ = 0;
        bool converged_ = true;
        double samplePeriod_ = 1.0 / 48000.0;
        double magnetisation_ = 0.0;
        double previousField_ = 0.0;
        double previousFieldRate_ = 0.0;
    };
}
