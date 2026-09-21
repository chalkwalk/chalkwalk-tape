// Where the decimator's cutoff has to sit, which is a TAPE question.
//
// The decimator itself is chalkwalk-dsp's and knows nothing about heads. What
// wavelength it must pass is set by the reproduce response, so this one case
// needs both libraries and lives on the side that owns the physics.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/dsp/Decimator.h>
#include <chalkwalk/tape/LossEffects.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::tape;
namespace dsp = chalkwalk::dsp;

namespace
{
    constexpr double k15ips = 0.381;
}

TEST_CASE("lambda_cut is the FIRST crossing, not the deepest", "[decimator][sources]")
{
    // DESIGN 6.0 asks for this by name. Gap loss has sidelobes, so the
    // reproduce response climbs back after its first null: a search for a
    // deeper floor walks past the null and returns a much shorter wavelength,
    // "not because more resolution is needed, but because the search found a
    // sidelobe". It produces a plausible number, which is what makes it
    // dangerous.
    //
    // This pins both halves: that the first crossing is where DESIGN 6.0's
    // table says, and that the response really does climb back afterwards --
    // without which the warning would be about nothing.
    // MAGNITUDE, AND THE abs() IS NOT DECORATION. playbackResponse is a signed
    // transfer function: gap loss is a sinc, so past the first null the
    // response is NEGATIVE. A threshold search without the abs compares a
    // negative number against a positive threshold and reports that everything
    // past the null is below the floor; a dB conversion without it produces
    // NaN. Both were hit writing this test.
    tape::HeadGeometry head;   // Capstan defaults: 6 um repro gap
    const auto responseAt = [&head](double lambda)
    {
        return std::abs(tape::playbackResponse(k15ips / lambda, k15ips, head));
    };

    // Walk DOWN in wavelength and take the first crossing of -60 dB.
    const double threshold = std::pow(10.0, -60.0 / 20.0);
    double first = 0.0;
    for (double lambda = 50.0e-6; lambda > 0.5e-6; lambda -= 1.0e-9)
        if (responseAt(lambda) < threshold)
        {
            first = lambda;
            break;
        }

    // 6.725 um. This read 6.78 um while head-to-tape spacing was the old 1.0 um
    // default; sourcing it to Perry's 10 uin asperity (0.254 um, SOURCES section
    // 25) lifted the whole response and moved the crossing in by 55 nm. The
    // figure is dominated by the GAP, which is why it barely moved -- if
    // sourcing the spacing had moved it far, the gap would not have been what
    // set lambda_cut and DESIGN 6.0's reasoning would need revisiting.
    INFO("first crossing at " << first * 1.0e6 << " um");
    REQUIRE(first == Approx(6.725e-6).margin(0.15e-6));

    // The sidelobe is real, and large: past the first null the response climbs
    // back to -35.3 dB at 4.92 um, nearly twenty-five decibels above the floor it
    // had just crossed. That is what a search taking the LAST crossing, or a
    // deepest-point search, would walk into -- and sourcing the spacing made it
    // WORSE, from 17 dB above the floor to 25, because less spacing loss means
    // less of the sidelobe is buried.
    double peak = 0.0;
    double peakAt = 0.0;
    for (double lambda = first - 1.0e-9; lambda > 0.5e-6; lambda -= 1.0e-9)
        if (responseAt(lambda) > peak)
        {
            peak = responseAt(lambda);
            peakAt = lambda;
        }

    INFO("highest sidelobe " << 20.0 * std::log10(peak) << " dB at "
         << peakAt * 1.0e6 << " um");
    REQUIRE(peak > threshold);                                  // it climbs back
    REQUIRE(20.0 * std::log10(peak) == Approx(-35.3).margin(1.0));
}
