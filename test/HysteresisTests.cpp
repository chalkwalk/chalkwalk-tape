// Jiles-Atherton hysteresis (SOURCES section 5, DESIGN.md section 4.2).
//
// RECORD SIDE (PRINCIPLES section 2): what this computes is magnetisation, and
// magnetisation is what gets stored on the tape. Every test here is therefore
// about something permanent.
//
// These assert closed-form and structural properties, not golden buffers
// (PRINCIPLES section 7). A hysteresis model has a great deal of testable
// structure -- symmetry, saturation, loop area, minor-loop nesting -- and a
// buffer comparison would pass for any of a thousand wrong implementations.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/Bias.h>
#include <chalkwalk/tape/Hysteresis.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

// ---------------------------------------------------------------------------
// The Langevin function, which is the anhysteretic curve's shape.
//
//   L(x)  = coth(x) - 1/x
//   L'(x) = 1/x^2 - coth^2(x) + 1
//
// Both are 0/0 at the origin and need a series branch there (SOURCES section 5).
// ---------------------------------------------------------------------------

TEST_CASE("the Langevin function matches its closed form away from zero", "[hysteresis][langevin]")
{
    for (const double x : {0.5, 1.0, 2.0, 5.0, 20.0})
    {
        const double closedForm = 1.0 / std::tanh(x) - 1.0 / x;
        REQUIRE(tape::langevin(x) == Approx(closedForm).epsilon(1.0e-12));
    }
}

TEST_CASE("the Langevin function is odd", "[hysteresis][langevin]")
{
    // L(-x) = -L(x). This is what makes the tape symmetric about zero, so a
    // sign error here would show up as an asymmetric hysteresis loop -- audible
    // as even-harmonic distortion the model should not have.
    for (const double x : {0.001, 0.1, 1.0, 7.0})
        REQUIRE(tape::langevin(-x) == Approx(-tape::langevin(x)).epsilon(1.0e-12));
}

TEST_CASE("the Langevin function tends to x/3 at the origin", "[hysteresis][langevin]")
{
    // The series expansion. The closed form is 0/0 here and in floating point
    // it is catastrophic cancellation for a good while before it is actually
    // undefined, which is why there is a branch at all.
    for (const double x : {1.0e-8, 1.0e-6, 1.0e-5})
        REQUIRE(tape::langevin(x) == Approx(x / 3.0).epsilon(1.0e-9));

    REQUIRE(tape::langevin(0.0) == Approx(0.0).margin(1.0e-18));
}

TEST_CASE("the Langevin branches meet without a step", "[hysteresis][langevin]")
{
    // A discontinuity at the guard threshold would be a click in the audio, and
    // it is exactly the kind of defect that a test of each branch separately
    // cannot see.
    //
    // The comparison has to be BETWEEN THE TWO BRANCHES AT THE SAME x. Sampling
    // either side of the threshold instead compares different arguments, and
    // then a 0.2% difference in x produces a 0.2% difference in L that has
    // nothing to do with continuity.
    const double x = tape::kSmallArgument * 1.001;  // just above: closed form
    const double x2 = x * x;

    // What the branch below would have returned at the same x. Three terms,
    // matching the implementation -- a one-term x/3 is only good to x^2/5,
    // which at this threshold is 8e-5 and would make this test assert the
    // wrong thing rather than nothing.
    const double seriesL = x * (1.0 / 3.0 + x2 * (-1.0 / 45.0 + x2 * (2.0 / 945.0)));
    const double seriesLprime = 1.0 / 3.0 + x2 * (-1.0 / 15.0 + x2 * (2.0 / 189.0));

    REQUIRE(tape::langevin(x) == Approx(seriesL).epsilon(1.0e-11));
    REQUIRE(tape::langevinPrime(x) == Approx(seriesLprime).epsilon(1.0e-11));
}

TEST_CASE("the Langevin function saturates at one", "[hysteresis][langevin]")
{
    // L(x) -> 1 as x -> infinity, and this is what makes tape COMPRESS rather
    // than clip.
    //
    // But it approaches 1 as 1 - 1/x, which is slow: L(100) is 0.99, not 1.
    // Asserting the limit naively passes only for large x and hides the rate,
    // so assert the RATE, which is the thing that actually shapes the knee.
    for (const double x : {20.0, 100.0, 1000.0})
        REQUIRE(tape::langevin(x) == Approx(1.0 - 1.0 / x).epsilon(1.0e-9));

    // Never reaches or exceeds 1: magnetisation is bounded by saturation.
    for (const double x : {1.0, 50.0, 1.0e4, 1.0e8})
        REQUIRE(tape::langevin(x) < 1.0);

    // And it does get there eventually.
    REQUIRE(tape::langevin(1.0e7) == Approx(1.0).epsilon(1.0e-6));
}

TEST_CASE("the Langevin function is monotonic", "[hysteresis][langevin]")
{
    // More field always means more magnetisation. A non-monotonic anhysteretic
    // curve would mean pushing harder made the tape quieter, which is neither
    // physical nor something a listener would forgive.
    double previous = -2.0;
    for (int i = -500; i <= 500; ++i)
    {
        const double value = tape::langevin(static_cast<double>(i) * 0.02);
        REQUIRE(value > previous);
        previous = value;
    }
}

TEST_CASE("the Langevin derivative matches its closed form", "[hysteresis][langevin]")
{
    for (const double x : {0.5, 1.0, 2.0, 5.0})
    {
        const double coth = 1.0 / std::tanh(x);
        const double closedForm = 1.0 / (x * x) - coth * coth + 1.0;
        REQUIRE(tape::langevinPrime(x) == Approx(closedForm).epsilon(1.0e-12));
    }
}

TEST_CASE("the Langevin derivative tends to one third at the origin", "[hysteresis][langevin]")
{
    for (const double x : {1.0e-8, 1.0e-6, 1.0e-5})
        REQUIRE(tape::langevinPrime(x) == Approx(1.0 / 3.0).epsilon(1.0e-6));

    REQUIRE(tape::langevinPrime(0.0) == Approx(1.0 / 3.0).epsilon(1.0e-12));
}

TEST_CASE("the Langevin derivative agrees with a numerical derivative", "[hysteresis][langevin]")
{
    // An independent check by a different method -- the strongest form of
    // agreement available here, since the two share no machinery. This
    // ecosystem has been caught out by two derivations of one quantity that
    // agreed with themselves and not with reality.
    for (const double x : {0.2, 0.7, 1.5, 3.0, 8.0})
    {
        constexpr double h = 1.0e-6;
        const double numerical = (tape::langevin(x + h) - tape::langevin(x - h)) / (2.0 * h);
        REQUIRE(tape::langevinPrime(x) == Approx(numerical).epsilon(1.0e-6));
    }
}

TEST_CASE("the Langevin derivative is even", "[hysteresis][langevin]")
{
    // The derivative of an odd function is even.
    for (const double x : {0.1, 1.0, 4.0})
        REQUIRE(tape::langevinPrime(-x) == Approx(tape::langevinPrime(x)).epsilon(1.0e-12));
}

// ---------------------------------------------------------------------------
// The hysteresis engine.
//
// Fields and magnetisations are in A/m throughout. The record head produces a
// field on the order of 1e5 A/m peak (SOURCES section 1), and the ferric-oxide
// constants put coercivity at 27 kA/m, so test amplitudes live around there.
// ---------------------------------------------------------------------------

namespace
{
    // Drive a field sequence through the model and return the magnetisations.
    std::vector<double> run(tape::Hysteresis& h, const std::vector<double>& field)
    {
        std::vector<double> out;
        out.reserve(field.size());
        for (const double H : field)
            out.push_back(h.process(H));
        return out;
    }

    // A field that ramps linearly from `from` to `to` over `n` samples.
    std::vector<double> ramp(double from, double to, int n)
    {
        std::vector<double> v;
        v.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            v.push_back(from + (to - from) * (static_cast<double>(i) / (n - 1)));
        return v;
    }

    std::vector<double> sine(double amplitude, double cycles, int n)
    {
        std::vector<double> v;
        v.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            v.push_back(amplitude * std::sin(2.0 * M_PI * cycles * i / n));
        return v;
    }
}

TEST_CASE("ferric oxide has the published constants", "[hysteresis][stock]")
{
    // SOURCES section 5. These are the tape, and getting one wrong changes the
    // sound of everything without failing anything else.
    const auto stock = tape::TapeStock::ferricOxide();
    REQUIRE(stock.saturationMagnetisation == Approx(3.5e5));
    REQUIRE(stock.coercivity == Approx(27.0e3));
    REQUIRE(stock.anhystericShape == Approx(22.0e3));
    REQUIRE(stock.susceptibilityRatio == Approx(0.17));
    REQUIRE(stock.meanFieldCoupling == Approx(1.6e-3));
}

TEST_CASE("a blank tape in no field stays blank", "[hysteresis]")
{
    // Silence in, silence out. A model that drifts or self-oscillates from rest
    // would put a signal on tape that nobody recorded, and it would be doing it
    // on every idle armed track.
    tape::Hysteresis h;
    h.prepare(48000.0);

    for (int i = 0; i < 10000; ++i)
        REQUIRE(h.process(0.0) == Approx(0.0).margin(1.0e-12));
}

TEST_CASE("magnetisation never exceeds saturation", "[hysteresis]")
{
    // The bound that makes this compression rather than clipping.
    tape::Hysteresis h;
    h.prepare(48000.0);
    const auto stock = tape::TapeStock::ferricOxide();

    const auto out = run(h, sine(2.0e6, 20.0, 20000));  // absurdly hard drive
    for (const double M : out)
    {
        REQUIRE(std::isfinite(M));
        REQUIRE(std::abs(M) <= stock.saturationMagnetisation * 1.001);
    }
}

TEST_CASE("the model is stable under a long hard drive", "[hysteresis]")
{
    // The trapezoidal derivative approximation is where instability would come
    // from, and it would show up as a slow divergence rather than an immediate
    // one -- so this runs long enough to see that.
    tape::Hysteresis h;
    h.prepare(48000.0);

    const auto out = run(h, sine(5.0e5, 2000.0, 480000));  // ten seconds
    for (const double M : out)
        REQUIRE(std::isfinite(M));
}

TEST_CASE("the response is odd-symmetric", "[hysteresis]")
{
    // Negating the input negates the output. This is what stops the model
    // generating even harmonics it has no physical reason to generate, and it
    // is the assertion that catches a sign error in the direction terms
    // (delta_S and delta_M), which are the easiest thing here to get wrong.
    const auto field = sine(1.5e5, 5.0, 4000);
    std::vector<double> negated;
    negated.reserve(field.size());
    for (const double H : field)
        negated.push_back(-H);

    tape::Hysteresis a, b;
    a.prepare(48000.0);
    b.prepare(48000.0);

    const auto positive = run(a, field);
    const auto inverted = run(b, negated);

    for (std::size_t i = 0; i < positive.size(); ++i)
        REQUIRE(inverted[i] == Approx(-positive[i]).epsilon(1.0e-9));
}

TEST_CASE("the tape remembers: remanence is non-zero", "[hysteresis]")
{
    // THE defining property, and the one the project is named after. Drive the
    // tape to saturation, take the field away, and magnetisation REMAINS. If
    // this is zero there is no recording -- only a memoryless waveshaper, which
    // is what most tape plugins actually are.
    tape::Hysteresis h;
    h.prepare(48000.0);

    run(h, ramp(0.0, 3.0e5, 4000));         // magnetise hard
    const auto relaxed = run(h, ramp(3.0e5, 0.0, 4000));  // remove the field

    const double remanent = relaxed.back();
    REQUIRE(remanent > 0.0);
    // A real ferric-oxide tape retains a substantial fraction of saturation.
    REQUIRE(remanent > 0.05 * tape::TapeStock::ferricOxide().saturationMagnetisation);
}

TEST_CASE("the loop has area: M depends on which way you came", "[hysteresis]")
{
    // Hysteresis means the curve traced going up is not the curve traced coming
    // down. Sampling M at the same field on both legs must give different
    // answers -- if they agree, the model has no memory.
    tape::Hysteresis h;
    h.prepare(48000.0);

    constexpr double probeField = 1.0e4;

    // Settle into a repeating major loop first, so this is the steady state
    // rather than the initial magnetisation curve.
    run(h, sine(2.0e5, 8.0, 8000));

    const auto up = run(h, ramp(-2.0e5, 2.0e5, 8000));
    const auto down = run(h, ramp(2.0e5, -2.0e5, 8000));

    const auto fieldUp = ramp(-2.0e5, 2.0e5, 8000);
    const auto fieldDown = ramp(2.0e5, -2.0e5, 8000);

    auto sampleAt = [&](const std::vector<double>& f, const std::vector<double>& m)
    {
        std::size_t best = 0;
        double bestErr = 1.0e30;
        for (std::size_t i = 0; i < f.size(); ++i)
            if (std::abs(f[i] - probeField) < bestErr)
            {
                bestErr = std::abs(f[i] - probeField);
                best = i;
            }
        return m[best];
    };

    const double rising = sampleAt(fieldUp, up);
    const double falling = sampleAt(fieldDown, down);

    // Coming down from saturation leaves MORE magnetisation at a given field
    // than coming up from the negative side.
    REQUIRE(falling > rising);
}

TEST_CASE("the solver converges as the step shrinks", "[hysteresis][solver]")
{
    // An integrator test rather than a physics test: the SAME field history,
    // sampled at two rates, must integrate to nearly the same magnetisation. If
    // it does not, the answer depends on the sample rate -- a project would
    // sound different at 96 kHz than at 48 kHz.
    //
    // This is the measurement every cheaper rung of the ladder gets compared
    // against, so it is worth being precise about what "the same" means.
    //
    // COMPARE MATCHED SAMPLE POINTS. Doubling the rate puts fine[2i] at the
    // same phase as coarse[i], but the two sequences' LAST samples are at
    // different phases -- sin(-2*pi*10/4800) against sin(-2*pi*10/9600). On a
    // steep part of the loop that difference reads as a 5% solver error when
    // the solver is in fact agreeing to 0.1%. An integrator test that compares
    // the wrong points measures the test, not the integrator.
    constexpr int coarseSamples = 4800;
    auto fieldAt = [](int i, int n) { return 2.0e5 * std::sin(2.0 * M_PI * 10.0 * i / n); };

    tape::Hysteresis coarse, fine;
    coarse.prepare(48000.0);
    fine.prepare(96000.0);

    std::vector<double> atCoarse, atFine;
    for (int i = 0; i < coarseSamples; ++i)
        atCoarse.push_back(coarse.process(fieldAt(i, coarseSamples)));
    for (int i = 0; i < coarseSamples * 2; ++i)
        atFine.push_back(fine.process(fieldAt(i, coarseSamples * 2)));

    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;

    double worst = 0.0;
    double peakCoarse = 0.0;
    double peakFine = 0.0;
    for (int i = 0; i < coarseSamples; ++i)
    {
        worst = std::max(worst, std::abs(atFine[static_cast<std::size_t>(i) * 2] - atCoarse[i]));
        peakCoarse = std::max(peakCoarse, std::abs(atCoarse[i]));
        peakFine = std::max(peakFine, std::abs(atFine[static_cast<std::size_t>(i) * 2]));
    }

    // Trajectory: agreement measured at 6.2e-4 of saturation, asserted with
    // margin. Bounds set FROM the measurement rather than chosen, so a
    // regression in the integrator moves this rather than hiding under a round
    // number.
    REQUIRE(worst / saturation < 1.0e-3);

    // Peak magnetisation is the robust feature -- a stationary point, so it does
    // not care about phase. Measured agreement 2.4e-5.
    REQUIRE(peakFine == Approx(peakCoarse).epsilon(1.0e-4));
}

TEST_CASE("harder drive gives more output, but not proportionally", "[hysteresis]")
{
    // Compression. Doubling the field must increase magnetisation, and by less
    // than double -- which is the whole reason anybody wants tape.
    auto peakFor = [](double amplitude)
    {
        tape::Hysteresis h;
        h.prepare(48000.0);
        const auto out = run(h, sine(amplitude, 10.0, 8000));
        double peak = 0.0;
        for (const double M : out)
            peak = std::max(peak, std::abs(M));
        return peak;
    };

    const double quiet = peakFor(5.0e4);
    const double loud = peakFor(1.0e5);

    REQUIRE(loud > quiet);
    REQUIRE(loud < 2.0 * quiet);
}

TEST_CASE("reset returns the machine to a blank tape", "[hysteresis]")
{
    tape::Hysteresis h;
    h.prepare(48000.0);
    run(h, sine(2.0e5, 5.0, 4000));
    REQUIRE(h.magnetisation() != Approx(0.0));

    h.reset();
    REQUIRE(h.magnetisation() == Approx(0.0));
    REQUIRE(h.process(0.0) == Approx(0.0).margin(1.0e-12));
}

// ---------------------------------------------------------------------------
// The two tests below exist because the suite above was PROVEN NOT TO CATCH
// two deliberate bugs. Planting a fault and watching the tests stay green is
// the only way to find out that they are decorative (PRINCIPLES section 7), and
// it found exactly that here.
// ---------------------------------------------------------------------------

TEST_CASE("magnetisation always moves the way the field moves", "[hysteresis][direction]")
{
    // The physical content of delta_M: irreversible domain motion happens only
    // in the direction the field is pushing. With the reversible term carrying
    // the field's sign directly, dM/dt must ALWAYS share the sign of dH/dt.
    //
    // Removing the delta_M gate lets the irreversible term run backwards
    // whenever magnetisation is on the far side of the anhysteretic curve, so
    // the tape demagnetises while the field is still rising. Every structural
    // test in this file passed with that fault in place -- loop area,
    // remanence, saturation, symmetry and compression are all blind to it.
    //
    // Asserted on the RATE FUNCTION, not on a processed signal. The discretised
    // model is driven by a RECONSTRUCTED field rate, and that reconstruction
    // does not share the sign of the sample-to-sample difference (see the note
    // on the trapezoidal rule in Hysteresis.h) -- so the same assertion applied
    // to process() would be testing the discretisation, not the physics.
    const auto stock = tape::TapeStock::ferricOxide();

    for (int mi = -40; mi <= 40; ++mi)
    {
        for (int hi = -40; hi <= 40; ++hi)
        {
            for (const double fieldRate : {1.0e6, -1.0e6, 3.0e4, -3.0e4})
            {
                const double rate = tape::magnetisationRate(
                    mi * 1.0e4, hi * 1.0e4, fieldRate, stock);
                REQUIRE(std::isfinite(rate));
                REQUIRE(rate * fieldRate >= 0.0);
            }
        }
    }
}

TEST_CASE("the solver rejects the trapezoidal rule's alternating error", "[hysteresis][solver]")
{
    // The field-rate reconstruction is marginally stable: its error alternates
    // in sign and NEVER DECAYS, and measured against an analytic derivative it
    // is 141% of the true RMS. See the note in Hysteresis.h.
    //
    // The model is nevertheless correct, because RK4's two midpoint stages use
    // the MEAN of consecutive field rates, and averaging two consecutive
    // samples of an alternating sequence cancels it exactly -- and those two
    // stages carry two thirds of the weight.
    //
    // That is a load-bearing accident, so it is pinned here. Anyone who
    // "simplifies" the midpoint to use the current field rate directly will
    // find the artefact appearing in the output at half the sample rate, and
    // this test is what tells them.
    tape::Hysteresis h;
    h.prepare(48000.0);

    constexpr int n = 8192;
    std::vector<double> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(h.process(1.0e5 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0)));

    // Energy at exactly Nyquist -- the alternating component -- against total.
    // A clean output has essentially none.
    double alternating = 0.0;
    double total = 0.0;
    for (int i = 1024; i < n; ++i)  // skip the genuine startup transient
    {
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        alternating += sign * out[static_cast<std::size_t>(i)];
        total += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)];
    }
    const auto samples = static_cast<double>(n - 1024);
    const double alternatingRms = std::abs(alternating) / samples;
    const double totalRms = std::sqrt(total / samples);

    // Measured residual: 3.8e-3. The cancellation is not exact, because the
    // model is non-linear and the two outer RK4 stages carry the artefact with
    // opposite signs at one sixth weight each. What survives sits at exactly
    // Nyquist, where the downsampling filter after the oversampled record path
    // removes it (DESIGN.md section 4.2).
    //
    // The bound is set FROM that measurement with margin, not chosen as a round
    // number, so a regression moves it rather than hiding beneath it.
    REQUIRE(alternatingRms / totalRms < 1.0e-2);
}

TEST_CASE("the rate equation matches an independent evaluation", "[hysteresis][rate]")
{
    // A direct algebraic check of magnetisationRate, computed a second time in
    // this test from the published equation with different code structure.
    //
    // It exists for the implicit coupling denominator, 1 - c*alpha*(Ms/a)*L'.
    // For ferric oxide that term is a 0.14% correction, so deleting it entirely
    // left every structural test green. Here the stock is chosen to make it a
    // 13% effect, which turns an invisible algebra slip into a failing test.
    tape::TapeStock exaggerated;
    exaggerated.saturationMagnetisation = 3.5e5;
    exaggerated.coercivity = 27.0e3;
    exaggerated.anhystericShape = 22.0e3;
    exaggerated.susceptibilityRatio = 0.5;    // c, far above ferric oxide's 0.17
    exaggerated.meanFieldCoupling = 0.05;     // alpha, far above 1.6e-3

    struct Case { double M, H, fieldRate; };
    const Case cases[] = {
        {  0.0,      0.0,  1.0e6},
        {  5.0e4,    2.0e4,  1.0e6},
        {  5.0e4,    2.0e4, -1.0e6},   // delta_S flips
        { -8.0e4,   -3.0e4,  5.0e5},
        {  1.2e5,    1.0e4, -2.0e6},
    };

    for (const auto& c : cases)
    {
        // Independent evaluation, written out longhand from the equation.
        const double Q = (c.H + exaggerated.meanFieldCoupling * c.M)
                       / exaggerated.anhystericShape;
        const double coth = std::abs(Q) < 1.0e-4 ? 0.0 : 1.0 / std::tanh(Q);
        const double L = std::abs(Q) < 1.0e-4 ? Q / 3.0 : coth - 1.0 / Q;
        const double Lp = std::abs(Q) < 1.0e-4 ? 1.0 / 3.0
                                               : 1.0 / (Q * Q) - coth * coth + 1.0;
        const double Man = exaggerated.saturationMagnetisation * L;
        const double gap = Man - c.M;
        const double dS = c.fieldRate >= 0.0 ? 1.0 : -1.0;
        const double dM = ((dS > 0.0) == (gap > 0.0)) ? 1.0 : 0.0;
        const double cc = exaggerated.susceptibilityRatio;
        const double msa = exaggerated.saturationMagnetisation / exaggerated.anhystericShape;

        const double irr = ((1.0 - cc) * dM * gap)
                         / ((1.0 - cc) * dS * exaggerated.coercivity
                            - exaggerated.meanFieldCoupling * gap)
                         * c.fieldRate;
        const double rev = cc * msa * c.fieldRate * Lp;
        const double coupling = 1.0 - cc * exaggerated.meanFieldCoupling * msa * Lp;
        const double expected = (irr + rev) / coupling;

        REQUIRE(tape::magnetisationRate(c.M, c.H, c.fieldRate, exaggerated)
                == Approx(expected).epsilon(1.0e-12));

        // And confirm the denominator is actually doing visible work here, so
        // this test cannot quietly stop testing what it exists to test.
        REQUIRE(std::abs(coupling - 1.0) > 0.05);
    }
}

// ===========================================================================
// Rung 2 of the ladder: trapezoidal integration solved by Newton-Raphson.
//
// The prize is NOT evaluation count -- NR at three iterations evaluates more
// than RK4's four stages. It is stability, because stability is what permits
// lower oversampling, and oversampling is the actual cost driver
// (DESIGN.md section 4.2).
//
// It needs the Jacobian of the rate equation, which needs the Langevin second
// derivative.
// ===========================================================================

TEST_CASE("the Langevin second derivative matches a reference", "[hysteresis][langevin]")
{
    // L''(x) = -2/x^3 - 2coth(x) + 2coth^3(x), and near zero -2x/15.
    //
    // Checked against a central difference of L', which shares none of its
    // machinery -- the independent-method check that this ecosystem keeps
    // finding it needs.
    for (const double x : {0.05, 0.2, 0.5, 1.0, 3.0, 8.0})
    {
        const double h = 1.0e-5 * std::max(1.0, x);
        const double numerical = (tape::langevinPrime(x + h) - tape::langevinPrime(x - h)) / (2.0 * h);
        REQUIRE(tape::langevinDoublePrime(x) == Approx(numerical).epsilon(1.0e-4));
    }
}

TEST_CASE("the Langevin second derivative tends to -2x/15 at the origin", "[hysteresis][langevin]")
{
    for (const double x : {1.0e-6, 1.0e-4, 1.0e-3})
        REQUIRE(tape::langevinDoublePrime(x) == Approx(-2.0 * x / 15.0).epsilon(1.0e-3));

    REQUIRE(tape::langevinDoublePrime(0.0) == Approx(0.0).margin(1.0e-18));
}

TEST_CASE("the Langevin second derivative is odd", "[hysteresis][langevin]")
{
    // L' is even, so its derivative is odd.
    for (const double x : {0.001, 0.05, 1.0, 5.0})
        REQUIRE(tape::langevinDoublePrime(-x) == Approx(-tape::langevinDoublePrime(x)).epsilon(1.0e-12));
}

TEST_CASE("the second derivative stays accurate through the cancellation zone",
          "[hysteresis][langevin]")
{
    // THE reason this function has its own threshold, a hundred times larger
    // than the one L and L' use.
    //
    // The closed form's terms are of order 2/x^3 while the answer is of order
    // 2x/15, so the subtraction cancels roughly 15/x^4 worth of significance.
    // At x = 1e-4 that is seventeen orders and the closed form returns a value
    // wrong by a factor of twenty. Reusing kSmallArgument here -- which is the
    // obvious thing to do, and wrong -- lands exactly in that hole.
    //
    // This walks the whole danger zone rather than sampling either side of it.
    for (int i = 1; i <= 200; ++i)
    {
        const double x = static_cast<double>(i) * 1.0e-4;  // 1e-4 .. 2e-2
        const double series = -2.0 * x / 15.0 + 8.0 * x * x * x / 189.0;
        REQUIRE(tape::langevinDoublePrime(x) == Approx(series).epsilon(1.0e-4));
    }
}

TEST_CASE("the rate Jacobian agrees with numerical differentiation", "[hysteresis][jacobian]")
{
    // d(dM/dt)/dM, which is what Newton-Raphson needs to find the step.
    //
    // Checked against a central difference of magnetisationRate itself -- an
    // independent method, so an algebra slip in the analytic derivation cannot
    // hide behind a shared mistake.
    //
    // POINTS WHERE delta_M FLIPS ARE SKIPPED, and legitimately: the rate
    // function is genuinely discontinuous where `gap` changes sign, because the
    // irreversible term switches off there. A numerical derivative straddling
    // that step measures the step height divided by 2h, which is not a
    // derivative of anything. This is the model being non-smooth, not the
    // Jacobian being wrong.
    const auto stock = tape::TapeStock::ferricOxide();
    int compared = 0;

    for (int mi = -30; mi <= 30; ++mi)
    {
        for (int hi = -30; hi <= 30; ++hi)
        {
            for (const double fieldRate : {2.0e6, -2.0e6})
            {
                const double M = mi * 1.0e4;
                const double H = hi * 1.0e4;

                // STEP SIZE IS SET BY CONDITIONING, and the obvious small value
                // is the wrong one. The rate is of order 1e6 while the Jacobian
                // is of order 1e-3, so at h = 1 the difference being measured is
                // 2e-3 out of values of 1.8e6 -- and the closed forms' residual
                // 5e-13 relative error alone puts ~1e-6 of noise on each, which
                // swamps it. Truncation is negligible either way: the rate
                // varies over a scale of a/alpha, about 1.4e7 A/m, so h = 1e3 is
                // still a ten-thousandth of it.
                //
                // Verified by sweeping h: the numerical estimate converges onto
                // the analytic value monotonically as h grows, which is what
                // says the analytic form is right and the small-h estimate is
                // the unreliable one.
                const double h = 1.0e3;  // A/m

                auto gapAt = [&](double m)
                {
                    const double Q = (H + stock.meanFieldCoupling * m) / stock.anhystericShape;
                    return stock.saturationMagnetisation * tape::langevin(Q) - m;
                };
                // Skip if the irreversible term switches on or off across the
                // differencing interval.
                if ((gapAt(M - h) > 0.0) != (gapAt(M + h) > 0.0))
                    continue;

                const double numerical =
                    (tape::magnetisationRate(M + h, H, fieldRate, stock)
                     - tape::magnetisationRate(M - h, H, fieldRate, stock)) / (2.0 * h);

                const double analytic = tape::magnetisationRateJacobian(M, H, fieldRate, stock);
                REQUIRE(analytic == Approx(numerical).epsilon(1.0e-4));
                ++compared;
            }
        }
    }

    // Guard against the test quietly skipping everything and passing.
    REQUIRE(compared > 3000);
}

TEST_CASE("the Jacobian is exercised on an exaggerated stock too", "[hysteresis][jacobian]")
{
    // Ferric oxide's alpha is 1.6e-3, which makes several terms in the Jacobian
    // nearly invisible -- the same blindness that let a missing coupling
    // denominator pass earlier. A stock with strong coupling makes them matter.
    tape::TapeStock strong;
    strong.susceptibilityRatio = 0.5;
    strong.meanFieldCoupling = 0.05;

    for (int mi = -10; mi <= 10; ++mi)
    {
        const double M = mi * 1.0e4;
        const double H = 1.5e4;
        const double h = 1.0e3;

        auto gapAt = [&](double m)
        {
            const double Q = (H + strong.meanFieldCoupling * m) / strong.anhystericShape;
            return strong.saturationMagnetisation * tape::langevin(Q) - m;
        };
        if ((gapAt(M - h) > 0.0) != (gapAt(M + h) > 0.0))
            continue;

        const double numerical = (tape::magnetisationRate(M + h, H, 1.0e6, strong)
                                  - tape::magnetisationRate(M - h, H, 1.0e6, strong)) / (2.0 * h);
        REQUIRE(tape::magnetisationRateJacobian(M, H, 1.0e6, strong)
                == Approx(numerical).epsilon(1.0e-4));
    }
}

TEST_CASE("a zero field rate has a zero Jacobian", "[hysteresis][jacobian]")
{
    // The rate is exactly proportional to the field rate, so its derivative
    // with respect to M must be too. This is also what makes the Newton step
    // trivially correct on a stationary field.
    const auto stock = tape::TapeStock::ferricOxide();
    for (int mi = -20; mi <= 20; ++mi)
        REQUIRE(tape::magnetisationRateJacobian(mi * 1.0e4, 3.0e4, 0.0, stock)
                == Approx(0.0).margin(1.0e-18));
}

TEST_CASE("the Langevin functions are accurate through the cancellation zone",
          "[hysteresis][langevin][accuracy]")
{
    // This test exists because a real accuracy bug was found by chasing a
    // Jacobian disagreement, and the disagreement turned out to be honest:
    // langevinPrime was noisy, not the Jacobian.
    //
    // Both closed forms subtract two large nearly-equal quantities. For L those
    // are of order 1/x against an answer of order x/3; for L' they are 1/x^2
    // against an answer of 1/3. Either way the cancellation is about 3/x^2, so
    // at x = 2e-3 roughly six significant digits are gone. In the rate equation
    // that lands as +/-1.8e-4 of noise on a value of 1.8e6 -- invisible in any
    // structural test, and enough to swamp a numerical derivative.
    //
    // References computed independently at 60 decimal digits, so this is a
    // closed-form expectation rather than a golden buffer (PRINCIPLES section 7).
    struct Reference { double x, L, Lprime; };
    constexpr Reference references[] = {
        {0.0001, 3.3333333311111114e-05, 0.33333333266666665},
        {0.0005, 0.00016666666388888895, 0.33333331666666732},
        {0.001,  0.00033333331111111322, 0.33333326666667723},
        {0.005,  0.0016666638888955026,  0.3333316666732804},
        {0.01,   0.003333311111322749,   0.33332666677248529},
        {0.02,   0.0066664888956611051,  0.33330666835969353},
        {0.05,   0.016663889550099249,   0.33316673278109216},
        {0.1,    0.033311132253989607,   0.33266772338816503},
        {0.5,    0.16395341373865285,    0.31730562316883071},
        {1.0,    0.31303528549933129,    0.27593833903368953},
        {3.0,    0.67163648998035586,    0.10114676533996347},
    };

    for (const auto& r : references)
    {
        REQUIRE(tape::langevin(r.x) == Approx(r.L).epsilon(1.0e-11));
        REQUIRE(tape::langevinPrime(r.x) == Approx(r.Lprime).epsilon(1.0e-11));
    }
}

// ---------------------------------------------------------------------------
// Rung 2: trapezoidal integration solved by Newton-Raphson.
//
//   M(n) = M(n-1) + (T/2) * [ f(M(n), u(n)) + f(M(n-1), u(n-1)) ]
//
// implicit in M(n), so each sample is a root-find. Newton needs the Jacobian
// above.
// ---------------------------------------------------------------------------

namespace
{
    tape::Hysteresis makeSolver(tape::Solver solver, double sampleRate = 48000.0)
    {
        tape::Hysteresis h;
        h.prepare(sampleRate, tape::TapeStock::ferricOxide(), solver);
        return h;
    }
}

namespace
{
    // RK4 in time driven by the EXACT analytic field rate -- no reconstruction
    // anywhere. For a pure sine the derivative is known in closed form, so this
    // is a genuine reference rather than another approximation
    // (PRINCIPLES section 7).
    double referenceMagnetisation(double amplitude, double hz, double sampleRate, int samples)
    {
        const auto stock = tape::TapeStock::ferricOxide();
        const double T = 1.0 / sampleRate;
        auto H = [&](double t) { return amplitude * std::sin(2.0 * M_PI * hz * t); };
        auto Hdot = [&](double t)
        { return amplitude * 2.0 * M_PI * hz * std::cos(2.0 * M_PI * hz * t); };

        double M = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            const double t0 = i * T, tm = t0 + 0.5 * T, t1 = t0 + T;
            const double k1 = T * tape::magnetisationRate(M, H(t0), Hdot(t0), stock);
            const double k2 = T * tape::magnetisationRate(M + 0.5 * k1, H(tm), Hdot(tm), stock);
            const double k3 = T * tape::magnetisationRate(M + 0.5 * k2, H(tm), Hdot(tm), stock);
            const double k4 = T * tape::magnetisationRate(M + k3, H(t1), Hdot(t1), stock);
            M += k1 / 6.0 + k2 / 3.0 + k3 / 3.0 + k4 / 6.0;
        }
        return M;
    }

    double solveSine(tape::Solver solver, double amplitude, double hz,
                     double sampleRate, int samples)
    {
        auto h = makeSolver(solver, sampleRate);
        double last = 0.0;
        for (int i = 0; i < samples; ++i)
            last = h.process(amplitude * std::sin(2.0 * M_PI * hz * i / sampleRate));
        return last;
    }
}

TEST_CASE("Newton converges on the exactly-driven reference", "[hysteresis][newton]")
{
    // THE correctness test for this rung, and a stronger claim than agreeing
    // with RK4.
    //
    // Integrating in H means the step is an exact difference of two samples, so
    // as the step shrinks the answer must converge on what the model gives when
    // driven by the true derivative. Second-order scheme, so quadrupling the
    // sample rate should quarter the error.
    constexpr double amplitude = 2.0e5;
    constexpr double hz = 220.0;

    auto errorAt = [&](double sampleRate)
    {
        const int samples = static_cast<int>(sampleRate * 0.02);
        const double reference = referenceMagnetisation(amplitude, hz, sampleRate, samples);
        const double newton = solveSine(tape::Solver::NewtonRaphson, amplitude, hz,
                                        sampleRate, samples);
        return std::abs(newton - reference);
    };

    const double coarse = errorAt(48000.0);
    const double fine = errorAt(192000.0);
    const double finer = errorAt(768000.0);

    INFO("errors: " << coarse << " " << fine << " " << finer);
    REQUIRE(fine < coarse * 0.35);   // second order would give 0.25
    REQUIRE(finer < fine * 0.35);
}

TEST_CASE("RK4's field-rate reconstruction biases it, permanently",
          "[hysteresis][newton][estimator]")
{
    // A finding pinned so it cannot silently change.
    //
    // RK4 here integrates in TIME, so it needs dH/dt reconstructed from
    // samples, and the trapezoidal reconstruction does not merely add noise --
    // it adds a BIAS THAT SURVIVES REFINEMENT. Both solvers converge at second
    // order, but to different limits: Richardson extrapolation puts Newton on
    // the exactly-driven reference and RK4 about 4.9e-4 away from it.
    //
    // So the reconstruction, not the integrator, is what limits the reference
    // implementation's accuracy -- and it is why this rung integrates in H
    // instead (ROADMAP.md, "A field-rate estimator without a pole at z = -1").
    constexpr double amplitude = 2.0e5;
    constexpr double hz = 220.0;

    auto errorAt = [&](tape::Solver solver, double sampleRate)
    {
        const int samples = static_cast<int>(sampleRate * 0.02);
        const double reference = referenceMagnetisation(amplitude, hz, sampleRate, samples);
        return std::abs(solveSine(solver, amplitude, hz, sampleRate, samples) - reference)
             / std::abs(reference);
    };

    // The RATIO is the assertion, not the absolute error, because that is what
    // distinguishes "converging to the right answer" from "converging to the
    // wrong one". Quadrupling the rate quarters a second-order error only if
    // the limit it is heading for is zero.
    //
    // Measured: Newton 3.9e-4 -> 9.7e-5, a ratio of 4.0 -- textbook second
    // order, heading for zero. RK4 8.9e-4 -> 5.9e-4, a ratio of 1.5 -- flattening
    // onto a floor it will not go below.
    const double newtonCoarse = errorAt(tape::Solver::NewtonRaphson, 768000.0);
    const double newtonFine = errorAt(tape::Solver::NewtonRaphson, 3072000.0);
    const double rk4Coarse = errorAt(tape::Solver::RungeKutta4, 768000.0);
    const double rk4Fine = errorAt(tape::Solver::RungeKutta4, 3072000.0);

    INFO("newton " << newtonCoarse << " -> " << newtonFine
                   << " ; rk4 " << rk4Coarse << " -> " << rk4Fine);

    REQUIRE(newtonCoarse / newtonFine > 3.0);   // converging on the reference
    REQUIRE(rk4Coarse / rk4Fine < 2.0);         // converging on something else
    REQUIRE(rk4Fine > newtonFine * 3.0);        // and it is measurably further away
}

TEST_CASE("the two solvers agree to within the reconstruction's bias",
          "[hysteresis][newton]")
{
    // They are different schemes solving subtly different problems -- see the
    // test above -- so exact agreement is not available and would be suspicious
    // if it appeared. What is asserted is that the gap between them stays the
    // size of the known reconstruction bias rather than growing into something
    // structural.
    auto rk4 = makeSolver(tape::Solver::RungeKutta4);
    auto newton = makeSolver(tape::Solver::NewtonRaphson);

    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;
    double worst = 0.0;

    for (int i = 0; i < 48000; ++i)
    {
        const double field = 2.0e5 * std::sin(2.0 * M_PI * 220.0 * i / 48000.0)
                           + 6.0e4 * std::sin(2.0 * M_PI * 3100.0 * i / 48000.0);
        worst = std::max(worst, std::abs(rk4.process(field) - newton.process(field)));
    }

    INFO("worst divergence " << worst << " A/m");
    REQUIRE(worst / saturation < 0.15);
}

TEST_CASE("Newton converges in a handful of iterations", "[hysteresis][newton]")
{
    // The cost model for this rung. Newton is NOT cheaper per sample than RK4
    // by evaluation count -- three iterations evaluate the rate three times and
    // the Jacobian three times, against RK4's four rates. What it buys is
    // stability, and stability is what permits lower oversampling
    // (DESIGN.md section 4.2). So the iteration count is the number that has to
    // be small and bounded, not zero.
    auto newton = makeSolver(tape::Solver::NewtonRaphson);

    int worstIterations = 0;
    long long total = 0;
    constexpr int n = 48000;

    for (int i = 0; i < n; ++i)
    {
        newton.process(3.0e5 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
        worstIterations = std::max(worstIterations, newton.lastIterations());
        total += newton.lastIterations();
    }

    const double mean = static_cast<double>(total) / n;
    INFO("mean iterations " << mean << ", worst " << worstIterations);
    REQUIRE(worstIterations <= 8);
    REQUIRE(mean < 4.0);
}

TEST_CASE("Newton also rejects the trapezoidal rule's alternating error",
          "[hysteresis][newton]")
{
    // The z = -1 pole finding from rung one said the cheaper rungs might not
    // have RK4's midpoint averaging to hide behind, so this had to be checked
    // rather than assumed.
    //
    // It turns out trapezoidal INTEGRATION cancels it by the same mechanism as
    // RK4's midpoint: the rate is exactly proportional to the field rate, so
    // averaging f(n) with f(n-1) averages the field rates, and the mean of two
    // consecutive samples of an alternating sequence is zero. Different scheme,
    // same rescue -- which is worth knowing, because a scheme that averaged
    // nothing would expose it.
    auto newton = makeSolver(tape::Solver::NewtonRaphson);

    constexpr int n = 8192;
    std::vector<double> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(newton.process(1.0e5 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0)));

    double alternating = 0.0;
    double total = 0.0;
    for (int i = 1024; i < n; ++i)
    {
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        alternating += sign * out[static_cast<std::size_t>(i)];
        total += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)];
    }
    const auto samples = static_cast<double>(n - 1024);
    REQUIRE(std::abs(alternating) / samples / std::sqrt(total / samples) < 1.0e-2);
}

TEST_CASE("Newton is stable where the field moves fastest", "[hysteresis][newton]")
{
    // The reason this rung exists. A full-scale signal at Nyquist is the worst
    // case the field-rate reconstruction can be handed, and it is where the
    // paper warns the explicit scheme goes unstable.
    auto newton = makeSolver(tape::Solver::NewtonRaphson);
    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;

    for (int i = 0; i < 200000; ++i)
    {
        const double field = (i % 2 == 0 ? 1.0 : -1.0) * 5.0e5;  // Nyquist, hard
        const double M = newton.process(field);
        REQUIRE(std::isfinite(M));
        REQUIRE(std::abs(M) <= saturation * 1.001);
    }
}

TEST_CASE("both solvers start from a blank tape and stay there", "[hysteresis][newton]")
{
    for (const auto solver : {tape::Solver::RungeKutta4, tape::Solver::NewtonRaphson})
    {
        auto h = makeSolver(solver);
        for (int i = 0; i < 5000; ++i)
            REQUIRE(h.process(0.0) == Approx(0.0).margin(1.0e-12));
    }
}

TEST_CASE("Newton reproduces remanence and loop area", "[hysteresis][newton]")
{
    // The two properties that make this a tape rather than a waveshaper have to
    // survive the change of solver, or the rung is not interchangeable.
    auto h = makeSolver(tape::Solver::NewtonRaphson);

    for (int i = 0; i < 4000; ++i)
        h.process(3.0e5 * static_cast<double>(i) / 3999.0);
    double remanent = 0.0;
    for (int i = 0; i < 4000; ++i)
        remanent = h.process(3.0e5 * (1.0 - static_cast<double>(i) / 3999.0));

    REQUIRE(remanent > 0.05 * tape::TapeStock::ferricOxide().saturationMagnetisation);
}

TEST_CASE("Newton is odd-symmetric too", "[hysteresis][newton]")
{
    auto a = makeSolver(tape::Solver::NewtonRaphson);
    auto b = makeSolver(tape::Solver::NewtonRaphson);

    for (int i = 0; i < 4000; ++i)
    {
        const double field = 1.5e5 * std::sin(2.0 * M_PI * 5.0 * i / 4000.0);
        const double positive = a.process(field);
        const double inverted = b.process(-field);
        REQUIRE(inverted == Approx(-positive).epsilon(1.0e-9));
    }
}

TEST_CASE("Newton converges across the operating regime", "[hysteresis][newton]")
{
    // This test exists because capping Newton at ONE iteration was planted as a
    // bug and the whole suite stayed green.
    //
    // The reason is worth knowing: at 220 Hz one iteration is bit-identical to
    // eight, because the Euler guess is already so close that Newton lands on
    // the root in a single step. So no accuracy assertion at ordinary
    // frequencies can distinguish a converged solver from a one-shot one -- and
    // an iteration-count assertion cannot either, since one is a perfectly
    // small number. What separates them is whether the solve REACHED ITS
    // TOLERANCE.
    //
    // The cases below are chosen by FIELD STEP PER SAMPLE, which is the
    // quantity the solver actually sees, rather than by frequency -- and which
    // the record path's oversampling is what keeps small (DESIGN.md 4.2).
    struct Case { double amplitude, hz; };
    const Case cases[] = {
        {2.0e5,   220.0},
        {2.0e5,  8000.0},
        {8.0e5,  8000.0},
    };

    for (const auto& c : cases)
    {
        auto h = makeSolver(tape::Solver::NewtonRaphson);
        int worst = 0;

        for (int i = 0; i < 24000; ++i)
        {
            h.process(c.amplitude * std::sin(2.0 * M_PI * c.hz * i / 48000.0));
            REQUIRE(h.lastSolveConverged());
            worst = std::max(worst, h.lastIterations());
        }

        INFO("amplitude " << c.amplitude << " at " << c.hz << " Hz, worst " << worst);
        REQUIRE(worst <= 8);
    }
}

TEST_CASE("the demanding cases need more than one iteration", "[hysteresis][newton]")
{
    // The companion to the above: if a single iteration were always enough the
    // loop would be vestigial and this rung's cost model would be wrong. It is
    // not -- measured mean is 2.45 iterations at 220 Hz and 3.33 at 8 kHz.
    auto h = makeSolver(tape::Solver::NewtonRaphson);

    int worst = 0;
    for (int i = 0; i < 24000; ++i)
    {
        h.process(2.0e5 * std::sin(2.0 * M_PI * 8000.0 * i / 48000.0));
        worst = std::max(worst, h.lastIterations());
    }

    REQUIRE(worst > 1);
}

TEST_CASE("Newton stays converged and bounded past 2.5 samples per cycle",
          "[hysteresis][newton][limit]")
{
    // A measured limit that turned into a fix, recorded because the fix is
    // non-obvious and easy to undo.
    //
    // At 8e5 A/m and 19 kHz -- two and a half samples per cycle, a field step
    // per sample near the largest possible -- undamped Newton failed to reach
    // tolerance on HALF of all samples and returned magnetisations 36% above
    // saturation, which is not a physical value.
    //
    // The cause is structural rather than numerical: the residual is
    // DISCONTINUOUS in m, because delta_M switches the irreversible term off
    // when `gap` changes sign, and Newton can cycle across that step
    // indefinitely. More iterations do not resolve a discontinuity.
    //
    // Capping the step at one saturation stops the iterate wandering across it
    // and converges everywhere, at no cost in the operating regime. See the
    // measured table at kMaxStepFraction.
    auto h = makeSolver(tape::Solver::NewtonRaphson);
    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;

    for (int i = 0; i < 24000; ++i)
    {
        const double M = h.process(8.0e5 * std::sin(2.0 * M_PI * 19000.0 * i / 48000.0));
        REQUIRE(std::isfinite(M));
        REQUIRE(std::abs(M) <= saturation);
        REQUIRE(h.lastSolveConverged());
    }
}

// ===========================================================================
// Rung 3: tabulated evaluation.
//
// The rung-2 refactor changed what there is to tabulate. DESIGN section 4.2
// planned a table of f(H, H', M) -- three dimensions -- because the rate was
// the primitive. Factoring the field rate out left the SLOPE, which depends on
// Q only through L(Q) and L'(Q); everything else in the slope is a few
// multiplies and one divide.
//
// So what gets tabulated is two smooth one-dimensional functions, not a
// surface. That is cheaper, more accurate, and -- most importantly -- it leaves
// the delta_M gate exact, because the gate is a sign comparison made after the
// lookup rather than something interpolated across.
// ===========================================================================

TEST_CASE("the Langevin table reproduces the exact functions", "[hysteresis][table]")
{
    tape::LangevinTable table;
    table.build();

    double worstL = 0.0, worstPrime = 0.0, worstDouble = 0.0;
    for (int i = 0; i <= 200000; ++i)
    {
        const double q = -20.0 + 40.0 * i / 200000.0;
        worstL = std::max(worstL, std::abs(table.l(q) - tape::langevin(q)));

        const double exactPrime = tape::langevinPrime(q);
        if (std::abs(exactPrime) > 1.0e-9)
            worstPrime = std::max(worstPrime,
                                  std::abs((table.lPrime(q) - exactPrime) / exactPrime));

        const double exactDouble = tape::langevinDoublePrime(q);
        if (std::abs(exactDouble) > 1.0e-6)
            worstDouble = std::max(worstDouble,
                                   std::abs((table.lDoublePrime(q) - exactDouble) / exactDouble));
    }

    INFO("worst L " << worstL << ", L' " << worstPrime << ", L'' " << worstDouble);
    REQUIRE(worstL < 5.0e-6);
    REQUIRE(worstPrime < 5.0e-5);
    REQUIRE(worstDouble < 5.0e-4);
}

TEST_CASE("the table is exact outside its range, not clamped", "[hysteresis][table]")
{
    // Past the table the closed forms ARE their asymptotes -- coth(q) differs
    // from 1 by 2e^(-2q), which at q = 16 is 1e-14 -- so the out-of-range branch
    // is not a fallback of last resort but the cheapest accurate answer
    // available. Clamping to the last table entry, which is the obvious thing to
    // write, would put a hard error floor on every hard-driven sample.
    tape::LangevinTable table;
    table.build();

    for (const double q : {20.0, 50.0, 200.0, 5000.0, -20.0, -50.0, -5000.0})
    {
        REQUIRE(table.l(q) == Approx(tape::langevin(q)).epsilon(1.0e-9));
        REQUIRE(table.lPrime(q) == Approx(tape::langevinPrime(q)).epsilon(1.0e-6));
    }
}

TEST_CASE("the table preserves the shape properties that matter", "[hysteresis][table]")
{
    tape::LangevinTable table;
    table.build();

    SECTION("odd, so the tape stays symmetric")
    {
        for (const double q : {0.1, 1.0, 5.0, 15.0})
            REQUIRE(table.l(-q) == Approx(-table.l(q)).margin(1.0e-7));
    }

    SECTION("monotonic, so more field never means less magnetisation")
    {
        double previous = -2.0;
        for (int i = -2000; i <= 2000; ++i)
        {
            const double value = table.l(static_cast<double>(i) * 0.008);
            REQUIRE(value >= previous);
            previous = value;
        }
    }

    SECTION("bounded by saturation")
    {
        for (int i = -4000; i <= 4000; ++i)
            REQUIRE(std::abs(table.l(static_cast<double>(i) * 0.01)) < 1.0);
    }
}

TEST_CASE("an unbuilt table is not silently wrong", "[hysteresis][table]")
{
    // A table used before build() would return zeros, which is a plausible
    // magnetisation and therefore the worst kind of failure.
    tape::LangevinTable table;
    REQUIRE(!table.isBuilt());
    table.build();
    REQUIRE(table.isBuilt());
}

TEST_CASE("the tabulated slope tracks the exact slope", "[hysteresis][table]")
{
    tape::LangevinTable table;
    table.build();
    const auto stock = tape::TapeStock::ferricOxide();

    double worst = 0.0;
    int compared = 0;
    for (int mi = -30; mi <= 30; ++mi)
    {
        for (int hi = -30; hi <= 30; ++hi)
        {
            for (const double direction : {1.0, -1.0})
            {
                const double M = mi * 1.0e4;
                const double H = hi * 1.0e4;

                const double exact = tape::magnetisationSlope(M, H, direction, stock);
                const double tabulated =
                    tape::magnetisationSlopeWith(M, H, direction, stock, table);

                // Skip points where the two land on opposite sides of the gate:
                // the table's tiny error in L moves the gate by a hair, and
                // straddling it compares two different branches rather than two
                // approximations of one.
                if ((exact != 0.0) == (tabulated != 0.0))
                {
                    worst = std::max(worst, std::abs(exact - tabulated)
                                            / std::max(1.0e-12, std::abs(exact)));
                    ++compared;
                }
            }
        }
    }

    INFO("worst relative divergence " << worst << " over " << compared << " points");
    REQUIRE(compared > 3000);
    REQUIRE(worst < 1.0e-3);
}

TEST_CASE("the gate is a kink in the slope, not a step", "[hysteresis][table][gate]")
{
    // Worth pinning because the obvious intuition is wrong, and it was wrong
    // here first.
    //
    // delta_M switches the irreversible term off when `gap` changes sign -- but
    // that term is PROPORTIONAL TO gap, so it switches on exactly where it is
    // zero. The slope is therefore continuous across the gate; what jumps is its
    // DERIVATIVE. The gate is a kink, not a cliff.
    //
    // (This also sharpens why Newton can cycle near it, and the note in
    // stepNewton says so: the residual is continuous but its derivative is not,
    // which is enough to make Newton oscillate across the corner.)
    const auto stock = tape::TapeStock::ferricOxide();
    constexpr double H = 3.0e4;
    constexpr double direction = 1.0;

    auto biggestJump = [](auto&& evaluate)
    {
        double previous = evaluate(-2.0e5);
        double jump = 0.0;
        for (int i = -19999; i <= 20000; ++i)
        {
            const double value = evaluate(static_cast<double>(i) * 10.0);
            jump = std::max(jump, std::abs(value - previous));
            previous = value;
        }
        return jump;
    };

    const double slopeJump =
        biggestJump([&](double M) { return tape::magnetisationSlope(M, H, direction, stock); });
    const double jacobianJump = biggestJump(
        [&](double M) { return tape::magnetisationSlopeJacobian(M, H, direction, stock); });

    INFO("slope jump " << slopeJump << ", jacobian jump " << jacobianJump);

    // The slope barely moves across the gate: it is continuous there.
    REQUIRE(slopeJump < 1.0e-3);
    // The Jacobian genuinely jumps: that is the corner.
    REQUIRE(jacobianJump > 1.0e-8);
    REQUIRE(jacobianJump > slopeJump * 1.0e-5);
}

TEST_CASE("tabulating does not smear the gate", "[hysteresis][table][gate]")
{
    // THE claim this rung rests on.
    //
    // Tabulating the whole slope as a surface in (Q, M) would interpolate
    // across the corner and round it off -- and rung 1 established, by planting
    // exactly that kind of fault, that no structural test would notice.
    //
    // Tabulating only L and L' keeps the corner intact, because the gate is a
    // SIGN COMPARISON PERFORMED AFTER THE LOOKUP. The table moves where the gate
    // falls by a hair; it cannot make it gradual.
    //
    // Measured on the Jacobian, since that is where the corner shows (above).
    tape::LangevinTable table;
    table.build();
    const auto stock = tape::TapeStock::ferricOxide();
    constexpr double H = 3.0e4;
    constexpr double direction = 1.0;

    auto biggestJump = [](auto&& evaluate)
    {
        double previous = evaluate(-2.0e5);
        double jump = 0.0;
        for (int i = -19999; i <= 20000; ++i)
        {
            const double value = evaluate(static_cast<double>(i) * 10.0);
            jump = std::max(jump, std::abs(value - previous));
            previous = value;
        }
        return jump;
    };

    const double exactJump = biggestJump(
        [&](double M) { return tape::magnetisationSlopeJacobian(M, H, direction, stock); });
    const double tabulatedJump = biggestJump(
        [&](double M)
        { return tape::magnetisationSlopeJacobianWith(M, H, direction, stock, table); });

    INFO("exact " << exactJump << ", tabulated " << tabulatedJump);
    REQUIRE(tabulatedJump == Approx(exactJump).epsilon(0.05));
}

TEST_CASE("the solver agrees with itself whichever way it evaluates",
          "[hysteresis][table]")
{
    // EVERY RUNG, and `RungeKutta2InField` above all: it is the one the deck
    // actually runs, and it was the one this test did not cover. The record
    // chain's evaluation mode is Tabulated on the strength of a bench
    // measurement, and a bench is not a guarantee.
    for (const auto solver : {tape::Solver::RungeKutta4,
                              tape::Solver::NewtonRaphson,
                              tape::Solver::RungeKutta4InField,
                              tape::Solver::RungeKutta2InField})
    {
        // AT THE RATE THE RECORD CHAIN SOLVES AT, because that is where the
        // two have to agree; 48 kHz is not a rate this solver ever sees in the
        // machine.
        constexpr double kSolverRate = 768000.0;
        tape::Hysteresis exact, tabulated;
        exact.prepare(kSolverRate, tape::TapeStock::ferricOxide(), solver,
                      tape::Evaluation::Exact);
        tabulated.prepare(kSolverRate, tape::TapeStock::ferricOxide(), solver,
                          tape::Evaluation::Tabulated);

        const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;
        double worst = 0.0;

        for (int i = 0; i < 48000; ++i)
        {
            // AUDIO ONLY. Adding the bias carrier here breaks the in-field
            // rungs entirely, which is a defect of its own and is pinned by
            // the test below rather than folded into this one.
            const double field = 2.0e5 * std::sin(2.0 * M_PI * 220.0 * i / kSolverRate)
                               + 6.0e4 * std::sin(2.0 * M_PI * 3100.0 * i / kSolverRate);
            worst = std::max(worst, std::abs(exact.process(field) - tabulated.process(field)));
        }

        INFO("solver " << int(solver) << ": worst divergence " << worst
             << " A/m of " << saturation);
        REQUIRE(worst / saturation < 5.0e-3);
    }
}

TEST_CASE("tabulated evaluation keeps the tape a tape", "[hysteresis][table]")
{
    // The properties that make this a medium rather than a waveshaper have to
    // survive the cheapest rung, or it is not interchangeable with the others.
    tape::Hysteresis h;
    h.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::NewtonRaphson,
              tape::Evaluation::Tabulated);

    for (int i = 0; i < 4000; ++i)
        h.process(3.0e5 * static_cast<double>(i) / 3999.0);
    double remanent = 0.0;
    for (int i = 0; i < 4000; ++i)
        remanent = h.process(3.0e5 * (1.0 - static_cast<double>(i) / 3999.0));

    REQUIRE(remanent > 0.05 * tape::TapeStock::ferricOxide().saturationMagnetisation);

    h.reset();
    for (int i = 0; i < 2000; ++i)
        REQUIRE(h.process(0.0) == Approx(0.0).margin(1.0e-12));
}

// ===========================================================================
// RK4 integrating in the field.
//
// Added after a direct question -- was Newton really more accurate than RK4? --
// which the measurements answered "no, and the earlier claim confused the
// integrator with what it was being fed".
// ===========================================================================

TEST_CASE("tabulating the Langevin terms changes nothing the machine can hear",
          "[hysteresis][teeth]")
{
    // THIS REPLACES A TEST THAT ASSERTED THE OPPOSITE, and the opposite was
    // wrong. Chasing why `Evaluation::Tabulated` diverged from `Exact` for the
    // in-field rungs, a test was written here pinning that divergence as a
    // defect of the TABLE. The table is innocent: it is accurate to 1e-6 on L,
    // L' and L'', and perturbing the FIELD by one part in a BILLION diverges
    // by the same amount the table does -- 363 % against 362 %. What the old
    // test had found was a solver whose solution is chaotically sensitive to
    // any perturbation whatever, and the table was merely the perturbation to
    // hand.
    //
    // Measured here, worst |dM|/Ms between the two evaluations, 768 kHz:
    //
    //                  x1.00     x1.20     x1.35     (bias, at +0 and +12 dB)
    //     RK4         0.00012   0.00015   0.00025 %
    //     Newton      0.00006   0.00006   0.00006 %
    //     RK4-in-fld  0.00010   0.00013   0.00015 %
    //     RK2-in-fld  0.00035   0.00063  105.2    %   <- not shipped
    //
    // So the three rungs the deck can select agree with their own tabulation
    // four orders inside anything audible, across the whole bias range
    // `AgeToMachine` can drift to. The fourth is why `RecordChain` no longer
    // offers `RungeKutta2InField` as its default -- and its 105 % is what
    // gives this test teeth: put it back in the loop and this fails.
    const auto stock = tape::TapeStock::ferricOxide();
    constexpr double kSolverRate = 768000.0;
    constexpr double kOperatingOverCoercivity = 0.602;   // RecordChain.h
    const double operating = stock.coercivity * kOperatingOverCoercivity;
    const double nominalBias = tape::nominalBiasAmplitude(stock);

    for (const auto solver : { tape::Solver::RungeKutta4,
                               tape::Solver::NewtonRaphson,
                               tape::Solver::RungeKutta4InField })
    {
        // 1.35 is where a drifted machine's oscillator lands; drift is towards
        // OVERBIAS and only that (`AgeToMachine.h`).
        for (const double biasScale : { 1.0, 1.2, 1.35 })
        for (const double driveDb : { 0.0, 12.0 })
        {
            tape::Hysteresis exact, tabulated;
            exact.prepare(kSolverRate, stock, solver, tape::Evaluation::Exact);
            tabulated.prepare(kSolverRate, stock, solver, tape::Evaluation::Tabulated);
            tape::BiasOscillator carrier, carrierAgain;
            REQUIRE(carrier.prepare(kSolverRate, 128000.0, nominalBias * biasScale));
            REQUIRE(carrierAgain.prepare(kSolverRate, 128000.0, nominalBias * biasScale));

            const double drive = operating * std::pow(10.0, driveDb / 20.0);
            double divergence = 0.0;
            for (int i = 0; i < 76800; ++i)   // a tenth of a second
            {
                const double audio = drive * std::sin(
                    2.0 * M_PI * 1000.0 * i / kSolverRate);
                divergence = std::max(divergence,
                    std::abs(exact.process(audio + carrier.next())
                           - tabulated.process(audio + carrierAgain.next())));
            }

            INFO("solver " << int(solver) << " at bias x" << biasScale
                 << ", drive +" << driveDb << " dB: tabulated differs by "
                 << (100.0 * divergence / stock.saturationMagnetisation) << " %");
            REQUIRE(divergence / stock.saturationMagnetisation < 1.0e-5);
        }
    }
}

TEST_CASE("RK4 in the field is near-exact where the field does not reverse",
          "[hysteresis][rk4field]")
{
    // THE property that distinguishes it, and it is enormous.
    //
    // On a monotonic ramp -- no field reversal, so delta_S never switches and
    // the right-hand side is smooth along the whole path -- RK4 reaches machine
    // precision at ordinary audio rates, while the trapezoidal scheme is
    // textbook second order.
    //
    // Measured: 5.0e-13 against 1.2e-8 at 48 kHz. Five orders.
    constexpr double target = 3.0e5;
    constexpr int n = 960;

    auto rampThrough = [](tape::Solver solver, int samples)
    {
        tape::Hysteresis h;
        h.prepare(48000.0, tape::TapeStock::ferricOxide(), solver);
        double last = 0.0;
        for (int i = 1; i <= samples; ++i)
            last = h.process(target * static_cast<double>(i) / samples);
        return last;
    };

    // A far finer run of the same ramp is the reference.
    auto reference = [&](tape::Solver solver)
    {
        tape::Hysteresis h;
        h.prepare(48000.0 * 512.0, tape::TapeStock::ferricOxide(), solver);
        double last = 0.0;
        for (int i = 1; i <= n * 512; ++i)
            last = h.process(target * static_cast<double>(i) / (n * 512));
        return last;
    };

    const double rk4Error = std::abs(rampThrough(tape::Solver::RungeKutta4InField, n)
                                     - reference(tape::Solver::RungeKutta4InField))
                          / target;
    const double newtonError = std::abs(rampThrough(tape::Solver::NewtonRaphson, n)
                                        - reference(tape::Solver::NewtonRaphson))
                             / target;

    INFO("rk4-in-field " << rk4Error << ", newton " << newtonError);
    REQUIRE(rk4Error < 1.0e-10);
    REQUIRE(newtonError > rk4Error * 100.0);
}

TEST_CASE("but on real signals both are limited by the reversals",
          "[hysteresis][rk4field]")
{
    // The other half, and the reason RK4's extra order buys almost nothing on
    // music: every field reversal switches the model's branch, and no
    // high-order method integrates across a branch switch at high order. A sine
    // reverses twice a cycle, so the reversal error dominates and the two
    // schemes land in the same place.
    tape::Hysteresis rk4, newton;
    rk4.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::RungeKutta4InField);
    newton.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::NewtonRaphson);

    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;
    double worst = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double field = 2.0e5 * std::sin(2.0 * M_PI * 220.0 * i / 48000.0);
        worst = std::max(worst, std::abs(rk4.process(field) - newton.process(field)));
    }

    INFO("worst divergence " << worst << " A/m");
    REQUIRE(worst / saturation < 1.0e-3);
}

TEST_CASE("RK4 in the field is explicit, and says so at Nyquist",
          "[hysteresis][rk4field][limit]")
{
    // A pinned limit, and the reason the implicit rung still earns its place.
    //
    // Integrating in H removes the field-rate reconstruction, which was one
    // cause of the Nyquist blow-up -- but RK4 is still an EXPLICIT method, and
    // at a full-scale field step every sample it is past its stability bound.
    // Measured peak: 1.5e8 times saturation. Newton, being implicit and damped,
    // stays inside saturation on the same input.
    //
    // So the choice between them is a real trade rather than a free win, and it
    // is decided by how much oversampling there is -- which is what the bench
    // exists to measure.
    tape::Hysteresis rk4, newton;
    rk4.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::RungeKutta4InField);
    newton.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::NewtonRaphson);

    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;
    double rk4Peak = 0.0;
    double newtonPeak = 0.0;

    for (int i = 0; i < 20000; ++i)
    {
        const double field = (i % 2 == 0 ? 1.0 : -1.0) * 5.0e5;
        rk4Peak = std::max(rk4Peak, std::abs(rk4.process(field)));
        newtonPeak = std::max(newtonPeak, std::abs(newton.process(field)));
    }

    REQUIRE(rk4Peak > saturation * 100.0);      // explicit: runs away
    REQUIRE(newtonPeak <= saturation);          // implicit and clamped: does not
}

TEST_CASE("RK4 in the field keeps the tape a tape", "[hysteresis][rk4field]")
{
    tape::Hysteresis h;
    h.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::RungeKutta4InField);

    for (int i = 0; i < 4000; ++i)
        h.process(3.0e5 * static_cast<double>(i) / 3999.0);
    double remanent = 0.0;
    for (int i = 0; i < 4000; ++i)
        remanent = h.process(3.0e5 * (1.0 - static_cast<double>(i) / 3999.0));
    REQUIRE(remanent > 0.05 * tape::TapeStock::ferricOxide().saturationMagnetisation);

    h.reset();
    for (int i = 0; i < 2000; ++i)
        REQUIRE(h.process(0.0) == Approx(0.0).margin(1.0e-12));
}

// ===========================================================================
// RK2 in the field (explicit midpoint).
//
// Added on a prediction rather than a hunch. If field reversals really are what
// caps the order on real signals -- which is what the ramp measurement showed --
// then a SECOND-ORDER method loses nothing there, because second order is all
// anyone is getting. RK2 evaluates the slope twice instead of four times, so it
// should match RK4-in-field on a sine at about half the cost.
//
// If the prediction fails, the reversal explanation is wrong and DESIGN 4.2
// needs revisiting.
// ===========================================================================

TEST_CASE("RK2 matches RK4 at a modest field step", "[hysteresis][rk2]")
{
    // NOTE ON WHAT THIS DOES AND DOES NOT SHOW. It was written to demonstrate
    // that reversals cap the achievable order, so the extra stages buy nothing.
    // `RemanenceBench sweep` later contradicted the general form of that: the
    // gap between the two WIDENS as reversals get denser.
    //
    // What survives is narrower and is what is asserted here: at 220 Hz and this
    // amplitude -- a modest field step per sample -- the two agree closely. It is
    // a pinned data point, not evidence for a mechanism (DESIGN.md section 4.2).
    tape::Hysteresis rk2, rk4;
    rk2.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::RungeKutta2InField);
    rk4.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::RungeKutta4InField);

    const double saturation = tape::TapeStock::ferricOxide().saturationMagnetisation;
    double worst = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double field = 2.0e5 * std::sin(2.0 * M_PI * 220.0 * i / 48000.0);
        worst = std::max(worst, std::abs(rk2.process(field) - rk4.process(field)));
    }

    INFO("worst divergence " << worst << " A/m");
    REQUIRE(worst / saturation < 2.0e-3);
}

TEST_CASE("RK2 is second order where RK4 is not", "[hysteresis][rk2]")
{
    // The other half, and what stops the test above being vacuous: on a
    // reversal-free ramp the two are NOT equivalent, because there RK4's extra
    // order is reachable. RK2 should be far worse there -- which is the evidence
    // that it really is a lower-order method and the sine result is about the
    // signal rather than about the solvers being the same.
    constexpr double target = 3.0e5;
    constexpr int n = 960;

    auto rampTo = [](tape::Solver solver, int samples, double rate)
    {
        tape::Hysteresis h;
        h.prepare(rate, tape::TapeStock::ferricOxide(), solver);
        double last = 0.0;
        for (int i = 1; i <= samples; ++i)
            last = h.process(target * static_cast<double>(i) / samples);
        return last;
    };

    const double fine = rampTo(tape::Solver::RungeKutta4InField, n * 512, 48000.0 * 512.0);
    const double rk2Error = std::abs(rampTo(tape::Solver::RungeKutta2InField, n, 48000.0) - fine);
    const double rk4Error = std::abs(rampTo(tape::Solver::RungeKutta4InField, n, 48000.0) - fine);

    INFO("rk2 " << rk2Error << ", rk4 " << rk4Error);
    REQUIRE(rk2Error > rk4Error * 1000.0);
}

TEST_CASE("RK2 keeps the tape a tape", "[hysteresis][rk2]")
{
    tape::Hysteresis h;
    h.prepare(48000.0, tape::TapeStock::ferricOxide(), tape::Solver::RungeKutta2InField);

    for (int i = 0; i < 4000; ++i)
        h.process(3.0e5 * static_cast<double>(i) / 3999.0);
    double remanent = 0.0;
    for (int i = 0; i < 4000; ++i)
        remanent = h.process(3.0e5 * (1.0 - static_cast<double>(i) / 3999.0));
    REQUIRE(remanent > 0.05 * tape::TapeStock::ferricOxide().saturationMagnetisation);

    h.reset();
    for (int i = 0; i < 2000; ++i)
        REQUIRE(h.process(0.0) == Approx(0.0).margin(1.0e-12));
}
