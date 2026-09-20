#pragma once

// Reproduce-side loss effects (DESIGN.md section 4.3).
//
// Everything here is PLAYBACK SIDE (PRINCIPLES section 2): these are properties
// of the head at the moment of reproduction, not of the tape. Nothing in this
// file may be baked into the medium, and cleaning or aligning a machine is
// expected to change what these return without touching a stored sample.
//
// Every loss is a function of WAVELENGTH, not of frequency
// (docs/references/LOSS-EFFECTS.md section 0). lambda = v/f, so tape speed moves
// all of them at once, which is why speed is the control it is
// (PRINCIPLES section 4).
//
// Provenance per function is cited as SOURCES section N; see
// docs/references/SOURCES.md. Derivations are restated first-party in
// docs/references/LOSS-EFFECTS.md.
//
// JUCE-free by design. Promotion target: chalkwalk-tape.

#include <chalkwalk/tape/HeadLengthLoss.h>

#include <cmath>

namespace chalkwalk::tape
{
    // Wave number for a frequency at a tape speed. SOURCES section 2.
    //
    // Guards a stopped transport: at v = 0 nothing passes the head at all, and
    // the caller wants "no output", not an infinity propagated through four
    // multiplications.
    [[nodiscard]] inline double waveNumber(double frequencyHz, double speedMps) noexcept
    {
        if (speedMps <= 0.0)
            return 0.0;
        return 2.0 * M_PI * frequencyHz / speedMps;
    }

    // Spacing loss: exp(-k*a). SOURCES section 2.
    //
    // The dominant loss in a worn or dirty machine, and the reason the age model
    // weights spacing heavily. In dB this is -54.6 * a/lambda, so at a spacing of
    // one wavelength the output is down 54.6 dB.
    //
    // Assumes the tape's relative permeability is unity, which is an
    // approximation -- see LOSS-EFFECTS.md section 1. Any accuracy claim made
    // against this function has to say so.
    [[nodiscard]] inline double spacingLoss(double waveNum, double spacingMetres) noexcept
    {
        if (waveNum <= 0.0 || spacingMetres <= 0.0)
            return 1.0;
        return std::exp(-waveNum * spacingMetres);
    }

    // Thickness loss: (1 - exp(-k*d)) / (k*d). SOURCES section 2.
    //
    // Spacing loss averaged over the depth of the coating, so it is monotonic --
    // no nulls -- unlike gap loss. Tends to 1 at long wavelengths and to
    // 1/(k*d) at short ones, which is 6 dB/octave.
    [[nodiscard]] inline double thicknessLoss(double waveNum, double thicknessMetres) noexcept
    {
        const double kd = waveNum * thicknessMetres;
        if (kd <= 0.0)
            return 1.0;
        // Series expansion near zero: the closed form is 0/0 there, and in
        // floating point it is catastrophic cancellation for a while before it
        // is actually zero.
        if (kd < 1.0e-6)
            return 1.0 - 0.5 * kd;
        return (1.0 - std::exp(-kd)) / kd;
    }

    // ------------------------------------------------------------------
    // Gap loss. SOURCES section 3, LOSS-EFFECTS.md section 3.
    //
    // THREE functions, not one, and this is where we deliberately differ from
    // the usual implementation.
    //
    // The familiar sin(x)/x is derived for a head geometry that cannot exist --
    // an "infinite gap" the tape would have to cross an infinitely small slit
    // to reach. Solving the head potential problem properly for three
    // geometries gives three different functions, and the one that resembles
    // practical heads is neither the familiar one nor the other extreme.
    //
    // The argument throughout is x = pi * l / lambda = k * l / 2, with l the
    // reproduce-head gap length.
    // ------------------------------------------------------------------

    enum class GapGeometry
    {
        // Two parallel planes; the tape would have to cross an infinitely small
        // slit. Theoretical only, and the source of the textbook formula.
        Infinite,
        // Two coplanar sheets. Maxima decay as 1/sqrt(x) rather than 1/x.
        Thin,
        // Pole pieces bounded in one direction. "Bears close resemblance to
        // practical heads", and our default.
        SemiInfinite
    };

    // Type a: G(x) = sin(x)/x. Nulls at x = n*pi, i.e. l = n*lambda.
    [[nodiscard]] inline double gapLossInfinite(double x) noexcept
    {
        if (std::abs(x) < 1.0e-9)
            return 1.0;
        return std::sin(x) / x;
    }

    // Type b: J0(x), Bessel of the first kind, order zero.
    //
    // Same oscillating character as G, but the maxima decay as 1/sqrt(x)
    // instead of 1/x -- asymptotically sin(x + pi/4) / sqrt(pi*x/2) -- so a
    // thin-gap head keeps more output past its first null than the textbook
    // formula predicts.
    [[nodiscard]] inline double gapLossThin(double x) noexcept
    {
        return std::cyl_bessel_j(0.0, std::abs(x));
    }

    // Type c: S(x), the practical head. Intermediate between a and b.
    //
    // The exact function comes from a Schwarz-Christoffel solution of the
    // potential problem and has no elementary closed form. THIS IS AN
    // INTERPOLANT, not that solution, and it is built to match the three
    // properties we can state and test:
    //
    //   1. S(0) = 1.
    //   2. Zeros at x = 0.9 * n * pi -- so the FIRST NULL FALLS AT
    //      l = 0.9 * lambda, not at l = lambda. This is the measurable
    //      difference from the textbook formula: a gap length inferred from the
    //      first null on the l = lambda assumption is about 10% too large, and
    //      the literature records that discrepancy being confirmed from the
    //      other side, as the magnetic gap measuring 10% longer than the
    //      mechanical one.
    //   3. An envelope decaying as x^(-3/4), between type a's x^(-1) and type
    //      b's x^(-1/2).
    //
    // Replacing this with the real solution is a roadmap item. It is called out
    // as an interpolant rather than quietly presented as the exact function
    // because PRINCIPLES section 7 does not allow the second.
    [[nodiscard]] inline double gapLossSemiInfinite(double x) noexcept
    {
        constexpr double kNullScale = 0.9;  // first null at l = 0.9 lambda
        const double u = std::abs(x) / kNullScale;
        if (u < 1.0e-9)
            return 1.0;
        // sin(u)/u gives the zeros and unity at DC; (1+u^2)^(1/8) lifts the
        // envelope from u^-1 to u^-3/4 without disturbing either.
        return (std::sin(u) / u) * std::pow(1.0 + u * u, 0.125);
    }

    [[nodiscard]] inline double gapLoss(double x, GapGeometry geometry) noexcept
    {
        switch (geometry)
        {
            case GapGeometry::Infinite:     return gapLossInfinite(x);
            case GapGeometry::Thin:         return gapLossThin(x);
            case GapGeometry::SemiInfinite: return gapLossSemiInfinite(x);
        }
        return gapLossSemiInfinite(x);
    }

    // Azimuth loss: sin(y)/y with y = pi * W * tan(theta) / lambda.
    // SOURCES section 4. Not modelled in the DAFx paper; this one is ours.
    //
    // Note that this is the SAME integral as gap loss over a different window:
    // gap loss averages over the gap length along the track, azimuth loss over
    // the along-track displacement W*tan(theta) caused by the slant. Hence the
    // shared implementation.
    //
    // Savage on wide tracks, because the window scales with W -- and it is the
    // mechanism behind inter-track bleed (DESIGN.md section 4.5).
    [[nodiscard]] inline double azimuthLoss(double waveNum,
                                            double trackWidthMetres,
                                            double angleRadians) noexcept
    {
        if (waveNum <= 0.0 || trackWidthMetres <= 0.0 || angleRadians == 0.0)
            return 1.0;
        const double y = 0.5 * waveNum * trackWidthMetres * std::tan(std::abs(angleRadians));
        return gapLossInfinite(y);
    }

    // Head geometry and alignment, as one head presents it.
    //
    // Machine-wide quantities -- tape speed above all -- are deliberately NOT
    // here: they belong to the transport and are shared by every head on the
    // machine (PRINCIPLES section 3).
    struct HeadGeometry
    {
        double gapLengthMetres  = 6.0e-6;   // reproduce gap


        // THE RECORD SIDE OF A HEAD (SOURCES section 1). The reproduce losses
        // above do not use these; the record chain does, and they belong to the
        // head rather than to the chain because a machine has more than one.
        //
        // `H = N E I / g` -- turns, efficiency, current, gap. DAFx-19 section
        // 4.2.1 gives both as ORDERS OF MAGNITUDE rather than measurements:
        // "the number of turns of wire is typically on the order of 100" and
        // "the head efficiency is typically on the order of 0.1". They are
        // carried at those values and marked, because a product of two
        // order-of-magnitude figures is not a calibration -- what calibrates the
        // record path is the drive, in A/m, against coercivity (SOURCES 7).
        // NO MARKER, AND THAT IS THE POINT. The product of these two is
        // cancelled by the drive calibration -- what sets the record field is
        // amperes per metre against coercivity (SOURCES 7) -- so neither
        // reaches the output and neither is owed a document. They were marked
        // UNSOURCED, which put two numbers that cannot matter on a list of
        // numbers that do.
        int turns = 100;              // order of 100, and cancelled downstream
        double efficiency = 0.1;      // order of 0.1, and cancelled downstream
        // Tape to head. A WELL-MAINTAINED machine, not the worst case, and now
        // SOURCED rather than merely reduced (SOURCES section 25).
        //
        // The floor is set by asperities: the closest tape can ride to a head is
        // the average of the surface roughness of both. Perry gives the working
        // figure -- "an asperity of 10 uin, for example, in a typical 100-uin
        // wavelength video recording can result in a loss of signal of 5.5 dB"
        // -- and 54.6 x 10/100 is 5.46 dB, so the example checks against
        // Wallace exactly.
        //
        // Ten microinches is **0.254 um**. This was 1.0 um, which is four times
        // that, and before that 20 um from the source paper's worst case. Each
        // correction moved the top end by a lot: at a 10 um wavelength the
        // spacing loss goes 5.46 dB -> 1.36 dB on this change alone.
        //
        // 20 um is where AGE takes this (DESIGN.md section 7.2), not where it
        // starts.
        double spacingMetres    = 0.254e-6;

        // Depth of the MAGNETIC COATING, which is not the thickness of the
        // tape.
        //
        // The paper's 35 microns is total tape thickness -- base film plus
        // coating -- and using it here was wrong by roughly a factor of three.
        // The base is PET and holds no magnetisation, so it contributes nothing
        // to thickness loss; only the oxide layer does. Getting this wrong put
        // about 10 dB of extra loss at 10 kHz into every machine.
        //
        // THIS DEFAULT IS NOW A LAST RESORT: every machine sets its own from its
        // stock (SOURCES section 25), because a cassette's coating is half a
        // mastering tape's and the difference is most of why one is duller.
        // Kept at the middle of the published 4.32-16.5 um range so a machine
        // that forgets to set it is merely generic rather than wrong.
        double thicknessMetres  = 10.0e-6;

        double trackWidthMetres = 1.0e-3;
        // Unrecorded tape between one track and the next. Track PITCH is
        // trackWidthMetres + guardBandMetres, and it is the guard band rather
        // than the pitch that appears in the side-fringing exponent, because
        // the fringing field is measured from the edge of the head.
        double guardBandMetres  = 1.0e-3;
        // Magnetic shielding between the track elements of a head stack, in dB
        // of extra rejection on the side-fringing path.
        //
        // A TERM THE DERIVATION DOES NOT CONTAIN, ADDED BECAUSE TWO
        // SPECIFICATIONS SAY IT MUST. `SOURCES §12`'s reciprocity argument
        // describes the fringe field of an UNSHIELDED gap, and a real
        // multitrack head has mu-metal between its track elements precisely to
        // intercept that flux.
        //
        // Measured against Otari's own MTR-90 III specification (`SOURCES §11`):
        // the unshielded kernel gives -34.0 dB at 200 Hz for that machine's
        // geometry, against a published minimum of 43 dB. So the model is at
        // least 9 dB too pessimistic there -- and van Herk's correction moves it
        // further the wrong way, since the two-dimensional form under-predicts
        // crosstalk.
        //
        // ZERO BY DEFAULT, which is the unshielded gap the derivation actually
        // describes. A machine that wants to be believable sets it; a machine
        // that leaves it gets the physics we can derive rather than a number we
        // fitted. This is a quality-tier dial -- shielding is exactly what a
        // studio deck spends money on and a portastudio does not -- so it lives
        // on the head rather than in the closed form.
        double shieldingDb      = 0.0;
        double azimuthRadians   = 0.0;      // misalignment, not a user control

        // The two dimensions the CONTOUR EFFECT is a function of (SOURCES
        // section 17). Both are properties of the head's outside, not of its
        // gap, which is why nothing else in this struct resembles them.
        //
        //   faceLength -- the head face in contact with the tape, Fritzsch's L
        //   shieldGap  -- core to shielding can, his l_s
        //
        // Defaults are his measured heads: L = 14.6 mm and l_s = 4.2 mm, the
        // ones equation (18) was verified against. They are a real pair of
        // numbers from a real head rather than a plausible guess, and they are
        // NOT per-machine yet -- McKnight says the undulation is
        // "characteristic of each head and shield design", so eventually each
        // machine wants its own.
        double faceLengthMetres = 14.6e-3;
        double shieldGapMetres  = 4.2e-3;

        // How much of the head's length is ROUNDED -- Duinker and Geurst's
        // P = pi*R/L, the parameter their Table II is tabulated against
        // (SOURCES section 17). It is the axis WEAR MOVES: a head worn flat has
        // a smaller curved fraction and a louder head bump.
        //
        // pi/8 is a moderately rounded head, a quarter of the way along their
        // tabulated range from sharp to semicircular. Chosen, not measured -- a
        // real head is contoured but nothing like semicircular -- and it is one
        // of the two numbers here that wants a machine of its own.
        double contourRounding  = M_PI / 8.0;

        // What the SHIELD does to the undulation, as a multiplier on its
        // departure from unity.
        //
        // 1.0 is Duinker and Geurst's answer exactly: a bare plate head, which
        // overshoots by 20 per cent. A real shielded head is worse, and by a
        // lot -- Fritzsch measures ±6 dB where the bare plate gives ±1.2, which
        // is a gain of about 4.6. McKnight says why it must be per machine: the
        // undulation is "characteristic of each head AND SHIELD design".
        //
        // Defaulting to 1.0 means a machine that has not chosen gets the
        // published exact solution rather than an amplification nobody picked --
        // the same discipline `inductiveCoupling` and `shieldingDb` follow.
        double contourShieldGain = 1.0;

        GapGeometry gapGeometry = GapGeometry::SemiInfinite;
    };

    // WHERE THE FRINGING KERNEL STOPS BEING TRUSTWORTHY.
    //
    // McKnight measured a professional 16-track head on 50 mm tape -- Capstan's
    // format -- against the calculated fringing of the kind this file
    // implements, and found the theory good "above the undulations in the
    // frequency response (say above 125 Hz at 380 mm/s)" and wrong below, by
    // -3.5 dB at 40-50 Hz and +1.5 dB at 16 Hz (`SOURCES §10`). Below the
    // undulations the fringing correction itself undulates, and "the fringing
    // sometimes actually causes a decrease in the measured flux, instead of the
    // expected increase" -- so it is not merely inaccurate, it changes sign.
    //
    // The cause is named and it is the SAME finite pole length that produces the
    // contour effect: "all authors have assumed that the head length is
    // infinite, but in practice the longest..." So the contour effect and the
    // error in the fringing calculation are one phenomenon seen twice, which is
    // why this bound lives next to the kernel rather than in a note somewhere.
    //
    // Expressed as a WAVELENGTH, because that is what it is a function of: at
    // 380 mm/s, 125 Hz is 3.05 mm. Every user of the kernel below this is
    // extrapolating past what anybody has measured.
    inline constexpr double kFringingValidWavelengthMetres = 3.05e-3;

    // The frequency that wavelength lands at, for a given tape speed. Doubling
    // the speed doubles it, like everything else here.
    [[nodiscard]] inline double fringingValidAboveHz(double speedMps) noexcept
    {
        if (speedMps <= 0.0)
            return 0.0;
        return speedMps / kFringingValidWavelengthMetres;
    }

    // Side-fringing coupling from a neighbour `separation` tracks away
    // (`SOURCES §10`, derived in `DESIGN.md` §4.5).
    //
    //   X = (lambda / (2 pi W)) * exp(-2 pi g / lambda)
    //                           * (1 - exp(-2 pi Wt / lambda))
    //
    // PLAYBACK SIDE, and it stores nothing (`PRINCIPLES §2`).
    //
    // AN APPROXIMATION, NAMED AS ONE. This uses the two-dimensional distance
    // loss of `SOURCES §2` in the across-track direction. van Herk measured
    // that exact substitution exaggerating the loss by up to 30% (2.4 dB), so
    // this UNDER-predicts crosstalk by a known sign and a bounded amount. His
    // corrections are in normalised graphs we do not have. This has the same
    // standing as `gapLossSemiInfinite` and must not be presented as his
    // result.
    //
    // THE 1/W IS THE NORMALISATION AND IT IS LOAD-BEARING. Without it this
    // describes only how far the field reaches, not how much of it the head
    // hears relative to its own track -- and it is the term that makes narrow
    // tracks worse.
    //
    // Note the two limits, both asserted in the tests: as the wavelength grows
    // this tends to Wt/W and NOT to infinity, because at DC a head cannot tell
    // the tracks apart; as it shrinks it dies exponentially.
    [[nodiscard]] inline double sideFringingCoupling(double frequencyHz,
                                                     double speedMps,
                                                     const HeadGeometry& head,
                                                     int separation = 1) noexcept
    {
        if (separation < 1 || speedMps <= 0.0 || head.trackWidthMetres <= 0.0)
            return 0.0;

        const double pitch = head.trackWidthMetres + head.guardBandMetres;
        // Distance from this head's edge to the near edge of the neighbour.
        const double gap = head.guardBandMetres
                         + static_cast<double>(separation - 1) * pitch;

        // DC is the limit, not a special case: the expression below is 0/0
        // there, and Wt/W is what it tends to.
        if (frequencyHz <= 0.0)
            return 1.0;

        const double lambda = speedMps / frequencyHz;
        const double reach = std::exp(-2.0 * M_PI * gap / lambda);
        const double extent = 1.0 - std::exp(-2.0 * M_PI * head.trackWidthMetres / lambda);
        const double unshielded =
            (lambda / (2.0 * M_PI * head.trackWidthMetres)) * reach * extent;

        // The shield is frequency-independent here, and that is an assumption
        // rather than a result: a mu-metal shield's effectiveness does vary with
        // frequency, and nothing available says how for this geometry. Flat is
        // the answer that adds no shape we cannot defend.
        if (head.shieldingDb <= 0.0)
            return unshielded;
        return unshielded * std::pow(10.0, -head.shieldingDb / 20.0);
    }

    // Inductive coupling between the heads themselves (`SOURCES §11`).
    //
    // NOT THROUGH THE TAPE, AND THAT IS THE WHOLE DIFFERENCE. Side fringing is
    // a head reading the MEDIUM past its own track, so it is a function of
    // recorded wavelength and therefore moves with tape speed. This is
    // winding-to-winding mutual inductance across the head stack: it never
    // touches the tape, so it is a function of FREQUENCY alone and does not
    // move with speed at all.
    //
    // ** THIS IS THE ONE RESPONSE IN THE PROJECT THAT IS NOT A FUNCTION OF
    // WAVELENGTH. ** The standing invariant -- halving the speed and halving
    // the frequency give an identical response -- is a theorem about magnetic
    // recording, and this is not a magnetic-recording effect. A correct
    // implementation FAILS a wavelength-scaling test, so that test must not be
    // pointed at this path (`DESIGN.md` §4.5).
    //
    // `level` IS NOT IN THE PATENT, BUT TWO SPECIFICATIONS AGREE ON IT.
    // Warren gives the mechanism and the sense of the slope and states no dB
    // figures. Otari's MTR-90 III and Tascam's 424 MkII both publish crosstalk
    // as 55 dB at 1 kHz -- a 2-inch studio multitrack and a cassette
    // portastudio, quoting the same number. Side fringing is negligible at
    // 1 kHz on both (-130 dB and -439 dB respectively), so both figures are
    // this term almost alone, and it lands at 1.78e-3.
    //
    // That the two agree is worth reading carefully. It may mean the inductive
    // path really is similar across head stacks, which is plausible -- it is a
    // property of windings and wiring rather than of track geometry. It may
    // equally mean 55 dB is a conventional figure manufacturers quote rather
    // than each machine's measured limit. Either way it is NOT a quality-tier
    // dial: the cheap machine does not do worse here, which is the opposite of
    // what the tiers would have guessed.
    //
    // STILL DEFAULTS TO ZERO, because the slope's exponent is unsettled: a
    // 6 dB/octave rise from 55 dB at 1 kHz reaches -31 dB at 16 kHz where Otari
    // allow only -43, so the true rise is nearer 3 dB/octave and the patent
    // does not say. Zero means "not yet chosen", so a machine built today has
    // the low-frequency arm of the V and not the high-frequency one -- which is
    // honest, where a plausible-looking invented constant would not be.
    //
    // The distance falloff is `1/separation^3`, the magnetic dipole law. The
    // patent establishes that distance matters ("for a given fixed distance
    // between adjacent heads") and gives no law, so THIS EXPONENT IS CHOSEN,
    // not derived.
    [[nodiscard]] inline double inductiveCoupling(double frequencyHz,
                                                  double level,
                                                  int separation = 1,
                                                  double referenceHz = 1000.0) noexcept
    {
        if (level <= 0.0 || separation < 1 || frequencyHz <= 0.0 || referenceHz <= 0.0)
            return 0.0;

        const double s = static_cast<double>(separation);
        // Rising with frequency: an induced voltage goes as dI/dt.
        return level * (frequencyHz / referenceHz) / (s * s * s);
    }

    // The contour effect -- "head bump". SOURCES section 17.
    //
    //     psi(k) = 1 + shieldGain * (H(P, L/lambda) - 1)
    //
    // THE SHAPE COMES FROM THE EXACT SOLUTION AND THE SIZE FROM THE SHIELD, and
    // that split is the whole design. Duinker and Geurst solved the head-length
    // problem properly and tabulated the answer; what they solved is a bare
    // plate head, and a real head has a shielding can that makes the same ripple
    // several times larger. So the table supplies where the peaks and nulls are
    // and how fast they die away, and one per-machine number supplies how far
    // they swing.
    //
    // It is the whole of the long-wavelength response the rest of this file does
    // not have: spacing, thickness, gap and azimuth all tend to unity as the
    // wavelength grows, and a real head does not.
    //
    // WHY THIS AND NOT FRITZSCH'S CLOSED FORM, which is also in this file below.
    // His is an approximation verified against one measured head; theirs is the
    // exact solution of the same problem. The two agree on the period -- both
    // repeat every 2 in L/lambda -- and differ by about a quarter of that in
    // phase, which is the difference between their geometries. Taking the exact
    // shape and scaling it is the honest combination, and it brings the rounding
    // parameter with it, which is what age moves.
    //
    // WHAT THE COMBINATION DOES NOT CAPTURE: the shield has its own envelope,
    // and Fritzsch's sinc keeps his ripple alive longer than the bare plate's.
    // At L/lambda = 4 he is still swinging ±1.5 dB where the table has decayed
    // to ±0.5. A gain cannot produce that, so the far tail is the bare head's.
    // ROADMAP.md carries it.
    //
    // ** IT IS NOT A LOSS. ** It peaks above unity -- that is what a head bump
    // is -- so any code assuming the reproduce chain only attenuates is wrong
    // once this is in the product.
    [[nodiscard]] inline double contourResponse(double waveNum,
                                                const HeadGeometry& head) noexcept
    {
        if (waveNum <= 0.0 || head.faceLengthMetres <= 0.0)
            return 1.0;

        const double h = headLengthLossFactor(waveNum, head.faceLengthMetres,
                                              head.contourRounding);
        return 1.0 + head.contourShieldGain * (h - 1.0);
    }

    // Fritzsch's closed form, kept as the cross-check rather than as the model.
    //
    //     psi(k) = 1 - r2 * sinc(k * l_s / 1.762) * cos(k * L / 2)
    //
    // His equations (16) and (18) combined, verified against his own measured
    // shielded head, and the thing that established the effect exists at the
    // size it does. `r2` is his reluctance ratio, which he says "usually takes
    // values between 1.02 and 1.20".
    //
    // Two known limits it gets wrong and the table does not: it runs away to
    // `1 - r2` at long wavelengths, a negative number, where the exact answer is
    // 0.5 for a head of infinite height and 0 for a real one. Valid above
    // `contourCrossoverHz` and not below.
    [[nodiscard]] inline double fritzschContourResponse(double waveNum,
                                                        const HeadGeometry& head,
                                                        double reluctanceRatio) noexcept
    {
        if (waveNum <= 0.0
            || head.faceLengthMetres <= 0.0
            || head.shieldGapMetres <= 0.0)
            return 1.0;

        // The 1.762 is his, from the two-dimensional field solution at the
        // shield gap. It is not fitted and must not be tuned.
        const double x = waveNum * head.shieldGapMetres / 1.762;
        const double envelope = (std::abs(x) < 1.0e-9) ? 1.0 : std::sin(x) / x;
        return 1.0 - reluctanceRatio * envelope
                   * std::cos(0.5 * waveNum * head.faceLengthMetres);
    }

    // The lowest frequency at which the undulation term vanishes and the
    // response passes through unity: `L / lambda = 1/2`, so `f = v / (2 L)`.
    //
    // NOT a null of the response -- the response is 1 there, which is the
    // opposite. It is the reference point of the ripple, and it is reported
    // because it is the one figure of the effect that is a prediction rather
    // than an artifact, and because it is what moves when the speed does --
    // which is the whole of why 30 ips sounds different at the bottom.
    [[nodiscard]] inline double contourCrossoverHz(double speedMps,
                                                   const HeadGeometry& head) noexcept
    {
        if (speedMps <= 0.0 || head.faceLengthMetres <= 0.0)
            return 0.0;
        return speedMps / (2.0 * head.faceLengthMetres);
    }

    // The total reproduce response at one frequency: the product of the four.
    //
    // Realised as an FIR by inverse transform in the audio path; this function
    // is the specification that design is checked against, and is what the
    // tests assert on.
    //
    // ** THE CONTOUR EFFECT IS DELIBERATELY NOT IN HERE **, and the reason is
    // the one CrosstalkFilter already learned. Its structure is at the wrong
    // scale for this chain: with a 14.6 mm face at 15 ips its ripple has a
    // period of 26 Hz, and a 512-tap FIR at 48 kHz resolves 94 Hz, so
    // PlaybackFilter
    // cannot represent it -- measured, the design misses the specification by
    // 0.04 at 1 kHz and rings by 167 dB across the passband.
    //
    // Three other things broke when it was tried here, each worth knowing:
    // the response goes NEGATIVE past a null, so every dB conversion in the
    // suite returned NaN; a worn head stopped being measurably worse than a
    // clean one at 10 kHz, because the ripple swamped the spacing difference;
    // and DESIGN.md section 6.0's lambda_cut search returned 49.95 um instead
    // of 6.78, because the FIRST crossing of the noise floor became a contour
    // null rather than the gap null -- which would have set the medium's
    // resolution from the wrong feature entirely.
    //
    // So it wants its own filter at its own rate, cascaded with this one, the
    // way the crosstalk arms do. ROADMAP.md carries it.
    // ---------------------------------------------------------------------
    // THE RECORDING ZONE (SOURCES section 29, Westmijze part II and part V.4).
    //
    // A RECORD-SIDE WAVELENGTH LOSS, and the engine had none. Westmijze
    // enumerates four wavelength-dependent losses and this project modelled the
    // two that belong to reproduction -- thickness with spacing, and the
    // reproduce gap. The first of the two it lacked is this one:
    //
    //   "During the time the tape passes the finite length in front of the
    //    recording gap where the magnetization process takes place, the phase
    //    of the high signal-frequency to be recorded changes, thus effecting a
    //    weakening of the recorded magnetization."
    //
    // A phase that turns while an element of tape crosses a zone of finite
    // length is an AVERAGE OVER THAT LENGTH, so the loss is the same sin(x)/x
    // the reproduce gap already produces -- with the recording zone's length in
    // place of the gap's. That is why this needed no new mathematics, only a
    // length.
    //
    // AND IT IS PERMANENT (`PRINCIPLES §2`). It acts on the magnetisation
    // before it is stored, so it is baked into the take -- unlike every loss
    // above it, which is applied on the way out and stores nothing.
    //
    // THE LENGTH IS CALIBRATED, NOT GUESSED, which is the whole reason this was
    // worth waiting for a source. Westmijze reports Muckenhirn's measurement:
    // a 20 um record gap loses 19 dB at a 25 um wavelength. Solving
    // `sinc(pi L / lambda) = 10^(-19/20)` gives L = 22.4 um, so
    //
    //     the recording zone is 1.12 times the record gap
    //
    // which is physically what one would hope for -- the zone reaches a little
    // past the gap because the field fringes -- and is a measurement rather
    // than a taste. ONE POINT CALIBRATES ONE PARAMETER AND NOT A SHAPE: the
    // sin(x)/x form comes from Westmijze's description of the mechanism, and a
    // single measured value cannot confirm it.
    inline constexpr double kRecordingZonePerGap = 1.122;

    [[nodiscard]] inline double recordingZoneMetres(const HeadGeometry& recordHead) noexcept
    {
        return kRecordingZonePerGap * recordHead.gapLengthMetres;
    }

    [[nodiscard]] inline double recordingZoneLoss(double waveNum,
                                                  double zoneMetres) noexcept
    {
        if (waveNum <= 0.0 || zoneMetres <= 0.0)
            return 1.0;
        const double x = 0.5 * waveNum * zoneMetres;
        return (std::abs(x) < 1.0e-9) ? 1.0 : std::sin(x) / x;
    }

    // What the recording process costs at a frequency, on a machine.
    [[nodiscard]] inline double recordingResponse(double frequencyHz, double speedMps,
                                                  const HeadGeometry& recordHead) noexcept
    {
        return recordingZoneLoss(waveNumber(frequencyHz, speedMps),
                                 recordingZoneMetres(recordHead));
    }

    [[nodiscard]] inline double playbackResponse(double frequencyHz,
                                                 double speedMps,
                                                 const HeadGeometry& head) noexcept
    {
        const double k = waveNumber(frequencyHz, speedMps);
        if (k <= 0.0)
            return 1.0;
        return spacingLoss(k, head.spacingMetres)
             * thicknessLoss(k, head.thicknessMetres)
             * gapLoss(0.5 * k * head.gapLengthMetres, head.gapGeometry)
             * azimuthLoss(k, head.trackWidthMetres, head.azimuthRadians);
    }

    // THE SHORTEST WAVELENGTH A HEAD STILL DELIVERS, which is what sets how
    // finely the medium must be sampled (DESIGN.md section 6.0).
    //
    // It has NO SPEED IN IT. Every loss here is a function of wavelength, so the
    // point at which a head gives up is a property of its geometry alone --
    // which is why `R = 2 / lambda_cut` is a machine constant and `R v` falls
    // out of it rather than the other way round.
    //
    // THE FIRST CROSSING, AND THAT IS THE WHOLE DIFFICULTY. Gap loss is a sinc,
    // so past its first null the response CLIMBS BACK -- measured, to within
    // twenty-five decibels of the passband. A search for the deepest point, or
    // for the last crossing, walks through the null into a sidelobe and returns
    // a much shorter wavelength: not because more resolution is needed, but
    // because the search found a bump. It produces a plausible number, which is
    // what makes it dangerous, and `DecimatorTests` pins both halves.
    //
    // The magnitude matters too: past the null the response is NEGATIVE, so a
    // threshold test without `abs` reports everything beyond it as below the
    // floor.
    [[nodiscard]] inline double lambdaCutMetres(const HeadGeometry& head,
                                                double thresholdDb = -60.0) noexcept
    {
        const double threshold = std::pow(10.0, thresholdDb / 20.0);

        // Geometric walk DOWNWARD, so the first crossing found is the longest
        // wavelength that fails -- and the step is a ratio rather than a
        // difference so the resolution is uniform in the octaves that matter.
        // Speed cancels out of `playbackResponse`, so any value serves.
        constexpr double kSpeed = 1.0;
        for (double lambda = 500.0e-6; lambda > 0.02e-6; lambda *= 0.9995)
            if (std::abs(playbackResponse(kSpeed / lambda, kSpeed, head)) < threshold)
                return lambda;
        return 0.02e-6;
    }

}
