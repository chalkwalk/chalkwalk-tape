// Minimum-phase design from a magnitude (chalkwalk/tape/MinimumPhase.h, SOURCES 15).
//
// The transform is tested against a naive DFT, which shares none of its
// machinery, and the design is tested against a CLOSED FORM rather than a
// golden buffer: the minimum-phase filter whose magnitude is a one-pole
// lowpass's is that one-pole's impulse response, a^n, exactly. That is the
// assertion with teeth here -- a design that came back with the right magnitude
// and the wrong phase would match a magnitude check and fail this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/MinimumPhase.h>

#include <cmath>
#include <complex>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;

namespace
{
    // The transform written the obvious way, for the fast one to be checked
    // against. Shares no code with it, which is the point.
    std::vector<std::complex<double>> naiveDft(
        const std::vector<std::complex<double>>& x, bool inverse)
    {
        const std::size_t n = x.size();
        std::vector<std::complex<double>> out(n);
        for (std::size_t k = 0; k < n; ++k)
        {
            std::complex<double> sum{ 0.0, 0.0 };
            for (std::size_t i = 0; i < n; ++i)
            {
                const double angle = (inverse ? 2.0 : -2.0) * M_PI
                    * static_cast<double>(k) * static_cast<double>(i)
                    / static_cast<double>(n);
                sum += x[i] * std::complex<double>{ std::cos(angle), std::sin(angle) };
            }
            out[k] = inverse ? sum / static_cast<double>(n) : sum;
        }
        return out;
    }
}

TEST_CASE("the transform matches a naive DFT, in both directions",
          "[minphase][fft]")
{
    std::vector<std::complex<double>> x(64);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = { std::sin(0.37 * static_cast<double>(i)) + 0.5,
                 std::cos(0.11 * static_cast<double>(i)) };

    for (const bool inverse : { false, true })
    {
        auto fast = x;
        tape::fft(fast, inverse);
        const auto slow = naiveDft(x, inverse);
        for (std::size_t k = 0; k < x.size(); ++k)
        {
            INFO((inverse ? "inverse" : "forward") << " bin " << k);
            REQUIRE(fast[k].real() == Approx(slow[k].real()).margin(1.0e-9));
            REQUIRE(fast[k].imag() == Approx(slow[k].imag()).margin(1.0e-9));
        }
    }
}

TEST_CASE("the transform's direction is pinned, not just its magnitude",
          "[minphase][fft]")
{
    // Flipping the sign of the twiddle angle conjugates the result and leaves
    // every magnitude untouched, so a magnitude-only check passes with the
    // transform running backwards. A sine's bin has to be NEGATIVE imaginary
    // under the forward transform.
    const std::size_t n = 32;
    std::vector<std::complex<double>> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = std::sin(2.0 * M_PI * 3.0 * static_cast<double>(i)
                        / static_cast<double>(n));
    tape::fft(x, false);
    INFO("bin 3 = " << x[3].real() << " + " << x[3].imag() << "i");
    REQUIRE(x[3].real() == Approx(0.0).margin(1.0e-9));
    REQUIRE(x[3].imag() < -1.0);

    // And a round trip is the identity.
    std::vector<std::complex<double>> y(n);
    for (std::size_t i = 0; i < n; ++i)
        y[i] = { static_cast<double>(i) * 0.25, -0.5 * static_cast<double>(i) };
    auto z = y;
    tape::fft(z, false);
    tape::fft(z, true);
    for (std::size_t i = 0; i < n; ++i)
        REQUIRE(z[i].real() == Approx(y[i].real()).margin(1.0e-9));
}

TEST_CASE("a one-pole magnitude designs back into a one-pole impulse response",
          "[minphase][teeth]")
{
    // THE CLOSED FORM. H(z) = 1/(1 - a z^-1) is minimum phase for |a| < 1, its
    // impulse response is a^n, and its magnitude at frequency f is
    // 1/|1 - a exp(-j 2 pi f / fs)|. Design from that magnitude and the answer
    // has to come back a^n -- which pins the PHASE, not merely the magnitude.
    const double a = 0.8;
    const double fs = 1000.0;
    const auto magnitude = [&](double hz)
    {
        const double w = 2.0 * M_PI * hz / fs;
        const std::complex<double> pole =
            1.0 - a * std::complex<double>{ std::cos(w), -std::sin(w) };
        return 1.0 / std::abs(pole);
    };

    const auto h = tape::designMinimumPhase(48, fs, magnitude);
    REQUIRE(h.size() == 48);
    for (std::size_t n = 0; n < 20; ++n)
    {
        INFO("tap " << n << ": got " << h[n] << ", a^n = " << std::pow(a, n));
        REQUIRE(h[n] == Approx(std::pow(a, static_cast<double>(n))).margin(1.0e-6));
    }
}

TEST_CASE("the design is minimum phase, not merely the right magnitude",
          "[minphase][teeth]")
{
    // The same magnitude realised symmetrically has the same spectrum and a
    // completely different impulse response. This is what distinguishes the two,
    // and it is the property the live crosstalk path is buying: the energy is at
    // the FRONT.
    const double a = 0.85;
    const double fs = 1000.0;
    const auto magnitude = [&](double hz)
    {
        const double w = 2.0 * M_PI * hz / fs;
        const std::complex<double> pole =
            1.0 - a * std::complex<double>{ std::cos(w), -std::sin(w) };
        return 1.0 / std::abs(pole);
    };

    const int taps = 128;
    const auto minimum = tape::designMinimumPhase(taps, fs, magnitude);

    // The symmetric realisation of the same magnitude, by inverse cosine
    // transform -- the design CrosstalkFilter uses for the recorded path.
    std::vector<double> symmetric(static_cast<std::size_t>(taps));
    const int half = taps / 2;
    const int points = 4096;
    for (int n = -half; n < taps - half; ++n)
    {
        double sum = 0.0;
        for (int k = 0; k <= points; ++k)
        {
            const double hz = static_cast<double>(k) / points * 0.5 * fs;
            const double weight = (k == 0 || k == points) ? 0.5 : 1.0;
            sum += weight * magnitude(hz)
                 * std::cos(2.0 * M_PI * hz * n / fs);
        }
        symmetric[static_cast<std::size_t>(n + half)] = sum / points;
    }

    const double minimumDelay = tape::medianEnergyDelay(minimum);
    const double symmetricDelay = tape::medianEnergyDelay(symmetric);
    INFO("minimum phase " << minimumDelay << " samples, symmetric "
         << symmetricDelay);
    REQUIRE(symmetricDelay > 50.0);         // half its length, as it must be
    REQUIRE(minimumDelay < 10.0);           // and the other one is at the front

    // Same magnitude, though, which is what makes the substitution legitimate.
    const auto magnitudeOf = [&](const std::vector<double>& h, double hz)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i)
        {
            const double angle = -2.0 * M_PI * hz * static_cast<double>(i) / fs;
            re += h[i] * std::cos(angle);
            im += h[i] * std::sin(angle);
        }
        return std::sqrt(re * re + im * im);
    };
    for (const double hz : {10.0, 50.0, 150.0, 400.0})
    {
        INFO(hz << " Hz");
        REQUIRE(magnitudeOf(minimum, hz)
                == Approx(magnitude(hz)).epsilon(0.05));
        REQUIRE(magnitudeOf(symmetric, hz)
                == Approx(magnitude(hz)).epsilon(0.05));
    }
}

TEST_CASE("a short transform aliases the cepstrum, and the default does not",
          "[minphase][teeth]")
{
    // The cepstrum lives on a circular grid, so a transform barely longer than
    // the filter wraps the impulse response's tail onto its head. This is the
    // reason for the sixteen-times default, and it is measured rather than
    // asserted: the closed form above is the reference, so the damage is
    // visible as departure from a^n.
    const double a = 0.9;
    const double fs = 1000.0;
    const auto magnitude = [&](double hz)
    {
        const double w = 2.0 * M_PI * hz / fs;
        return 1.0 / std::abs(1.0 - a * std::complex<double>{ std::cos(w),
                                                              -std::sin(w) });
    };

    const auto worstError = [&](const std::vector<double>& h)
    {
        double worst = 0.0;
        for (std::size_t n = 0; n < h.size(); ++n)
            worst = std::max(worst,
                             std::abs(h[n] - std::pow(a, static_cast<double>(n))));
        return worst;
    };

    const auto tight = tape::designMinimumPhase(64, fs, magnitude, 64);
    const auto roomy = tape::designMinimumPhase(64, fs, magnitude);
    INFO("64-point transform: worst " << worstError(tight)
         << ", default: " << worstError(roomy));
    REQUIRE(worstError(tight) > 20.0 * worstError(roomy));
    REQUIRE(worstError(roomy) < 1.0e-4);
}

TEST_CASE("the energy median is where the energy is", "[minphase]")
{
    // A closed form again: for a^n the cumulative energy passes half at
    // n = log(0.5)/log(a^2) - 1, rounded up.
    const double a = 0.5;
    std::vector<double> h(32);
    for (std::size_t n = 0; n < h.size(); ++n)
        h[n] = std::pow(a, static_cast<double>(n));
    // Energy is a^(2n), summing to 1/(1-a^2); half is reached once
    // (1 - a^(2(n+1)))/(1-a^2) >= 0.5/(1-a^2), i.e. a^(2(n+1)) <= 0.5.
    const double exact = std::ceil(std::log(0.5) / (2.0 * std::log(a))) - 1.0;
    REQUIRE(tape::medianEnergyDelay(h) == Approx(exact));

    // An impulse is at zero, and silence has no answer rather than a wrong one.
    REQUIRE(tape::medianEnergyDelay({ 1.0, 0.0, 0.0 }) == Approx(0.0));
    REQUIRE(tape::medianEnergyDelay({ 0.0, 0.0 }) == Approx(0.0));
}
