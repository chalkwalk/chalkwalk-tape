#pragma once

// Record and reproduce equalisation (SOURCES section 16, DESIGN.md section 4.11).
//
// WHY A TAPE MACHINE HAS TWO EQUALISERS AND A RECORD PLAYER HAS TWO EQUALISERS,
// for the same reason: the medium cannot carry a flat signal well, so the
// signal is pre-emphasised going on and de-emphasised coming off, and what the
// standard fixes is the REPRODUCE half. Fixing the reproduce end is what makes
// a tape recorded on one machine play correctly on another; the record end is
// then whatever that machine needs to come out flat, which is what alignment
// adjusts.
//
// THE CONSEQUENCE IS NOT FLATNESS. If both halves were merely inverse the pair
// would cancel and none of this would be audible. What it changes is everything
// that happens BETWEEN them:
//
//   the tape sees a pre-emphasised signal, so the top of the band arrives at
//   the coating ten to twenty decibels hot and SATURATES FIRST -- which is most
//   of what people mean by the sound of tape, and which this engine did not
//   produce at all before
//
//   the head losses stop being a permanent dullness and become the thing
//   ALIGNMENT CANCELS AND AGE UNCOVERS. Align once, at one speed on a fresh
//   head, and then wear, varispeed, head wear and azimuth all show, because the
//   record equaliser is a fixed network and they are not
//
//   NAB puts 5.8 dB more low-frequency flux on the tape than IEC at 32 Hz
//   (SOURCES section 16), so it saturates low sooner and prints through more
//
// WE MODEL NO INTEGRATOR, AND DO NOT NEED ONE. A real reproduce head is a
// differentiator -- its output follows dPhi/dt, so it rises 6 dB an octave --
// and a real reproduce equaliser undoes that. This engine stores magnetisation
// and reads it directly, so that pair has already cancelled before we start.
// What is left is the part MRL tabulates: the response against a CONSTANT-FLUX
// tape, which is what `reproduceResponse` returns.
//
// JUCE-free by design. Promotion target: chalkwalk-tape.

#include <chalkwalk/tape/LossEffects.h>

#include <algorithm>
#include <cmath>

namespace chalkwalk::tape
{
    // The two families. There is no NAB curve at 30 ips -- AES and IEC agree
    // there -- so a machine asking for one at that speed gets the IEC answer
    // rather than an invented curve (SOURCES section 16).
    enum class EqStandard
    {
        Nab,
        Iec,
        // ---- AND THE CASSETTE, WHICH IS A THIRD FAMILY AND NOT A ROW ----
        //
        // It used to be inferred: pick the nearest row by speed, and if that
        // came out 1 7/8 ips substitute the tape type's constant. That works
        // for a machine with one speed and is WRONG the moment one has two.
        //
        // A double-speed cassette multitracker runs at 9.5 cm/s, which is
        // nearer the reel table's 3.75 ips row than the cassette's -- so it
        // silently equalised as a REEL machine, 3180 + 90, when both documents
        // we hold say a machine of that class uses **3180 + 35** there
        // (`SOURCES §44`, `§42`). `§44` flags it in those words: the 3.75 ips
        // row is "correctly sourced for a reel deck" and "must not be applied"
        // to a portastudio.
        //
        // So the format is declared by the machine rather than guessed from its
        // speed. A cassette deck equalises as a cassette at every speed it has,
        // which is what a cassette deck is.
        IecCassette
    };

    // Two time constants, in seconds. Zero means the term is absent, which is
    // how IEC and AES express "no low-frequency time constant" -- and that is a
    // real difference rather than a very long one.
    struct EqCurve
    {
        double lowSeconds = 0.0;
        double highSeconds = 0.0;

        [[nodiscard]] constexpr bool operator==(const EqCurve& o) const noexcept
        {
            return lowSeconds == o.lowSeconds && highSeconds == o.highSeconds;
        }

        // THE HIGH-FREQUENCY TERM ALONE, which is what the FIR filters in this
        // engine can actually realise -- and saying so here is better than
        // dropping it silently at two call sites.
        //
        // A 3180 microsecond time constant is a 50 Hz corner, and an FIR long
        // enough to shape 50 Hz is about 20 ms: a thousand taps at 48 kHz, per
        // track, in the record chain AND the playback chain. The honest fix is
        // a matched pair of one-pole sections, one on each side, and it is not
        // built.
        //
        // WHAT IS LOST BY LEAVING IT OUT, stated rather than waved at. The two
        // halves cancel at the output, so the response is unaffected -- but the
        // FLUX ON THE TAPE is not, and `SOURCES §16` names the consequence:
        // NAB carries 5.8 dB more low-frequency flux at 32 Hz than IEC, so it
        // saturates low sooner and prints through more. Until the pair exists,
        // NAB and IEC differ here only in their high-frequency constants, and a
        // machine on NAB does not get its extra bass flux.
        [[nodiscard]] constexpr EqCurve highOnly() const noexcept
        {
            return EqCurve{ 0.0, highSeconds };
        }
    };

    // MRL's table, keyed by the speed the machine is actually running at
    // (SOURCES section 16). Speeds are matched to the nearest standard speed on
    // a LOGARITHMIC basis, because the table is in octaves of tape speed and a
    // machine at 19 cm/s is a 7.5 ips machine however the number is written.
    //
    // AND THE CASSETTE ROW ALONE DEPENDS ON THE TAPE. `cassetteHighSeconds`
    // defaults to Type I's 120 us, so every existing caller is unchanged; a
    // Type II or Type IV reel passes 70 us and gets the curve its formulation
    // is standardised to (`SOURCES §34`). Only the 1.875 ips row consults it,
    // because the reel-to-reel standards are written against SPEED and say
    // nothing about formulation.
    //
    // GETTING THIS WRONG IS 120/70 OF TREBLE -- 3.35 dB at 10 kHz on the
    // reproduce side alone, and the record equaliser is the other half -- and
    // it is exactly what a cassette deck's tape selector is for. It would have
    // arrived silently the moment a machine was given a second stock, which is
    // why the coupling exists before the second stock does.
    [[nodiscard]] inline EqCurve eqCurveFor(EqStandard standard, double speedMps,
                                            double cassetteHighSeconds = 120.0e-6) noexcept
    {
        struct Row { double ips; double nabLow, nabHigh, iecLow, iecHigh; };
        // ---- EVERY ROW IS SOURCED, from ONE document (`SOURCES §40`) ----
        //
        // IASA TC-04 5.4.10 tabulates replay equalisation at every speed in
        // both families, and every row below matches it: 76 cm/s IEC2/AES
        // infinity/17.5 with no NAB curve; 38 cm/s IEC1 infinity/35 and NAB
        // 3180/50; 19 cm/s IEC1 infinity/70 and NAB 3180/50; 9.5 cm/s
        // IEC2/NAB 3180/90; cassette Type I 3180/120 and Type II/IV 3180/70.
        //
        // That was found looking for the 3.75 ips row alone, which was the
        // only one marked UNSOURCED. The table it came from happened to
        // underwrite the other four as well.
        // ips        NAB low   NAB high    IEC low   IEC high
        static constexpr Row kTable[] = {
            // KEPT, and it is now only reachable by a machine that says it is
            // a REEL deck run absurdly slowly. A cassette machine takes the
            // `IecCassette` branch above and never sees this table.
            {  1.875,  3180e-6,   120e-6,   3180e-6,   120e-6 },  // cassette, Type I
            // SOURCED, and it used to be the one interpolated row here.
            // IASA TC-04 5.4.10 gives 9.5 cm/s as IEC2/NAB/RIAA 3180/90 us
            // (`SOURCES §40`), which is exactly what the interpolation had --
            // so the guess was right and is now a citation. Load-bearing:
            // Basinski's ReVox G36 ran at 3.75 ips (`SOURCES §32`) -- and a
            // Portastudio runs there too, which is exactly the trap: it is the
            // right row for the ReVox and the wrong one for the Portastudio,
            // and only the machine knows which it is. See `IecCassette`.
            //
            // IASA also records DIN variants at this speed (3180/120, and
            // infinity/200). We take IEC2/NAB because it is the family every
            // other row here is in.
            {  3.75,   3180e-6,    90e-6,   3180e-6,    90e-6 },  // SOURCES 40
            {  7.5,    3180e-6,    50e-6,        0.0,   70e-6 },
            { 15.0,    3180e-6,    50e-6,        0.0,   35e-6 },
            { 30.0,         0.0,  17.5e-6,       0.0, 17.5e-6 },  // no NAB curve exists
        };

        const double ips = std::abs(speedMps) / 0.0254;

        // ---- THE CASSETTE FAMILY: TWO SPEEDS, AND THE STANDARD'S OWN ----
        //
        // At 1 7/8 the high constant is the TAPE's -- 120 us for Type I and
        // 70 for Types II and IV -- which is why the standard reproduces two
        // curves at one speed and why a cassette deck needs a tape selector
        // (`SOURCES §34`).
        //
        // At 3 3/4 it is **35 us for everything**, and that last word is
        // `DECLARED`: the Tascam 246 and the 238 both publish "High Speed:
        // 3,180 us + 35 us" and NEITHER qualifies it by tape type. A machine
        // running the tape at twice the speed has twice the wavelength and
        // needs far less top-end pre-emphasis, so one row there is plausible as
        // well as published -- but it is one row because two documents show one
        // row, not because a standard says so.
        //
        // The threshold is the geometric mean of the two speeds, so it is
        // equidistant in the units the whole table is nearest-matched in.
        if (standard == EqStandard::IecCassette)
        {
            constexpr double kDoubleSpeedFrom = 2.6517;   // sqrt(1.875 * 3.75)
            if (ips >= kDoubleSpeedFrom)
                return EqCurve{ 3180e-6, 35e-6 };
            return EqCurve{ 3180e-6,
                            cassetteHighSeconds > 0.0 ? cassetteHighSeconds
                                                      : 120e-6 };
        }

        const Row* best = &kTable[0];
        double closest = 1.0e9;
        for (const auto& row : kTable)
        {
            const double distance = std::abs(std::log(std::max(1.0e-6, ips) / row.ips));
            if (distance < closest) { closest = distance; best = &row; }
        }
        EqCurve curve = (standard == EqStandard::Nab)
                      ? EqCurve{ best->nabLow, best->nabHigh }
                      : EqCurve{ best->iecLow, best->iecHigh };

        // The cassette row, and only it.
        if (best == &kTable[0] && cassetteHighSeconds > 0.0)
            curve.highSeconds = cassetteHighSeconds;
        return curve;
    }

    // THE REPRODUCE CHAIN'S RESPONSE TO A CONSTANT-FLUX TAPE, normalised at
    // 1 kHz -- which is the quantity MRL tabulates and the one this engine
    // needs, since the integrator has already cancelled (see the header note).
    //
    // A first-order high pass at `1 / (2 pi lowSeconds)` and a first-order
    // ZERO at `1 / (2 pi highSeconds)`. An absent time constant is an absent
    // term rather than a corner at zero.
    //
    // Verified against MRL's published conversion tables at every frequency and
    // speed they give, to 0.02 dB (`SOURCES §16`).
    [[nodiscard]] inline double reproduceResponse(double hz, const EqCurve& curve) noexcept
    {
        const auto shape = [&](double f)
        {
            double m = 1.0;
            if (curve.lowSeconds > 0.0)
            {
                const double corner = 1.0 / (2.0 * M_PI * curve.lowSeconds);
                m *= f / std::sqrt(f * f + corner * corner);
            }
            // A ZERO, NOT A POLE, and this is the thing to get right. The
            // reproduce characteristic against a CONSTANT-FLUX tape RISES at
            // high frequency -- MRL's own table gives +21.64 dB at 20 kHz for
            // a cassette curve -- because a tape recorded to the standard
            // carries LESS flux up there, not more. The short wavelengths are
            // where the medium cannot hold it.
            //
            // Modelled as a pole first, which is the intuitive reading of "a
            // 120 microsecond time constant", and it was wrong by 45 dB at
            // 20 kHz. With the zero, this reproduces the published table to
            // 0.02 dB at every frequency and every speed.
            if (curve.highSeconds > 0.0)
            {
                const double corner = 1.0 / (2.0 * M_PI * curve.highSeconds);
                m *= std::sqrt(1.0 + (f / corner) * (f / corner));
            }
            return m;
        };
        const double reference = shape(1000.0);
        if (!(reference > 0.0))
            return 1.0;
        return shape(std::max(1.0e-3, std::abs(hz))) / reference;
    }

    // ---- THE LOW-FREQUENCY TIME CONSTANT, AS THE PAIR IT HAS TO BE ----
    //
    // 3180 us is a 50 Hz corner and an FIR long enough to shape it is about
    // 20 ms -- a thousand taps at 48 kHz, per track, on BOTH sides. So it is a
    // one-pole section instead, which is two multiplies, and `EqCurve::highOnly`
    // exists to hand the FIR the rest.
    //
    // WHY IT IS A SHELF AND NOT A HIGH PASS. The reproduce term is
    // `f / sqrt(f^2 + fc^2)`, and the record side is its RECIPROCAL -- which is
    // an integrator at DC and does not exist as a filter. A real record
    // equaliser has a finite amount in hand, which `recordPreEmphasis` already
    // says (`maxBoostDb`, 20 dB), so the pair is bounded the same way: the boost
    // levels off below a second corner at `fc / 10^(maxBoostDb/20)`.
    //
    // AND THE TWO ARE EXACT RECIPROCALS, which is what makes the round trip flat
    // while the TAPE still carries the extra flux. That is the whole point of
    // building it: `SOURCES §16` has NAB carrying 5.8 dB more low-frequency flux
    // at 32 Hz than IEC, so it saturates low sooner and prints through more --
    // and until this existed, NAB and IEC differed only at the top and a machine
    // on NAB got none of its extra bass.
    //
    // At 32 Hz with a 3180 us constant this gives +5.26 dB of record boost
    // against the unbounded +5.37, so the bound costs a tenth of a decibel
    // where it matters and keeps the filter realisable where it does not.
    class LowConstant
    {
    public:
        // `lowSeconds` of zero is an ABSENT TERM, which is how IEC expresses
        // "no low-frequency time constant" -- not a corner at zero.
        void design(double lowSeconds, double sampleRateHz, bool record,
                    double maxBoostDb = 20.0) noexcept
        {
            reset();
            bypass_ = !(lowSeconds > 0.0 && sampleRateHz > 0.0);
            if (bypass_)
                return;

            const double fc = 1.0 / (2.0 * M_PI * lowSeconds);
            const double flo = fc * std::pow(10.0, -std::abs(maxBoostDb) / 20.0);

            // Reproduce is `(s + wlo) / (s + wc)`: unity well above the corner,
            // and `wlo / wc` below it. Record is the same section inverted.
            const double wc = 2.0 * M_PI * fc;
            const double wlo = 2.0 * M_PI * flo;
            const double zero = record ? wc : wlo;
            const double pole = record ? wlo : wc;

            // Bilinear. At 50 Hz against any audio rate the prewarp is a part
            // in ten thousand, so the plain transform is used and said so.
            const double k = 2.0 * sampleRateHz;
            const double den = k + pole;
            b0_ = (k + zero) / den;
            b1_ = (zero - k) / den;
            a1_ = (pole - k) / den;
        }

        void reset() noexcept { x1_ = y1_ = 0.0; }

        [[nodiscard]] double process(double x) noexcept
        {
            if (bypass_)
                return x;
            const double y = b0_ * x + b1_ * x1_ - a1_ * y1_;
            x1_ = x;
            y1_ = y;
            return y;
        }

        [[nodiscard]] bool bypassed() const noexcept { return bypass_; }

    private:
        double b0_ = 1.0, b1_ = 0.0, a1_ = 0.0;
        double x1_ = 0.0, y1_ = 0.0;
        bool bypass_ = true;
    };

    // WHAT THE RECORD AMPLIFIER HAS TO DO, which is the standard's half AND the
    // machine's own.
    //
    //   the STANDARD's half is `1 / reproduceResponse`, so that the flux on the
    //   tape follows the recorded characteristic and a tape made here plays
    //   correctly on any machine that follows it
    //   the MACHINE's half is `1 / playbackResponse`, which is the alignment
    //   trim: an engineer sets the record equaliser so that THIS head, at THIS
    //   speed, on THIS stock, comes out flat
    //
    // WHICH IS WHY IT IS DESIGNED ONCE AND THEN LEFT. A record equaliser is a
    // fixed network with a few trims, set at alignment for one speed and one
    // head. Everything that departs from that afterwards SHOWS -- wear raising
    // the spacing, varispeed moving every loss while the equaliser stays put,
    // the head wearing its gap wider, azimuth. The losses stop being a
    // permanent dullness and become the thing alignment cancels and age
    // uncovers, which is the entire reason for modelling this.
    //
    // BOUNDED, because the inverse of a loss that goes to zero does not. Twenty
    // decibels is about what a real record equaliser has in hand at the top of
    // the band, and past the limit the machine simply stops compensating --
    // which is what a real one does too, and is why every deck has a top end.
    // `head` is the REPRODUCE head, because what alignment compensates is what
    // comes back; `recordHead` supplies the recording zone, which is the
    // record-side loss the equaliser also has to overcome (`SOURCES §29`).
    //
    // The reproduce head was being used for BOTH, which compensated the SYNC
    // path's response rather than the repro path's -- an error the round trip
    // largely hid, because the standard's curve dominates and the two heads
    // differ only in their gaps.
    [[nodiscard]] inline double recordPreEmphasis(double hz, double speedMps,
                                                  const HeadGeometry& head,
                                                  const EqCurve& curve,
                                                  const HeadGeometry& recordHead,
                                                  double maxBoostDb = 20.0) noexcept
    {
        const double reproduce = reproduceResponse(hz, curve);
        const double losses = playbackResponse(hz, speedMps, head)
                            * recordingResponse(hz, speedMps, recordHead);
        const double product = reproduce * losses;
        if (!(product > 0.0))
            return std::pow(10.0, maxBoostDb / 20.0);
        return std::clamp(1.0 / product,
                          std::pow(10.0, -maxBoostDb / 20.0),
                          std::pow(10.0, maxBoostDb / 20.0));
    }
}
