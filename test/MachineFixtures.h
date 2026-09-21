#pragma once

// REALISTIC NUMBERS, SO THE ASSERTIONS DID NOT HAVE TO CHANGE.
//
// The suites in this directory for `Transport` and `WearMap`
// were written in Remanence against `capstanGeometry()` -- a sixteen-track
// studio deck -- and moved here when those headers did. This library has no
// machine catalogue and should not grow one: which decks exist is an editorial
// question about a plugin, not a property of tape.
//
// So the handful of figures those tests actually read are transcribed here,
// and every assertion they support moved UNCHANGED. That is the point: a test
// rewritten to accommodate a move has stopped being evidence that the move was
// safe.
//
// THESE ARE A FIXTURE, NOT AN AUTHORITY. Remanence's `MachineGeometry.h` is
// where these numbers are sourced, argued and maintained; anything here that
// drifts from it is this file's bug. Nothing in the library reads them.

#include <chalkwalk/tape/LossEffects.h>
#include <chalkwalk/tape/Transport.h>

namespace fixtures
{
    // Inches per second, in metres.
    [[nodiscard]] constexpr double ips(double inches) noexcept
    {
        return inches * 25.4e-3;
    }

    // Thousandths of an inch, in metres.
    [[nodiscard]] constexpr double mils(double thou) noexcept
    {
        return thou * 25.4e-6;
    }

    inline constexpr double kCapstanSpeed = ips(15.0);
    inline constexpr double kCapstanSlowSpeed = ips(7.5);
    inline constexpr double kCapstanFastSpeed = ips(30.0);

    // Capstan's reproduce head: 70 mil track, 6 um gap, 12 dB of shielding.
    [[nodiscard]] inline chalkwalk::tape::HeadGeometry capstanRepro() noexcept
    {
        chalkwalk::tape::HeadGeometry h;
        h.trackWidthMetres = mils(70.0);
        h.guardBandMetres = mils(57.0);
        h.thicknessMetres = 12.7e-6;
        h.spacingMetres = 0.254e-6;
        h.gapLengthMetres = 6.0e-6;
        h.shieldingDb = 12.0;
        return h;
    }

    // Capstan's transport: a 6 mm capstan, a seven-inch reel, a 25 Hz flywheel.
    [[nodiscard]] inline chalkwalk::tape::TransportGeometry capstanTransport() noexcept
    {
        chalkwalk::tape::TransportGeometry t;
        t.capstanDiameterMetres = 6.0e-3;
        t.pinchDiameterMetres = 10.0e-3;
        t.hubRadiusMetres = 26.0e-3;
        t.fullPackRadiusMetres = 89.0e-3;
        t.tapeThicknessMetres = 35.0e-6;
        t.flywheelCornerHz = 25.0;
        t.flywheelQ = 0.8;
        t.lineFrequencyHz = 50.0;
        t.unsupportedSpanMetres = 85.0e-3;
        return t;
    }

    // Slipback's reproduce head and speed: a quarter-inch echo unit, 7.5 ips,
    // a 3 um gap and a thinner coating than a studio deck's. Here because one
    // wear case needs a SECOND machine -- the point it makes is that the
    // spallation differential can go positive on a short wavelength, and that
    // only shows on a head and a speed unlike Capstan's.
    inline constexpr double kSlipbackSpeed = 0.19;

    [[nodiscard]] inline chalkwalk::tape::HeadGeometry slipbackRepro() noexcept
    {
        chalkwalk::tape::HeadGeometry h;
        h.trackWidthMetres = mils(75.0);
        h.guardBandMetres = mils(84.0);
        h.thicknessMetres = 9.6e-6;
        h.spacingMetres = 0.254e-6;
        h.gapLengthMetres = 3.0e-6;
        return h;
    }

    // A machine whose transport nobody measured. Default-constructed on
    // purpose: `modelled()` returning false is the property under test, and
    // spelling out zeroes here would hide which field decides it.
    [[nodiscard]] inline chalkwalk::tape::TransportGeometry unmodelledTransport() noexcept
    {
        return {};
    }
}
