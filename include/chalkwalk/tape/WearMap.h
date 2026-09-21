#pragma once

// What a reel loses by being played (ROADMAP.md "Wear from passes", SOURCES
// section 23).
//
// THE MEDIUM HOLDS MAGNETISATION; THIS HOLDS OXIDE INTEGRITY. They are
// different quantities stored in different places, which is why wear needs no
// amendment to PRINCIPLES section 2 -- a playback-side change still leaves
// `Medium` byte-identical, because playing a tape writes here and not there.
// The permanence is real all the same: the map is monotonic and nothing
// restores it.
//
// THREE QUANTITIES AT TWO SCALES (SOURCES section 23):
//
//   coating    per region   remaining magnetic layer, as a fraction of new
//   lubricant  per region   what is left of the surface lubricant
//   binderAge  reel-wide    hydrolysis, which is a property of the whole tape
//
// Lubricant is a separate slot rather than a curve fitted to `coating` because
// CLIR pub54 says it is "partially consumed every time the tape is played" and
// migrates to the heads -- tens of passes, where oxide takes hundreds. That is
// a middle phase one quantity cannot express: a well-used tape squeals and
// wears faster BEFORE it sounds any worse.
//
// THE MAPS ARE IN METRES AND ARE INTERPOLATED IN SPACE. Never a duration, never
// a sample count, and never smoothed in time. A region is a piece of tape and
// stays that piece of tape at any speed, in either direction -- so the same
// tape reads the same forwards, backwards, at half speed and at twelve times
// speed, which is the invariant every other loss in this engine already obeys.
//
// NO PER-MACHINE SETTING, AND DELIBERATELY SO. Wear is passes per region times
// shed per pass, and topology supplies the first: over ten minutes of running,
// Capstan's 32-minute linear reel sees under one pass per region while Splice's
// four-second loop sees a hundred and fifty. Capstan comes out clean BY
// CONSTRUCTION rather than by a "wear: off" default, which would be a quality
// tier wearing a different hat (`fence #2`).
//
// THEY ARE **UNSOURCED RATES**, WHICH IS A PERMANENT CONDITION AND NOT A DEBT.
// SOURCES section 23 sources the MECHANISM and explicitly sources no rate: "passes to audible degradation, lubricant
// lifetime, deposit accumulation per metre: none of it is here". So the
// constants below are stated as the lifetimes they mean -- 500 passes to bare,
// 40 to dry -- and the shed rate is DERIVED from that lifetime by closing the
// integral, rather than a tuned number whose meaning has to be reverse
// engineered. Tests assert shape: monotonic, accelerating, top end first.
//
// JUCE-free by design. Part of chalkwalk-tape.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chalkwalk::tape
{
    // How a reel is consumed. Every field here is an **UNSOURCED RATE**
    // (SOURCES section 23 sources the mechanism and explicitly no rate), except
    // the three marked ANCHORED, which are scored against the structural
    // anchors in `RemanenceBench spall-fit`. Each is expressed as the lifetime
    // it stands for so that changing one is changing a claim rather than a
    // magic number.
    //
    // THIRTEEN OF THEM AND THREE ARE ANCHORED, which the anchor bench now says
    // out loud: an admissible cell is admissible GIVEN the other ten sitting
    // where they sit, so the fit is conditional and the region would move if
    // any of them did.
    struct WearConstants
    {
        // The resolution of a slowly-varying statistic, and a LENGTH because
        // everything else here is. ~10 mm is 38 regions per second of Capstan
        // tape and 4.8 of Splice's -- a slow machine needs fewer entries per
        // second, which is the right way round.
        //
        // This is NOT the scale that has to be audio-relevant. A defect of a
        // given length takes out everything above v/lambda, so defect SIZE is
        // load-bearing and region size is not; dropouts arrive later and carry
        // their own spatial extent.
        double regionMetres = 0.010;

        // Passes for a fresh, well-lubricated reel to POLISH ITSELF AWAY --
        // which is a different and much longer number than the one for a reel
        // to die, because abrasion is not what kills a reel.
        //
        // THIS WAS 500 AND THE MEASUREMENT SAYS IT IS FAR TOO FAST. dlp 2.1
        // shows no wavelength signature at all for seventy passes: at the point
        // where its level starts to move, the 5-10 kHz band is within 0.6 dB of
        // the 100-300 Hz band, where this engine at the same level loss put the
        // top 3 to 5 dB further down. A thinning coating has a fingerprint and
        // the recording does not carry it, so the coating was not thinning.
        //
        // ABRASION IS THE FUSE, NOT THE DAMAGE. What it does inside a reel's
        // playable life is strip the lubricant and roughen the surface -- which
        // raises FRICTION, which fatigues the binder, which is what eventually
        // lets go. The signal barely moves until it does.
        double passesToBare = 3000.0;

        // Passes to a dry surface. Tens, per CLIR -- an order below the oxide,
        // which is the entire reason for the second slot.
        double passesToDry = 40.0;

        // How much faster a scuffed region sheds than a fresh one, at the
        // extreme: the rate is multiplied by `1 + wornSheds * (1 - coating)`.
        // This is what gives disintegration a KNEE instead of a fade, and it is
        // why an old reel dies quickly once it starts.
        double wornSheds = 3.0;

        // A dry region sheds this much faster than a lubricated one, and drives
        // scrape flutter by the same factor (SOURCES section 19: friction is
        // what excites it). Lubricant is gone long before the oxide, so
        // `passesToBare` is calibrated with this factor already applied --
        // otherwise the quoted lifetime would be one the tape never sees.
        double dryFriction = 2.0;

        // What hydrolysis does to the ABRASION rate, at `binderAge` 1: five
        // times faster, because a softened binder polishes away faster. Modest,
        // and deliberately so.
        //
        // THIS WAS 500, AND THAT WAS SPALLATION'S JOB DONE BADLY. Making
        // abrasion five hundred times faster did destroy an old reel quickly,
        // but it destroyed it SMOOTHLY -- abrasion is a continuous process and
        // no multiplier turns it into a knee. Measured against dlp 2.1, which
        // holds for seventy passes and then loses forty decibels in thirteen,
        // the shape was wrong at every age tried. The collapse belongs to
        // `spallCapacityPasses` below, which can produce an inflection because
        // it is a different mechanism rather than the same one hurried.
        double binderCollapse = 5.0;

        // Roughening, as a fraction of the coating's own thickness -- and at
        // 1.0 this is GEOMETRY RATHER THAN A TUNING CONSTANT, which is why it
        // is the default. Where oxide has gone the magnetic surface has
        // RECEDED by exactly the depth that left, and the head rides on the
        // tape around the pit, so head-to-oxide spacing grows by the coating
        // lost. It scales per machine for free: a thin cassette coating cannot
        // open as large a gap as a mastering tape's. Roughness needs no slot of
        // its own for the same reason.
        //
        // The bound, not the mean: wear that thinned the WHOLE tape evenly
        // would be followed by the head and cost no spacing at all. Wear here
        // is patchy by construction -- that is what a per-region map is -- so
        // the bridging case is the representative one, and a machine that
        // wanted the softer reading has the constant to turn down.
        double pitFraction = 1.0;

        // Hydrolysis roughens the surface BEFORE anything is lost: a sticky
        // tape is soft and dull while still carrying a full coating, which is
        // Basinski's tape before it is played. This term is what lets the state
        // express "bad surface, full coating" at all.
        double binderRoughFraction = 0.15;

        // The floor, so the tape is never non-physical and the dial always has
        // somewhere to go in both directions. Bare backing, not zero coating.
        double coatingFloor = 0.01;

        // How much a worn SURFACE adds to friction, on top of the lubricant
        // being gone. A polished coating is not a rough one, and roughness is
        // what abrasion leaves behind -- so the small thinning that does not
        // show in the signal shows here instead, which is the whole of how
        // abrasion reaches spallation. UNSOURCED RATE.
        double roughFriction = 3.0;

        // ---- SPALLATION: THE OTHER WAY A REEL LOSES ITS COATING ----
        //
        // NEW TAPE THINS AND OLD TAPE LOSES LUMPS, and they are different
        // mechanisms rather than one mechanism at two speeds. Abrasion above is
        // continuous polishing: smooth, spatially even, and it cannot produce
        // anything but a smooth curve at any setting. Spallation is chunks
        // detaching because the binder has failed, and it is what the
        // preservation literature describes -- CLIR's shed and IASA's sticky
        // pigment are lumps of coating, not a thinner layer (`SOURCES §23`).
        //
        // WHY IT MATTERS, MEASURED: dlp 2.1 holds within a few decibels for
        // seventy passes and then loses forty in thirteen. Accelerating
        // abrasion declines smoothly from the first pass at every age -- the
        // shape is wrong, not the constant -- and the two-regime curve with an
        // inflection is what the recording actually shows.
        //
        // MODELLED AS FATIGUE, WHICH NEEDS NO RANDOMNESS. Each pass applies
        // stress; the binder has a capacity that hydrolysis eats away; a region
        // spalls when accumulated stress exceeds its capacity. Deterministic,
        // position-addressed, and reproducible on any computer (`PRINCIPLES
        // §5`) -- where a per-pass probability would need a generator and would
        // give a different reel every time the same project opened.
        //
        // AND THE TRANSITION IS SHARP BECAUSE IT IS AUTOCATALYTIC. A region
        // that has gone hands stress to its neighbours -- a hole has edges, and
        // edges are where the next chunk lifts -- so the first failure pushes
        // the next two closer, and the reel comes apart along its grain rather
        // than everywhere at once. That is where the inflection comes from
        // rather than from a threshold anybody chose.
        //
        // EVERY RATE HERE IS AN UNSOURCED RATE, exactly as the abrasion rates
        // are: `SOURCES §23` sources the mechanism and explicitly no number.
        //
        // AND THE THREE BELOW ARE **ANCHORED** ON TOP OF THAT -- capacity,
        // spread and damage power are the ones `RemanenceBench spall-fit`
        // scores. Read that mode's header before moving one: two of its five
        // original anchors were dlp 1.1 and the Loops are a HOLDOUT
        // (`SOURCES §32`), so the search now runs on the structural anchors
        // alone and the references are reported as checks it may not select on.

        // Passes of stress a FRESH reel's binder can take. Far beyond
        // `passesToBare`, so new tape wears out by abrasion and never spalls at
        // all -- which is the regime separation, not a tuning.
        double spallCapacityPasses = 2500.0;

        // How steeply hydrolysis eats that capacity. At `binderAge` 1 the
        // capacity is `(1 - 1)^p` of its fresh value, so it is the exponent
        // that decides where the knee falls for a given age.
        double spallBinderPower = 1.5;

        // WHAT ONE EVENT TAKES IS NOT A CONSTANT -- IT IS A RATIO OF TWO
        // LENGTHS THAT ARE ALREADY HERE. One event is ONE FLAKE letting go, and
        // a flake has a size (`flakeMetres`, 1 mm); a region has a size too
        // (`regionMetres`, ~10 mm). So an event takes `flakeMetres /
        // regionMetres` of the region -- a tenth -- and there is nothing left
        // to choose. See `spallChunkFraction()`, which reads the EFFECTIVE
        // region size, since a circular reel adjusts it to close the loop.
        //
        // IT WAS 0.55, AND THAT MADE REGION SIZE LOAD-BEARING, which `regionMetres`
        // above says it must not be: at half the resolution the same event would
        // still have taken 55%, so the reel's damage per event tracked an array
        // dimension rather than a piece of physics. A ratio is invariant to it.
        //
        // MEASURED, AND IT IS THE CONCENTRATION THAT CONDEMNED IT. At 0.55 a
        // single pass carried 16.1% of the reel's whole decline where dlp 1.1's
        // worst carries 5.4% -- a staircase with two risers against a surface
        // that is rough everywhere. `the reel loses level in many small steps`
        // holds it, and the number the test reports is that percentage.

        // How much of a neighbour's remaining capacity an event consumes. This
        // is the autocatalysis IN SPACE, and it is why the reel goes in a
        // cascade rather than region by region.
        //
        // SOLVED, NOT CHOSEN -- and it is the one constant the anchors moved
        // when `spallChunk` became a ratio of lengths. With a tenth taken per
        // event instead of a half, a cascade needs to recruit more neighbours
        // to deliver the same damage, and 0.35 left a fresh reel only 17.6 dB
        // down at a thousand plays where studio practice says unusable.
        //
        // `RemanenceBench spall-fit` is the instrument (`PRINCIPLES section 7`)
        // and reproduces this: of 432 cells over (capacity x spread x power),
        // 115 satisfy all four anchors, so THE ANCHORS DEFINE A REGION AND NOT
        // A POINT. What picks this one out of it is that it moves least -- the
        // other two constants keep the values they already had -- and that it
        // is interior, every neighbour in a fine grid passing too. On the two
        // figures the anchor is scale-free in, it lands where the record does:
        // the largest single step carries 4.3% of the decline against dlp 1.1's
        // 5.4%, and the largest five carry 20.6% against its 20.6%.
        double spallSpread = 0.44;

        // ---- AND THE AUTOCATALYSIS IN TIME ----
        //
        // A region that has already lost coating has less binder holding what
        // is left, so its capacity is lower and the next chunk comes sooner.
        // Physically obvious, and the model was missing it.
        //
        // MEASURED, WHICH IS WHY IT IS HERE. dlp 2.1's collapse runs at
        // 3.67 dB per pass; without this term the steepest this engine reaches
        // at ANY age is 1.55, and only on a reel that knees a fifth of the way
        // in. That is not a constant to tune -- no setting of the others gets
        // there -- and it explained the shape of the mismatch exactly: a slow
        // cascade has to START early to get deep, so depth and a late knee
        // could not both be had.
        //
        // As a power of the INTACT AREA that remains. At 1 the intervals
        // between a region's events fall geometrically, which is what a runaway
        // is.
        double spallDamagePower = 1.0;

        // ---- AND WHERE IT STOPS, BECAUSE HYDROLYSIS IS A SURFACE PROCESS ----
        //
        // Hydrolysis needs water, and water arrives at the exposed face. It
        // eats the binder inward from there and reaches the depth its exposure
        // bought; the binder under that was never wetted and is as sound as the
        // day it was coated. So a sticky-shed reel loses ITS SURFACE LAYER and
        // then largely arrests. It does not go bare.
        //
        // MEASURED, AND THE MODEL WAS WRONG WITHOUT IT. dlp 1.1 is 11.5 dB down
        // at pass 409 -- the last pass before the fader moves -- and plainly
        // audible; this engine ran a reel of ANY age to 40 dB down inside three
        // hundred passes, because `spallDamagePower` makes a region that starts
        // finish and nothing stopped it. `ROADMAP.md` has the measurement.
        //
        // The depth reached at `binderAge` 1, as a fraction of the coating.
        // UNSOURCED RATE; the mechanism is `SOURCES §23` and it gives no number.
        // NOT anchored -- it is one of the ten held fixed while the three are
        // searched, which is what makes that search conditional.
        double hydrolysisDepth = 0.80;

        // ---- HOW THE DEPTH GROWS WITH AGE: DIFFUSION, NOT A RAMP ----
        //
        // THIS WAS 1.0, MARKED "linear until something says otherwise".
        // Something does. Hydrolysis needs water and water arrives at the
        // exposed face, so the depth is set by how far it has DIFFUSED inward --
        // and diffusion depth goes as the square root of time, not linearly.
        // That is the same `sqrt(Dt)` that governs every other wetting front.
        //
        // WHAT IT CHANGES, and it is a shape rather than an amount: most of the
        // depth arrives EARLY and the approach to the ceiling flattens. A reel
        // half way through its shelf life is 71 % of the way to its final depth
        // rather than 50, so the difference between a fifteen-year-old tape and
        // a thirty-year-old one is much smaller than the difference between
        // fresh and fifteen -- which is what the archival literature describes
        // and what a linear law cannot give.
        //
        // It also softens the corner at full age, because the derivative is
        // largest at zero and smallest at one, where the linear law's was
        // constant all the way to the stop.
        double hydrolysisDepthPower = 1.0;

        // ---- AND THE DEPTH IS NOT THE SAME EVERYWHERE ----
        //
        // How much deeper the weakest binder is hydrolysed than the strongest,
        // as a RATIO, applied geometrically about the field's own mean so the
        // reel-wide depth is unchanged and only its EVENNESS differs. Exactly
        // the form `grainShedRatio` uses, and for the same reason: the one
        // mechanism here that is sourced is that hydrolysis attacks binder, and
        // binder is what varies.
        //
        // WITHOUT THIS EVERY REGION MEETS AN IDENTICAL FLOOR AT AN IDENTICAL
        // MOMENT. Thickness varies, shed rate varies, and where the damage sits
        // varies -- and then the one thing that says how far it can GO was a
        // single number for the whole reel, so the tape arrived at its ceiling
        // all at once instead of in the patches everything else is at pains to
        // produce.
        //
        // UNSOURCED RATE. The mechanism is `SOURCES §23`; that binder
        // weakness and hydrolysis depth are the same variation seen twice is
        // the assumption, and it is a smaller one than giving the depth a
        // second field of its own.
        double hydrolysisDepthRatio = 1.5;

        // ---- AND BELOW THE FLOOR, HOW MUCH THINS RATHER THAN FLAKES ----
        //
        // Above the floor the loss is the hydrolysed layer lifting, which is
        // thinning over the whole area. Below it the binder is sound, and
        // treating ALL of that as area loss is what put bare backing on the
        // tape: at `intact` 0.04 against a floor of 0.36, area loss alone makes
        // 89% of the region silent.
        //
        // But sound binder does not only flake. It also THINS, which is what
        // abrasion is, and abrasion does not stop at the floor. So the loss
        // below the floor splits, and this is the thinning share as an
        // exponent: 0 is all flakes and bare backing, 1 is all thinning and no
        // holes at all.
        //
        //   thickness = floor * (intact / floor)^a
        //   area      =         (intact / floor)^(1 - a)
        //
        // whose product is `intact` for any `a`, so the level is untouched and
        // only the split moves. UNSOURCED RATE, and set by what a
        // disintegrating loop sounds like -- attenuation with holes in it
        // rather than holes with attenuation in them.
        //
        // "SOUNDS LIKE" IS DOING WORK HERE AND IS WORTH WATCHING. If the loop
        // meant is dlp 1.1 then this was fitted to the holdout (`SOURCES §32`);
        // the claim it is meant to encode is the generic one -- that a coating
        // coming apart thins as well as holes -- which needs no reference. It
        // is recorded rather than repaired because moving it is a change to the
        // sound and this note is a change to what it claims.
        double soundThinningShare = 0.65;


        // ---- HOW MUCH THE TAPE VARIES FROM ITSELF ----
        //
        // WITHOUT THIS A LOOP FLAKES UNIFORMLY, and that is not what tape does.
        // Every region of a loop is passed the same number of times, so an even
        // tape wears evenly: measured on a Splice loop after forty-one laps,
        // the coating ran 8.6% to 10.1% everywhere -- a spatial fade, with all
        // the character in the spectrum and none of it on the tape. Real
        // disintegration is patchy: some of the loop goes bare while the rest
        // still plays, which is most of what those recordings sound like.
        //
        // THIS IS MANUFACTURE, NOT WEAR, and that is why it belongs here rather
        // than in `binderAge`. Binder condition is the REVERSIBLE half -- baking
        // restores it (IASA TC-05) -- so making it per-region would mean baking
        // had to be per-region too, which is not a thing an oven does. What
        // varies along a reel permanently is how the coating was laid down: the
        // thin, weakly-bound places were thin and weakly bound when it left the
        // factory, and no service reaches them.
        //
        // ONE FIELD, TWO CONSEQUENCES, because they are one fact. A weak place
        // is both slightly thinner NOW and sheds faster once it starts, and the
        // second is what makes the difference grow rather than stay a texture:
        // it compounds with `wornSheds`, so a reel pulls apart along the grain
        // it was made with.
        //
        // 1/f IN SPACE, not white. White noise at region resolution is a
        // dither, not a grain: what is wanted is structure at every scale from
        // the whole reel down to a few centimetres, which is what a physical
        // coating process leaves.
        double grainDepth = 0.06;    // UNSOURCED RATE: how much thinner the worst is

        // How much faster the weakest tape sheds than the strongest, as a
        // RATIO. Applied geometrically about 1, so the mean rate is unchanged
        // and the calibrated lifetime still means what `passesToBare` says --
        // an additive form silently doubled it, because the field's mean is a
        // half and `1 + shed * 0.5` is not 1. Reinstating that is a teeth check.
        double grainShedRatio = 4.0;  // UNSOURCED RATE

        // How much further age spreads that ratio, as an exponent. At 2.0 a
        // fully hydrolysed reel is 4^3 = 64 to one rather than 4 to one, so it
        // comes apart in patches on the first play instead of thinning evenly
        // over hundreds. UNSOURCED RATE, and the mechanism is the one thing here
        // that IS sourced: hydrolysis attacks binder, and binder is what varies.
        double grainAgeContrast = 2.0;

        // The finest scale the grain has structure at. A LENGTH, because binder
        // weakness has a characteristic size and not a characteristic fraction
        // of whatever reel it is on -- so the same tape grains the same whether
        // it is cut into a four-second loop or left on a spool.
        double grainFinestMetres = 0.05;

        // ---- THE STRIATION FIELD: WHERE THE DAMAGE ACTUALLY IS ----
        //
        // The maps above are a SLOW STATISTIC at 10 mm, which is 26 ms of
        // Capstan tape and 210 ms of Splice's -- far too coarse to place a
        // defect, and on a loop the same boundary comes round every lap, so an
        // edge there is a periodic click rather than a flake. Every dropout
        // this engine made was 210 ms wide and sat on that lattice, which is
        // why the renders sounded like holes being cut rather than like tape
        // coming apart.
        //
        // So the region keeps the BOOKKEEPING and this does the REALISATION.
        // `intact` stops being read as a level and becomes a THRESHOLD on a
        // field: with the field uniform on [0, 1], thresholding at `s` removes
        // an area fraction of exactly `s`, so the region mean is unchanged and
        // every mean-based test still holds. Only where the damage is moves.
        //
        // The field's resolution, and the finest structure in it. A flake is
        // millimetre-scale, so the fine end is what produces crackle and the
        // coarse end what produces dropouts -- one field, and the SCALE
        // DISTRIBUTION decides the character.
        //
        // BOTH ARE DERIVED FROM THE FLAKE, and the resolution is NOT a taste:
        // it is set by the closed form that makes this whole scheme work.
        // `realiseIntact` thresholds the field so the realised mean is exactly
        // the region's `intact`, which holds only while the field is uniform AT
        // THE RESOLUTION IT IS READ -- and `striationAt` interpolates linearly
        // between cells, which pulls interpolated values toward the middle when
        // neighbouring cells are not already close. Measured, at a band centred
        // on a 1 mm flake:
        //
        //   samples per flake     5      10      20      40      80
        //   worst mean error   .0049   .0025   .0008   .0002  .00005
        //
        // It falls as the square. TWENTY is the coarsest that holds the
        // agreement: 0.0008 of area fraction is 0.007 dB of level, which is
        // nothing, and it sits inside `the damage sits where a flake is`'s
        // 0.002 margin. Forty would give another order of magnitude and costs
        // 2 MB and 138 ms on `prepare` for a long reel, against 1 MB and 70 ms
        // here -- and 145 ms of `prepare` is already recorded in this file as a
        // thing worth removing.
        //
        // At 0.2 mm, which is what this was, a 1 mm flake had five samples
        // across it and the realisation drifted 0.005 from the bookkeeping --
        // the one disagreement the field exists to prevent.
        double striationMetres = 0.0;           // 0, meaning flakeMetres / 20
        double striationFinestMetres = 0.0;     // 0, meaning flakeMetres / 4

        // A FLAKE HAS A CHARACTERISTIC SIZE, so the field is band-limited and
        // NOT 1/f. Straight 1/f amplitudes put nearly all the energy in the
        // coarsest octave, which makes the field one slow ramp across the reel:
        // measured, that gave five dropouts per loop with one of them five
        // seconds long, which is the same "holes cut out of the recording"
        // defect at a different size. Fracture picks a scale -- binder
        // thickness and adhesion set it -- so the spectrum is a BAND about that
        // scale, and the width is what carries crackle above it and clustering
        // below.
        //
        // AND THE SCALE FRACTURE PICKS IS THE FLAKE. There is no second length
        // here to choose: the field exists to say where flakes are, so it is
        // centred on `flakeMetres` and `striationScale()` returns exactly that.
        //
        // IT WAS 0.008, EIGHT TIMES THE FLAKE, AND IT WAS FITTED TO THE HOLDOUT.
        // The comment said so: "8 mm is 42 ms at 7.5 ips, which is the middle of
        // the 50-200 ms the disintegration loops sound like. UNSOURCED as a
        // number." Three things were wrong with that. The machine was a Revox
        // G36 at 3.75 ips, not 7.5, so 8 mm is 84 ms and not 42 (`SOURCES §32`).
        // The 50-200 ms was an estimate by ear, and measured against an early
        // lap of the same loop the irreversible dips are 5-12 ms. And the
        // Disintegration Loops are a HOLDOUT, so nothing may be fitted to them
        // at any speed.
        //
        // WHAT IT COST, MEASURED: a field eight times the flake size clusters
        // flakes into holes, and the excess showed in the TAIL. Against dlp 1.1
        // at a matched damaged fraction and with the reverb matched too, our
        // dropouts ran 2 times too long in the median and 8.7 times too long in
        // the maximum.

        // ---- AND THE HEAD IS WIDER THAN A FLAKE ----
        //
        // A flake is millimetre-scale in BOTH directions and a track is 1.78 mm
        // wide on Capstan and 1.9 mm on the A77, so a flake takes part of the
        // track's width and rarely all of it. The head integrates flux ACROSS
        // that width, so what it reads is the average of several places, and it
        // is nearly impossible for all of them to be bare at once.
        //
        // MEASURED, AND THIS IS THE WHOLE OF THE DIFFERENCE. Taking 20 ms
        // frames and asking how far each sits below its own passage's loud
        // level, dlp 1.1 puts 0.0% of them below -20 dB -- at pass 1 and still
        // at pass 340, so degradation does not change it. Sampling the field at
        // ONE point across the width, this engine put 32.6% of them there. Real
        // flaked tape attenuates; it does not drop out.
        //
        // So average the realisation across the track, over as many independent
        // places as the track is wide in FLAKES. That is a ratio of two lengths
        // and neither of them is a free parameter:
        //
        //   Capstan   1.78 mm track / 1 mm flake  =  1.8 places
        //   Slipback  1.90 mm                     =  1.9
        //   Splice    0.60 mm                     =  0.6, so ONE
        //
        // AND THE CASSETTE ROW IS A PREDICTION, not a shrug. A cassette track
        // is NARROWER THAN A FLAKE, so nothing averages and a flake takes the
        // whole track -- a cassette really does drop out where a multitrack
        // only dips, and that falls out of the geometry rather than being put
        // in. `striationWidthSamples` was 4 with nothing behind it.
        //
        // The flake DIAMETER. A millimetre is the scale of a chip of coating;
        // the field's 8 mm is a CLUSTER of them, which is why the two are
        // different numbers. UNSOURCED, and a DEBT rather than a permanent
        // condition: microscopy of shed tape publishes flake sizes, so this is
        // one of the few wear numbers a document could settle outright. It is
        // also now load-bearing beyond this file -- `SOURCES §39` derives the
        // music/click crossover and the read-depth tilt from it.
        double flakeMetres = 0.001;

        // Set from the machine at prepare. The default is a quarter-inch
        // two-track, so a map prepared without one is not silly.
        //
        // AND 1.9 mm IS THE AMPEX STANDARD, which this carried as a plausible
        // round number and can now cite: MRL gives the three standard two-track
        // widths on quarter-inch tape as 1.9 mm (Ampex, the original de facto
        // standard), 2.1 mm (NAB) and 2.8 mm (IEC stereo) -- `SOURCES §46`.
        // The same figure is what closes the tape-against-electronics noise
        // split in `§45`, because an Ampex tape sheet is quoted at it.
        //
        // AND STUDER MEASURED THE LAW ON ITS OWN MACHINE: the C37 manual says
        // that "due to the reduced track width on Stereo Recorders the above
        // indicated signal to noise ratios will be reduced by approx. 5 dB"
        // (`SOURCES §47`). Full-track quarter-inch to 1.9 mm is 5.21 dB by
        // `1/sqrt(width)`, to 2.1 mm is 4.77, and to 2.8 mm is 3.52 -- so the
        // measurement confirms both the law and this width, and rules out the
        // IEC 2.8 mm reading.
        double trackWidthMetres = 1.9e-3;   // SOURCES 46

        // How far apart in the field the lanes are sampled. Far enough to be
        // independent, which is all that is asked of it.
        double striationWidthStrideMetres = 0.317;

        // The band's width in OCTAVES either side. At 1.5 the field has real
        // structure from about 1 mm to about 64 mm: crackle at the fine end,
        // clustered dropouts at the coarse end, one field.
        double striationOctaves = 1.5;

        // THE BAND, and it is what stops this sounding digital. A hard
        // threshold is a gate: the edge is one sample wide and it clicks. This
        // takes a narrow band of the field either side of the threshold and
        // stretches it across the full depth, so the transition from present to
        // absent is a RAMP -- and because the band is fixed in FIELD units
        // while the field's gradient varies over orders of magnitude, the edge
        // length in milliseconds comes out as a DISTRIBUTION rather than as one
        // characteristic time. One characteristic time is what a gate has.
        //
        // Smootherstep across the band rather than linear: a linear ramp still
        // has a corner at each end where the slope jumps, which is the same
        // first-derivative discontinuity the region interpolation was changed
        // to avoid.
        //
        // THIS IS NOW A MINIMUM, NOT THE BAND. The band is solved from the
        // region's own bare fraction (see `realiseIntact`); this is the floor
        // under it, and it exists only so that a region with no thinning does
        // not get a hard threshold and an edge one sample wide.
        double striationBand = 0.08;

        // Cap on the table. A loop shorter than this gets a field that closes
        // on it exactly; a reel tiles at this period.
        double striationMaxMetres = 13.1;
    };

    // Passes per unit of coating lost, closing the integral rather than tuning.
    //
    // The evolution is dc/dn = -r (1 + w (1 - c)), so the passes from c = 1 to
    // the floor are ln(1 + w (1 - floor)) / (r w) and this inverts that. With
    // `wornSheds` at zero it degenerates to the straight line, which is why the
    // branch is here rather than an assertion.
    [[nodiscard]] inline double shedPerPass(const WearConstants& k) noexcept
    {
        const double span = 1.0 - k.coatingFloor;
        const double passes = std::max(1.0, k.passesToBare);
        // Divided by the dry-friction factor because the quoted lifetime is
        // the one the tape actually sees: lubricant is gone inside the first
        // tens of passes, so a reel spends nearly all of its life at the dry
        // rate. Calibrating on the lubricated rate would quote a lifetime no
        // reel reaches.
        const double dry = std::max(1.0, k.dryFriction);
        if (k.wornSheds <= 0.0)
            return span / (passes * dry);
        return std::log1p(k.wornSheds * span) / (k.wornSheds * passes * dry);
    }

    // A reel's oxide integrity: two maps in metres and one reel-wide scalar.
    //
    // `pass()` is called from the audio thread and allocates nothing; `prepare`
    // is where the storage comes from.
    class WearMap
    {
    public:
        struct State
        {
            // THE THICKNESS OF THE COATING WHERE THERE IS COATING. Abrasion
            // thins this, and it is what every WAVELENGTH loss reads: a
            // receding surface raises spacing, and a shallower layer changes
            // the thickness term.
            double coating = 1.0;

            // AND HOW MUCH OF THE AREA STILL HAS ANY. Spallation takes chunks
            // away completely, leaving bare backing beside full-thickness
            // coating, and a head averages flux across the two -- so this is a
            // BROADBAND level with no wavelength in it at all.
            //
            // The two are separate because they sound different, and the
            // measurement is what says so: dlp 2.1 loses level FLAT, with the
            // 5-10 kHz band inside 0.6 dB of the 100-300 Hz band all the way
            // down, where a thinning coating would have taken the top off
            // first. Flakes are an area loss. Modelling them as thinning gave
            // this engine a wavelength signature the real thing does not have.
            double intact = 1.0;

            double lubricant = 1.0;
        };

        // `circular` is the tape's topology, and it is not decoration: on a
        // loop the last region and the first are ADJACENT PIECES OF TAPE, so
        // the map has to read across the join and not stop at it.
        //
        // `reelSeed` is the REEL's identity, not the machine's -- two objects,
        // two clocks, and now two seeds. A reel grains the same way every time
        // it is loaded, on any computer (`PRINCIPLES §5`), because the field is
        // synthesised from integers and never from a running generator.
        void prepare(double lengthMetres, const WearConstants& constants,
                     bool circular = false, std::uint64_t reelSeed = 0)
        {
            reelSeed_ = reelSeed;
            constants_ = constants;
            circular_ = circular;
            lengthMetres_ = std::max(constants_.regionMetres, lengthMetres);

            // THE GRID MUST DIVIDE THE TAPE EXACTLY, and taking `ceil` did not.
            //
            // With 10 mm regions on a 476 mm loop the last region covered 6 mm
            // of tape and 4 mm of nothing, so the walk's boundary (480 mm) and
            // the tape's end (476 mm) disagreed: a span crossing the join was
            // charged to the wrong region, and the two regions either side of
            // it came out systematically LESS worn than the rest. Measured on a
            // Splice-length loop after two hundred laps that was a 0.06 bright
            // band in the coating -- once per lap, which is exactly what a
            // periodic jump sounds like.
            //
            // So the requested region size is a TARGET and the effective one is
            // the nearest size that closes. It moves by at most half a region
            // on the shortest tape here and by parts per million on a reel.
            const auto regions = std::max<std::size_t>(2, static_cast<std::size_t>(
                std::llround(lengthMetres_ / constants_.regionMetres)));
            constants_.regionMetres = lengthMetres_ / static_cast<double>(regions);

            // THE FIELD'S TWO LENGTHS, RESOLVED FROM THE FLAKE. Zero means
            // "derive", which is the default and what every machine uses; a
            // caller that sets either explicitly keeps it, which is how the
            // resolution study above was run. `striationBand` remains the
            // switch that turns the realisation off.
            if (constants_.striationMetres <= 0.0)
                constants_.striationMetres = constants_.flakeMetres / 20.0;
            if (constants_.striationFinestMetres <= 0.0)
                constants_.striationFinestMetres = constants_.flakeMetres / 4.0;

            coating_.assign(regions, 1.0f);
            intact_.assign(regions, 1.0f);
            lubricant_.assign(regions, 1.0f);
            fatigue_.assign(regions, 0.0f);
            buildGrain(regions);
            buildStriation();
            binderAge_ = 0.0;
            loadFreshStock();
        }

        // Fresh stock. Not a service action -- it is a new reel, and the caller
        // is what decides whether that is offered.
        void loadFreshStock() noexcept
        {
            std::fill(fatigue_.begin(), fatigue_.end(), 0.0f);
            std::fill(intact_.begin(), intact_.end(), 1.0f);
            // NEW TAPE IS NOT PERFECT TAPE. It comes off the line with its
            // grain already in it, which is why a fresh reel does not sound
            // synthetic and why the wear that follows has something to follow.
            for (std::size_t i = 0; i < coating_.size(); ++i)
                coating_[i] = static_cast<float>(freshCoating(i));
            std::fill(lubricant_.begin(), lubricant_.end(), 1.0f);
            binderAge_ = 0.0;
        }

        // What this piece of tape started as, and how fast it gives way. Both
        // come off the same field, because they are one fact about the coating.
        [[nodiscard]] double freshCoating(std::size_t region) const noexcept
        {
            if (region >= grain_.size())
                return 1.0;
            return 1.0 - constants_.grainDepth
                             * (1.0 - static_cast<double>(grain_[region]));
        }

        // Geometric about 1: the weakest region sheds `sqrt(ratio)` times the
        // mean and the strongest `1/sqrt(ratio)` of it, so the whole reel's
        // lifetime is the one `passesToBare` calibrates and only its EVENNESS
        // has changed. An even field (`grainDepth` at zero) gives exactly 1.
        [[nodiscard]] double shedScale(std::size_t region) const noexcept
        {
            if (region >= grain_.size() || constants_.grainShedRatio <= 1.0)
                return 1.0;
            // CENTRED ON THE FIELD'S OWN MEAN, not on a half. A 1/f sum
            // normalised to [0, 1] does not have its mean at the middle -- this
            // reel's is 0.58 -- so assuming one put the geometric mean rate at
            // 0.89 instead of 1 and quietly made every reel last longer than
            // `passesToBare` says.
            return std::pow(ratioFor(binderAge_),
                            grainMean_ - static_cast<double>(grain_[region]));
        }

        // HOW UNEVEN THE TAPE IS, AS A FUNCTION OF ITS AGE.
        //
        // A fresh reel's outcome is dominated by how many times each region was
        // passed: the coating is near enough uniform that the transport decides
        // everything, which is why the top of a reel wears from being rewound to
        // and a loop wears evenly. An old reel's is not. Hydrolysis attacks the
        // weakest binder first, so age does not merely scale the shed rate --
        // it PULLS THE REEL APART, and a badly aged tape can flake unevenly in a
        // single play where a new one would need hundreds.
        //
        // So the contrast is a power of the base ratio: four to one on new
        // stock, and `grainShedRatio ^ (1 + grainAgeContrast)` at full
        // hydrolysis, which at the defaults is sixty-four to one.
        [[nodiscard]] double ratioFor(double binderAge) const noexcept
        {
            return std::pow(constants_.grainShedRatio,
                            1.0 + constants_.grainAgeContrast
                                      * std::clamp(binderAge, 0.0, 1.0));
        }

        [[nodiscard]] const std::vector<float>& grainMap() const noexcept
        {
            return grain_;
        }

        [[nodiscard]] std::uint64_t reelSeed() const noexcept { return reelSeed_; }

        // THE REEL'S AVERAGE CONDITION, which is a different quantity from the
        // condition under the head and is wanted for a different job.
        //
        // The loss FILTER is designed from this. Once the tape has a grain, the
        // coating under the head crosses a design bucket almost every block --
        // measured, the offline renders went from ten seconds to minutes,
        // because each crossing commissions a 200 ms redesign. In a plugin the
        // design thread would simply fall behind and the filter would be stale,
        // which is worse than coarse.
        //
        // And it is not merely a performance dodge: a filter that takes
        // milliseconds to design cannot represent a feature 26 ms long. What a
        // redesign is FOR is the machine's standing condition -- speed, head
        // wear, how tired the tape is on average. The holes are carried by the
        // per-sample LEVEL, exactly, which is the split `TapeDeck` already
        // draws.
        //
        // What that leaves out is stated rather than hidden: a bare patch
        // should lose its TOP as well as its level, and that is a local spacing
        // bump with its own spatial extent -- a dropout, sized in tenths of a
        // millimetre (ROADMAP.md). It is the sharp half and it is not built.
        [[nodiscard]] double meanCoating() const noexcept
        {
            if (coating_.empty())
                return 1.0;
            double acc = 0.0;
            for (auto c : coating_)
                acc += static_cast<double>(c);
            return acc / static_cast<double>(coating_.size());
        }

        // AND HOW MUCH OF THE AREA IS STILL THERE, which is the other half of
        // what the head reads and the half that says how much material the reel
        // has SHED. `TapeDeck` differences it to drive head contamination:
        // oxide that has left the tape has gone somewhere, and some of it goes
        // onto the head (`SOURCES §23`, `§30`).
        [[nodiscard]] double meanIntact() const noexcept
        {
            if (intact_.empty())
                return 1.0;
            double acc = 0.0;
            for (auto a : intact_)
                acc += static_cast<double>(a);
            return acc / static_cast<double>(intact_.size());
        }

        // THE REEL'S IDENTITY TRAVELS WITH THE REEL, so loading one restores
        // the grain it was made with -- otherwise a reel opened tomorrow would
        // go on shedding along a different pattern from the one it has already
        // half worn through. Rebuilds the field; does NOT touch what has worn.
        void setReelSeed(std::uint64_t seed)
        {
            if (seed == reelSeed_)
                return;
            reelSeed_ = seed;
            buildGrain(coating_.size());
        }

        // WHERE THE EVOLUTION GETS TO, in closed form.
        //
        // `dc/dn = -r (1 + w (1 - c))` integrates to `c = 1 - (e^(r w n) - 1)/w`,
        // so "a reel with two hundred passes on it" is a value rather than a
        // simulation -- which is what a DIAL needs, since nobody is going to
        // wait while a knob plays a tape two hundred times.
        //
        // At the dry rate throughout, because that is what `passesToBare` is
        // calibrated at: lubricant is gone inside the first tens of passes, so
        // a reel spends nearly all of its life there.
        // `from` is what the region started at and `rate` is its own shed
        // multiplier, so the closed form follows the grain rather than
        // flattening it -- a dial that loaded a uniform reel would erase the
        // thing the grain exists to provide.
        [[nodiscard]] double coatingAfter(double passes, double from = 1.0,
                                          double rate = 1.0) const noexcept
        {
            const double n = std::max(0.0, passes);
            const double r = shedPerPass(constants_)
                           * std::max(1.0, constants_.dryFriction) * rate;
            const double w = constants_.wornSheds;
            if (w <= 0.0)
                return std::clamp(from - r * n, constants_.coatingFloor, 1.0);
            // u = 1 + w (1 - c) grows as exp(r w n), so this is that inverted
            // with u0 taken from `from` rather than assumed to be new tape.
            const double u0 = 1.0 + w * (1.0 - from);
            const double c = 1.0 - (u0 * std::exp(r * w * n) - 1.0) / w;
            return std::clamp(c, constants_.coatingFloor, 1.0);
        }

        [[nodiscard]] double lubricantAfter(double passes) const noexcept
        {
            const double n = std::max(0.0, passes);
            return std::clamp(1.0 - n / std::max(1.0, constants_.passesToDry), 0.0, 1.0);
        }

        // LOAD A REEL WITH THIS MUCH ON IT -- uniformly, because that is what
        // the gesture means. You did not play this tape; you put a different one
        // on, and a reel you have not played has no local history for the map to
        // preserve. Playing it then wears it BELOW this, unevenly, which is the
        // detail the next load throws away again.
        //
        // Allocation-free, and cheap enough to run from the audio thread when a
        // knob moves: it is a fill of two float arrays.
        void loadReel(double passes) noexcept
        {
            const auto l = static_cast<float>(lubricantAfter(passes));
            for (std::size_t i = 0; i < coating_.size(); ++i)
                coating_[i] = static_cast<float>(
                    coatingAfter(passes, freshCoating(i), shedScale(i)));
            std::fill(lubricant_.begin(), lubricant_.end(), l);
            // ABRASION ONLY. Spallation is a cascade and has no closed form, so
            // a reel loaded with passes on it is one that was polished rather
            // than one that flaked -- correct for the fresh stock this dial
            // usually describes, and understated at the top of both dials at
            // once (`ROADMAP.md`).
            std::fill(intact_.begin(), intact_.end(), 1.0f);
        }

        [[nodiscard]] double lastShed() const noexcept { return lastShed_; }
        [[nodiscard]] std::size_t numRegions() const noexcept { return coating_.size(); }
        [[nodiscard]] double lengthMetres() const noexcept { return lengthMetres_; }
        [[nodiscard]] const WearConstants& constants() const noexcept { return constants_; }

        // Hydrolysis, reel-wide and REVERSIBLE (IASA TC-05: baking
        // "reconditions for replay"). Raising it makes a lightly-used reel
        // sound bad at once and lowering it undoes that -- until it is played,
        // because it also multiplies the shed rate and what has shed is gone.
        void setBinderAge(double age) noexcept { binderAge_ = std::clamp(age, 0.0, 1.0); }
        [[nodiscard]] double binderAge() const noexcept { return binderAge_; }

        // What the tape is like at one point along it, interpolated in SPACE
        // between region centres.
        //
        // ON A LOOP THE ENDS ARE NEIGHBOURS. Clamping there holds the value
        // flat across the last half-region and then steps at the join, which is
        // a discontinuity once per lap -- on a reel the ends are the ends and
        // clamping is right, so the topology decides.
        [[nodiscard]] State at(double metres) const noexcept
        {
            if (coating_.empty())
                return {};
            const auto count = coating_.size();
            const double x = position(metres) / constants_.regionMetres - 0.5;

            std::size_t i = 0, j = 0;
            double f = 0.0;
            if (circular_)
            {
                const double wrapped = x < 0.0 ? x + static_cast<double>(count) : x;
                i = static_cast<std::size_t>(wrapped) % count;
                j = (i + 1) % count;
                f = wrapped - std::floor(wrapped);
            }
            else
            {
                const double clamped =
                    std::clamp(x, 0.0, static_cast<double>(count - 1));
                i = static_cast<std::size_t>(clamped);
                j = std::min(i + 1, count - 1);
                f = clamped - static_cast<double>(i);
            }
            // SMOOTHSTEP, NOT LINEAR, and it costs one multiply-add.
            //
            // Linear interpolation is continuous in VALUE and not in SLOPE, so
            // a strongly grained map puts a corner in the gain envelope at
            // every region centre -- at the region rate, which is 31 Hz on
            // Capstan and 4.6 Hz on Splice. A corner is a discontinuity in the
            // first derivative and it is audible in the same way the transport
            // slew's was.
            //
            // AND IT IS NOT WHERE EDGES BELONG. A region is 10 mm, which is
            // 26 ms of Capstan tape and 210 ms of Splice's -- far too coarse to
            // place a dropout, and on a loop the same boundary comes round every
            // lap, so an edge here would be a periodic click rather than a
            // defect. Dropouts are sized in tenths of a millimetre and carry
            // their own spatial shape (ROADMAP.md); they are the sharp half,
            // and this is the smooth one.
            const double t = f * f * (3.0 - 2.0 * f);
            return { lerp(coating_[i], coating_[j], t),
                     lerp(intact_[i], intact_[j], t),
                     lerp(lubricant_[i], lubricant_[j], t) };
        }

        // ---- WHAT THE HEAD ACTUALLY PASSES OVER ----
        //
        // `at()` is the region statistic, smoothed. This is the same reel read
        // at the scale a flake is, and it is what the audio path wants.
        //
        // THE ARITHMETIC IS THE WHOLE IDEA. With the field uniform on [0, 1],
        // `clamp((u - s) / w)` has mean `1 - s - w/2` -- so setting
        // `s = (1 - mean) - w/2` makes the realised mean exactly the region's
        // `intact`, in closed form and with no lookup. The region keeps its
        // meaning, friction and fatigue keep reading the mean they read now,
        // and the persistence format does not move: only WHERE the damage sits
        // has changed.
        // The inverse of `X^5 - 3X^4 + 2.5X^3` on (0, 1], which is smootherstep's
        // mean over a band truncated at the top of the field. Monotone, so a
        // table and a lerp; and it is a pure function, so one table serves every
        // reel, every age and every region.
        [[nodiscard]] static double invertTruncatedMean(double target) noexcept
        {
            static const std::array<double, 65> table = []
            {
                std::array<double, 65> t{};
                for (std::size_t i = 0; i < t.size(); ++i)
                {
                    // Bisect: `g` is monotone increasing from 0 to 0.5.
                    const double want = 0.5 * static_cast<double>(i)
                                      / static_cast<double>(t.size() - 1);
                    double lo = 0.0, hi = 1.0;
                    for (int k = 0; k < 40; ++k)
                    {
                        const double m = 0.5 * (lo + hi);
                        const double g = m * m * m * (m * m - 3.0 * m + 2.5);
                        (g < want ? lo : hi) = m;
                    }
                    t[i] = 0.5 * (lo + hi);
                }
                return t;
            }();

            const double u = std::clamp(target, 0.0, 0.5) / 0.5
                           * static_cast<double>(table.size() - 1);
            const auto i = static_cast<std::size_t>(u);
            const std::size_t j = std::min(i + 1, table.size() - 1);
            const double f = u - static_cast<double>(i);
            return table[i] + (table[j] - table[i]) * f;
        }

        [[nodiscard]] double realiseIntact(double metres, double mean) const noexcept
        {
            if (striation_.empty())
                return mean;

            // THE BAND IS NOT A CONSTANT. IT IS THE REGION'S OWN STATE.
            //
            // The region already knows how much of its loss is BARE BACKING and
            // how much is a coating thinned over its whole area: everything
            // above the hydrolysis floor is thinning, and only what is below it
            // is area. `thicknessMetres` has split them that way since the floor
            // was built. The realisation was NOT splitting them -- it
            // thresholded `intact` directly -- so it cut holes where the physics
            // says the tape is merely thin.
            //
            // MEASURED, AND IT IS WHY THE RENDERS SOUNDED PATCHY. At 174 passes
            // on a 32-year reel every region sat between 0.30 and 0.35 against a
            // floor of 0.36, so 15% of the tape should have been bare and the
            // rest thinned to a third. The head read 59% of it as SILENCE and
            // 8% as untouched, because a fixed band of 0.25 puts everything
            // below the threshold at zero. Good spots and dead spots, which is
            // exactly what it sounded like.
            //
            // So solve for the band instead of choosing it. `s` is fixed by the
            // BARE fraction the region already implies, and `w` is then whatever
            // makes the realised mean come out at `intact`:
            //
            //   P(fully gone) = s = 1 - min(1, intact / floor)
            //   mean         = intact
            //
            // Two equations, two unknowns, both closed form.
            // THE REGION'S OWN FLOOR, not the reel's. How much of a region's
            // loss is bare backing depends on how deep the water got THERE, and
            // that varies with the binder (`spallFloorFor`).
            const double area = areaFraction(mean, regionAt(metres));
            double s = std::clamp(1.0 - area, 0.0, 1.0);
            const double span = 1.0 - s;

            // Above the half-thickness point the band closes inside [0, 1] and
            // the mean is `1 - s - w/2`; below it the band runs off the top and
            // the mean is `(1 - s)^2 / 2w`. Which branch applies is decided by
            // the arithmetic, not by a flag.
            // BRANCH 1: the band closes inside [0, 1]. Smootherstep is
            // symmetric so its average across a whole band is a half, exactly
            // as a linear ramp's is, and `mean = 1 - s - w/2` is exact.
            //
            // BRANCH 2: the band runs off the top and is TRUNCATED, and there
            // the two shapes stop agreeing -- smootherstep sits below linear
            // over the first half, so the realised mean comes out LOW. Measured
            // at a region mean of 0.10 it read 0.054, which is the bookkeeping
            // and the head disagreeing by 5 dB.
            //
            // Integrating smootherstep over the truncated band gives
            //   mean / span = X^5 - 3X^4 + 2.5X^3,   X = span / w
            // which is monotone on (0, 1] and reaches a half at X = 1, so it
            // inverts cleanly and `w = span / X`. The curve depends on nothing
            // but itself, so the inverse is one static table for the process.
            double w = 2.0 * (span - mean);
            if (s + w > 1.0)
            {
                const double target = span > 0.0 ? mean / span : 0.0;
                const double x = invertTruncatedMean(target);
                w = x > 1.0e-6 ? span / x : 1.0e3;
            }

            // A FLOOR ON THE BAND, and it is the anti-click term: with no
            // thinning at all the physical band is zero, which is a hard
            // threshold and an edge one sample wide.
            //
            // WHEN IT BITES, THE THRESHOLD MOVES TO KEEP THE MEAN. Widening the
            // band without re-solving shifts the realised mean by w/2, which on
            // fresh tape read 0.860 where the region said 0.900 -- the
            // bookkeeping and the head disagreeing, which is the one thing this
            // whole scheme exists to prevent. The bare fraction is what gives
            // way instead, and it is the right thing to give: this branch only
            // runs when there was hardly any thinning to place.
            //
            // AND IT ONLY APPLIES WHERE THERE IS AN EDGE TO SOFTEN. The edge is
            // the rim of a BARE patch, which exists only when `s` is above zero;
            // a region with nothing bare has no rim, so there is nothing to
            // soften and the floor has no business acting.
            //
            // MEASURED, AND IT WAS COSTING NEW TAPE HALF A DECIBEL. Applied
            // unconditionally, a reel nobody had ever played realised at a mean
            // of 0.93 with dips to 0.02 -- because `s` is zero on fresh tape, so
            // the floor set `w` to 0.08 with the band straddling the bottom of
            // the field and took 0.04 of level for nothing. Pristine tape must
            // read 1.0, and now does.
            //
            // It was inaudible while the field's scale was eight times the
            // flake, because the modulation it produced sat at 24 Hz. With the
            // field on the flake it moves to about 190 Hz, into the band, which
            // is what made a pre-existing defect finally show: `flutter depth is
            // the speed error it says it is` reads a tone's zero crossings, and
            // amplitude modulation at 190 Hz puts sidebands on a 1 kHz tone.
            if (s > 0.0 && w < constants_.striationBand)
            {
                w = constants_.striationBand;
                s = std::max(0.0, 1.0 - mean - 0.5 * w);
            }
            if (w <= 0.0)
                return std::clamp(mean, 0.0, 1.0);   // nothing bare, nothing thinned

            // ACROSS THE WIDTH, NOT AT A POINT. One sample is one place on the
            // tape; the head reads a stripe. See `striationWidthSamples`.
            const int lanes = std::clamp(
                static_cast<int>(std::llround(constants_.trackWidthMetres
                                              / std::max(1.0e-9, constants_.flakeMetres))),
                1, 16);
            double acc = 0.0;
            for (int j = 0; j < lanes; ++j)
            {
                const double at = metres
                    + static_cast<double>(j) * constants_.striationWidthStrideMetres;
                double t = std::clamp((striationAt(at) - s) / w, 0.0, 1.0);
                // Smootherstep: no corner at either end of the band.
                acc += t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
            }
            return acc / static_cast<double>(lanes);
        }

        [[nodiscard]] State realisedAt(double metres) const noexcept
        {
            State s = at(metres);
            s.intact = realiseIntact(metres, s.intact);
            return s;
        }

        // The field itself, interpolated. Linear is right here and smoothstep
        // is not: the field is ALREADY smooth at its own resolution, and the
        // band is what shapes the edge.
        [[nodiscard]] double striationAt(double metres) const noexcept
        {
            const auto n = striation_.size();
            if (n == 0)
                return 0.5;
            const double x = circular_
                ? position(metres) / lengthMetres_ * static_cast<double>(n)
                : metres / constants_.striationMetres;
            double whole = std::floor(x);
            const double f = x - whole;
            auto i = static_cast<std::size_t>(
                ((static_cast<std::int64_t>(whole) % static_cast<std::int64_t>(n))
                 + static_cast<std::int64_t>(n)) % static_cast<std::int64_t>(n));
            const std::size_t j = (i + 1) % n;
            return static_cast<double>(striation_[i])
                 + (static_cast<double>(striation_[j])
                    - static_cast<double>(striation_[i])) * f;
        }

        [[nodiscard]] const std::vector<float>& striationMap() const noexcept
        {
            return striation_;
        }

        // ONE PASS OF TAPE ACROSS THE HEADS, from one position to another.
        //
        // Direction does not matter and speed does not appear: what wears tape
        // is contact over DISTANCE, so a rewind wears it and a slow pass wears
        // it the same as a fast one over the same stretch. Recording is not
        // charged extra either -- contact is contact, and the thinness is
        // charged exactly once, at read, through the loss chain.
        // ONE PASS, WITH THE DRAG THE MACHINE IS ADDING.
        //
        // ONE HEAD, ONE CALL. The span is absolute, so a caller with several
        // heads calls this once per head with the span shifted by that head's
        // offset and `contact` set to its share -- which is what puts wear
        // where a head actually touches rather than across the block's whole
        // sweep (`TapeDeck::engagedHeads`). Nothing here needs to know how many
        // there are.
        //
        // `drag` is what the HEAD contributes to friction and defaults to 1, a
        // clean one. Deposits on the face raise it: IASA TC-05 has tapes that
        // "squeal during replay due to friction because of sticky pigment and
        // binder particles deposited on tape guides and audio and video heads",
        // so the drag is the machine's and not the tape's -- which is why it
        // arrives as an argument rather than being read off a region.
        //
        // IT CLOSES A POSITIVE FEEDBACK LOOP and `ROADMAP.md` asked for the
        // ceiling first: dirty head, more friction, faster fatigue, more
        // flaking, more deposit. THE CEILING IS STRUCTURAL. Deposits saturate
        // (`TapeDeck::HeadDeposits::saturationMetres`), so the drag they can
        // produce saturates with them, and the loop's gain is bounded by
        // construction at every setting rather than by a clamp somebody has to
        // remember. There is no value of any constant that makes it run away.
        // `contact` is HOW MUCH OF THE HEAD STACK IS TOUCHING, as a fraction of
        // all of it. Wear is contact and a machine has three heads, so lifting
        // the record and erase heads and leaving the reproduce head down is a
        // third of the wear -- which is `TapeDeck`'s freeze, and is derived
        // rather than chosen. Lifting all of them is none of it: a tape running
        // past raised lifters is not being worn at all, which is the whole
        // reason a machine has them.
        void pass(double fromMetres, double toMetres, double drag = 1.0,
                  double contact = 1.0) noexcept
        {
            drag_ = std::max(1.0, drag);
            contact_ = std::clamp(contact, 0.0, 1.0);
            lastShed_ = 0.0;
            if (coating_.empty() || contact_ <= 0.0)
                return;
            // ---- DIRECTION IS THROWN AWAY, AND THAT IS A DECISION ----
            //
            // `abs` here and `min` below mean a pass backwards costs exactly
            // what the same pass forwards did. For TAPE wear that is close to
            // true: the head face is contoured and the oxide does not care
            // which way it slides.
            //
            // What would break it are three things this model does not have.
            // TENSION is the load term in any wear law and it is distributed
            // differently pulling from the supply side than from the takeup.
            // LEADING-EDGE pressure is real but wears the HEAD, and head wear is
            // not modelled at all. And DWELL -- a stopped transport holding tape
            // against a head under tension -- is direction-independent and
            // currently costs nothing, because this charges distance.
            //
            // So: symmetric, deliberately, and tension is the thing that would
            // end it (`ROADMAP.md`).
            const double distance = std::abs(toMetres - fromMetres);
            if (!(distance > 0.0))
                return;

            // WHOLE LAPS FIRST, so a shuttle that eats the reel several times
            // over in one block is not a rounding artefact. A loop machine
            // reaches this in normal running; a linear reel never does.
            double remaining = distance;
            const int laps = static_cast<int>(remaining / lengthMetres_);
            for (int lap = 0; lap < laps; ++lap)
                for (std::size_t i = 0; i < coating_.size(); ++i)
                    wearRegion(i, 1.0);
            remaining -= static_cast<double>(laps) * lengthMetres_;
            if (remaining <= 0.0)
                return;

            const double region = constants_.regionMetres;
            const auto regions = coating_.size();
            double x = position(std::min(fromMetres, toMetres));
            const double end = x + remaining;

            // WALK ON THE REGION INDEX, NOT ON THE POSITION. Recomputing the
            // next boundary from `x` looks equivalent and is not: at 10 m with
            // 10 mm regions, `(floor(x/r) + 1) * r` rounds to `x` itself and
            // the walk stops advancing. An integer index cannot stall, and it
            // is what makes the wrap a modulo rather than a special case.
            for (auto index = static_cast<std::size_t>(std::floor(x / region)); ; ++index)
            {
                const double edge = static_cast<double>(index + 1) * region;
                const double until = std::min(edge, end);
                // Passes, as a fraction of a region's length: a block that
                // covers a tenth of a region wears it a tenth of a pass. This
                // is why a shuttle wears tape twelve times as fast -- it passes
                // twelve times as much of it.
                wearRegion(index % regions, (until - x) / region);
                if (until >= end)
                    return;
                x = until;
            }
        }

        // ---------------------------------------------------------------
        // What the state does to the sound. No new DSP: every one of these
        // feeds a term `LossEffects` already has (ROADMAP.md).

        // A BROADBAND LEVEL, which the loss chain did not have because
        // thickness was a constant and level was absorbed into overall gain.
        // Less oxide is less flux, at every wavelength.
        //
        // AND THE NOISE FLOOR DOES NOT FOLLOW IT DOWN. That asymmetry is the
        // whole of what makes wear destroy information rather than turn the
        // volume down: at the limit there is hiss with nothing in it, and
        // turning the gain up gets you louder hiss.
        [[nodiscard]] static double outputScale(const State& s) noexcept
        {
            // BOTH LEVEL TERMS, and they arrive from different mechanisms: a
            // thinner coating carries less flux, and bare backing carries none.
            // Neither has a wavelength in it -- the wavelength losses read
            // `coating` through the head's own terms.
            return s.coating * s.intact;
        }

        // Depth of the MAGNETIC layer, which is what shrinks. The base film is
        // polyester and sheds nothing, so a worn tape is a thinner coating on a
        // substrate of unchanged gauge -- and because `thicknessLoss` is
        // normalised, that makes the tape relatively BRIGHTER. The dulling is
        // entirely the spacing term below, which is as it should be: a worn
        // tape is quiet and rough, not filtered.
        // SPALLATION THINS IT TOO, and that is the only route to the sign
        // change dlp 1.1 shows. Where the hydrolysed layer has lifted but the
        // sound binder underneath has not, the coating is THINNER over its full
        // area rather than absent over part of it -- so the level falls (that
        // is `outputScale`) while the top end, which only ever reads the
        // surface, falls LESS. A positive differential.
        //
        // Below the floor there is no hydrolysed layer left, further loss goes
        // through sound binder and takes the full depth with it, and the damage
        // is AREA: broadband, no differential at all. The two regimes come out
        // of one number because `outputScale` is `coating * intact` either way:
        //
        //   above the floor   area = 1,        thickness = intact
        //   below the floor   area = intact/F, thickness = F
        //
        // and both products are `intact`. So the level is untouched by this
        // split and only the SHAPE moves, which is what makes it testable.
        [[nodiscard]] double thicknessMetres(const State& s, double newThickness) const noexcept
        {
            return newThickness * s.coating * thicknessFraction(s.intact);
        }

        // The two halves of a region's spallation, which multiply to `intact`.
        [[nodiscard]] double thicknessFraction(double intact) const noexcept
        {
            const double floorFrac = spallFloor();
            if (intact >= floorFrac || floorFrac <= 0.0)
                return std::max(intact, floorFrac);
            return floorFrac * std::pow(intact / floorFrac,
                                        constants_.soundThinningShare);
        }

        // WHICH REGION A PLACE ON THE TAPE IS IN. The same arithmetic `at`
        // uses, without the interpolation, because a floor is a property of the
        // binder there rather than a quantity to blend.
        [[nodiscard]] std::size_t regionAt(double metres) const noexcept
        {
            if (coating_.empty())
                return 0;
            const auto count = coating_.size();
            const double x = position(metres) / constants_.regionMetres;
            const double wrapped = circular_
                ? (x < 0.0 ? x + static_cast<double>(count) : x) : x;
            const auto i = static_cast<std::size_t>(std::max(0.0, wrapped));
            return circular_ ? (i % count) : std::min(i, count - 1);
        }

        [[nodiscard]] double areaFraction(double intact,
                                          std::size_t region) const noexcept
        {
            const double floorFrac = spallFloorFor(region);
            if (intact >= floorFrac || floorFrac <= 0.0)
                return 1.0;
            return std::pow(intact / floorFrac,
                            1.0 - constants_.soundThinningShare);
        }

        [[nodiscard]] double areaFraction(double intact) const noexcept
        {
            const double floorFrac = spallFloor();
            if (intact >= floorFrac || floorFrac <= 0.0)
                return 1.0;
            return std::pow(intact / floorFrac,
                            1.0 - constants_.soundThinningShare);
        }

        // The fraction of the coating hydrolysis did NOT reach, so the depth
        // spallation arrests at. 1 on a fresh reel: nothing has been wetted, so
        // nothing lifts as a layer and what spalling there is takes full depth.
        [[nodiscard]] double spallFloor() const noexcept
        {
            return floorFromDepth(1.0);
        }

        // THE SAME FLOOR, FOR ONE REGION, deeper where the binder is weaker.
        // `grain_` is low where the tape sheds fastest (`shedScale`), so the
        // same exponent that makes a weak region shed faster makes the water
        // reach further into it.
        [[nodiscard]] double spallFloorFor(std::size_t region) const noexcept
        {
            if (region >= grain_.size() || constants_.hydrolysisDepthRatio <= 1.0)
                return spallFloor();
            return floorFromDepth(std::pow(constants_.hydrolysisDepthRatio,
                                           grainMean_
                                               - static_cast<double>(grain_[region])));
        }

    private:
        [[nodiscard]] double floorFromDepth(double scale) const noexcept
        {
            return std::clamp(1.0 - constants_.hydrolysisDepth * scale
                                  * std::pow(binderAge_,
                                             constants_.hydrolysisDepthPower),
                              constants_.coatingFloor, 1.0);
        }

    public:

        // Head to oxide. Two additions, and they are different claims: pitting
        // where material has gone, and swelling where the binder has
        // hydrolysed but nothing has yet.
        [[nodiscard]] double spacingMetres(const State& s,
                                           double newSpacing,
                                           double newThickness,
                                           double binderAge) const noexcept
        {
            return newSpacing
                 + constants_.pitFraction * newThickness * (1.0 - s.coating)
                 + constants_.binderRoughFraction * newThickness
                       * std::clamp(binderAge, 0.0, 1.0);
        }

        // The reel's own age. The overload above takes it explicitly because
        // the filter designer runs off the audio thread and must work from the
        // key it was handed rather than from whatever the member says by the
        // time it gets there.
        [[nodiscard]] double spacingMetres(const State& s,
                                           double newSpacing,
                                           double newThickness) const noexcept
        {
            return spacingMetres(s, newSpacing, newThickness, binderAge_);
        }

        // Friction, as a multiplier on scrape flutter (SOURCES section 19 and
        // section 23) and on the shed rate. THIS IS WHAT SQUEAL IS at the
        // output: stick-slip modulates speed, which puts sidebands either side
        // of everything on the tape. It is heard as roughness and a smeared
        // top, not as a whistle -- the whistle is in the room, and a plugin has
        // no room.
        [[nodiscard]] double frictionScale(const State& s) const noexcept
        {
            // THREE THINGS DRAG, and the third is the one that makes abrasion
            // matter at all. Losing the lubricant and hydrolysing the binder
            // both raise friction; so does the SURFACE ROUGHENING abrasion
            // leaves behind, and that is how a thinning nobody can hear reaches
            // the mechanism that can be heard.
            // AND THE STICKY ONE GOES WHEN THE STICKY LAYER DOES. Sticky-shed
            // is the HYDROLYSED SURFACE dragging on the head; once that layer
            // has lifted, what the head runs on is sound binder, which is not
            // sticky. So this term is charged in proportion to how much of the
            // layer is still there rather than to the reel's age forever.
            //
            // MEASURED. Without it an aged reel kept the full sticky friction
            // after shedding, fatigued its sound binder at four times a fresh
            // reel's rate and was bare by pass 576 -- where dlp 1.1 is 11.5 dB
            // down at 409 and still playing.
            const double floor = spallFloor();
            const double layer = floor < 1.0
                ? std::clamp((s.intact - floor) / (1.0 - floor), 0.0, 1.0) : 0.0;
            const double worn = 1.0 - s.coating * s.intact;
            return 1.0 + (constants_.dryFriction - 1.0) * (1.0 - s.lubricant)
                       + (constants_.dryFriction - 1.0) * binderAge_ * layer
                       + (constants_.roughFriction - 1.0) * worn;
        }

        // ---------------------------------------------------------------
        // Persistence. See `serialise` and `deserialise` below, which are what
        // callers should use; these two are for tests and for anything that
        // wants the maps in place.
        [[nodiscard]] const std::vector<float>& intactMap() const noexcept { return intact_; }
        [[nodiscard]] std::vector<float>& intactMap() noexcept { return intact_; }
        [[nodiscard]] const std::vector<float>& coatingMap() const noexcept { return coating_; }
        [[nodiscard]] const std::vector<float>& lubricantMap() const noexcept { return lubricant_; }
        [[nodiscard]] std::vector<float>& coatingMap() noexcept { return coating_; }
        [[nodiscard]] std::vector<float>& lubricantMap() noexcept { return lubricant_; }

    private:
        [[nodiscard]] static double lerp(float a, float b, double f) noexcept
        {
            return static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * f;
        }

        // Tape is finite and a loop is not: both are handled by wrapping, since
        // a linear reel's transport never leaves its length in the first place.
        [[nodiscard]] double position(double metres) const noexcept
        {
            if (lengthMetres_ <= 0.0)
                return 0.0;
            double x = std::fmod(metres, lengthMetres_);
            if (x < 0.0)
                x += lengthMetres_;
            return x;
        }

        void wearRegion(std::size_t i, double passes) noexcept
        {
            // SCALED BY HOW MUCH OF THE STACK IS TOUCHING. Every term below is
            // charged per contact -- oxide shed, lubricant spent, binder
            // fatigued -- so one factor covers all three, which is the point of
            // putting it here rather than at each of them.
            passes *= contact_;
            if (passes <= 0.0)
                return;
            const double c = coating_[i];
            const double l = lubricant_[i];

            // The rate a region sees is its own condition times the reel's.
            // Nothing here reads the MACHINE's age: hydrolysis belongs to the
            // tape, and wear is caused by passes and scaled by condition.
            const double binderFactor = std::pow(constants_.binderCollapse, binderAge_);
            const double worn = 1.0 + constants_.wornSheds * (1.0 - c);
            const double dry = 1.0 + (constants_.dryFriction - 1.0) * (1.0 - l);

            // AND THE TAPE'S OWN GRAIN, which is what makes a loop pull apart
            // along a pattern instead of fading evenly.
            const double shed = shedPerPass(constants_) * passes * worn * dry
                              * binderFactor * shedScale(i);
            coating_[i] = static_cast<float>(
                std::max(constants_.coatingFloor, c - shed));

            const double dried = passes * binderFactor / std::max(1.0, constants_.passesToDry);
            lubricant_[i] = static_cast<float>(std::max(0.0, l - dried));

            // WHAT THIS REGION HANDED OVER, kept so the caller can charge it to
            // the head that caused it. AREA, not thickness: abrasion is
            // burnishing and hands the head nothing, spallation is a flake
            // leaving the tape and has to go somewhere (`SOURCES §30`), which is
            // why this brackets `spall` and not the line above it.
            const double intactBefore = intact_[i];
            spall(i, passes,
                  drag_ * frictionScale({ coating_[i], intact_[i], lubricant_[i] }));
            lastShed_ += (intactBefore - static_cast<double>(intact_[i]))
                       / static_cast<double>(coating_.size());
        }

        // WHAT THE BINDER CAN STILL TAKE, in passes. Hydrolysis eats it as a
        // power, and the grain decides which region gives way first -- a weak
        // place is weak in both mechanisms, because it is one fact about the
        // coating.
        // HOW MUCH OF THE HYDROLYSED LAYER A REGION HAS LEFT, as a fraction of
        // the layer's own depth. Zero means the layer has lifted and the head
        // is over sound binder.
        //
        // THE EPSILON IS LOAD-BEARING AND IT IS NOT A FUDGE. `intact_` is
        // float and the floor is double, so a region clamped exactly to the
        // floor reads back a few parts in ten million ABOVE it. That is enough
        // to keep it in the hydrolysed branch, where its capacity is near zero
        // and it therefore spalls on every single pass -- and is clamped
        // straight back to the floor by the line that put it there. The reel
        // sat at the floor for two thousand passes looking beautifully arrested
        // while actually spalling a hundred and twenty thousand times.
        static constexpr double kLayerEpsilon = 1.0e-5;

        [[nodiscard]] double layerLeft(std::size_t i) const noexcept
        {
            const double floor = spallFloorFor(i);
            const double span  = std::max(1.0e-6, 1.0 - floor);
            const double above = (static_cast<double>(intact_[i]) - floor) / span;
            return above > kLayerEpsilon ? above : 0.0;
        }

        // ONE FLAKE OUT OF A REGION'S WORTH, and both lengths are constants
        // that already had to exist. Clamped to (0, 1]: a region can never be
        // smaller than a flake in practice, but `prepare` takes a requested
        // region size from outside and this must stay a fraction whatever it is
        // handed.
        [[nodiscard]] double spallChunkFraction() const noexcept
        {
            return std::clamp(constants_.flakeMetres
                              / std::max(1.0e-9, constants_.regionMetres),
                              1.0e-4, 1.0);
        }

        [[nodiscard]] double spallCapacity(std::size_t i) const noexcept
        {
            const double grain = (i < grain_.size())
                               ? 0.5 + static_cast<double>(grain_[i]) : 1.0;

            // TWO REGIMES, AND THE FLOOR IS THE BOUNDARY. Above it the binder
            // is hydrolysed, weak, and getting weaker as its own edges lift --
            // that is the fast, autocatalytic shedding. Below it the layer has
            // gone and what remains is sound binder, which fatigues at the rate
            // a fresh reel does.
            //
            // THIS IS WHAT BOUNDS THE RUNAWAY. Before it, `spallDamagePower`
            // drove every region that started to the floor of the array, so
            // every reel ended bare; now the acceleration is measured against
            // the hydrolysed layer's OWN depth and runs out when that does.
            const double floor = spallFloorFor(i);
            const double above = layerLeft(i);

            // What is left to hold: a region that has already lost material
            // has less binder anchoring the rest, and fewer edges to hold it
            // down. It applies in BOTH regimes -- sound binder loses anchorage
            // when a chunk goes just as hydrolysed binder does -- but it is
            // measured against whatever is being lost, or a shallow layer would
            // never accelerate and a fresh reel would never accelerate at all.
            //
            // ON A FRESH REEL THE FLOOR IS 1 AND THIS REDUCES EXACTLY TO WHAT
            // IT WAS: `left` is one, `held` is the intact area to the power,
            // and the 750-pass life that studio practice agrees with is
            // untouched. Getting that wrong switched the feedback off for new
            // tape and halved the number of regions that ever flaked.
            const double left = above > 0.0
                ? std::pow(std::clamp(1.0 - binderAge_, 0.0, 1.0),
                           constants_.spallBinderPower)
                : 1.0;
            const double remaining = above > 0.0
                ? above
                : static_cast<double>(intact_[i]) / std::max(1.0e-6, floor);
            const double held = std::pow(std::max(constants_.coatingFloor, remaining),
                                         constants_.spallDamagePower);
            return constants_.spallCapacityPasses * left * grain * held;
        }

        void spall(std::size_t i, double passes, double friction) noexcept
        {
            if (constants_.spallCapacityPasses <= 0.0 || fatigue_.empty())
                return;

            // Stress accumulates with contact, and friction is what applies it:
            // a dry or sticky tape fatigues its binder faster than a lubricated
            // one, which is the same coupling that drives scrape flutter.
            fatigue_[i] = static_cast<float>(fatigue_[i] + passes * friction);

            // A capacity of zero is a binder with nothing left, which spalls
            // on contact rather than never. Returning early on it made a fully
            // hydrolysed reel the ONE age that did not come apart.
            const double capacity = spallCapacity(i);
            if (capacity > 0.0 && static_cast<double>(fatigue_[i]) < capacity)
                return;

            // IT LET GO. A chunk of the AREA, not a shaving off the thickness:
            // a flake takes its coating away completely and leaves bare
            // backing, which is a broadband loss rather than a wavelength one.
            // A HYDROLYTIC EVENT CANNOT TAKE MORE THAN WAS HYDROLYSED. The
            // layer lifts to the depth the water reached and stops at sound
            // binder, so the floor is a hard stop for as long as there is
            // still a layer to lift. Without this clamp the very first event
            // took 55% of the coating and went straight THROUGH a floor at
            // 55%, which put every aged reel into the sound-binder regime on
            // pass one and made the floor do nothing at all.
            const double a = intact_[i];
            const double floor = spallFloorFor(i);
            const double taken = a * (1.0 - spallChunkFraction());
            intact_[i] = static_cast<float>(
                std::max(constants_.coatingFloor,
                         layerLeft(i) > 0.0 ? std::max(floor, taken) : taken));

            // Spent, and starting again from a weaker surface.
            fatigue_[i] = 0.0f;

            // AND THE EDGES IT LEFT ARE WHERE THE NEXT ONE LIFTS.
            const auto n = fatigue_.size();
            for (std::size_t j : { (i + 1) % n, (i + n - 1) % n })
                fatigue_[j] = static_cast<float>(
                    fatigue_[j] + constants_.spallSpread * spallCapacity(j));
        }

        // ---- THE GRAIN, SYNTHESISED FROM INTEGERS ----
        //
        // Deterministic and reproducible on any machine (`PRINCIPLES §5`): a
        // 64-bit integer mixer over (seed, octave, lattice index), interpolated
        // with a smoothstep, summed over octaves at 1/f amplitudes.
        //
        // PERIODIC BY CONSTRUCTION. Each octave's lattice index is taken modulo
        // that octave's cell count, so the field wraps -- which matters because
        // a loop's ends are adjacent tape and a grain with a seam would put a
        // discontinuity there once per lap, which is the defect the map's own
        // interpolation was just fixed for.
        [[nodiscard]] static std::uint64_t mix(std::uint64_t x) noexcept
        {
            x += 0x9e3779b97f4a7c15ull;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
            return x ^ (x >> 31);
        }

        [[nodiscard]] double lattice(int octave, std::int64_t index,
                                     std::int64_t cells) const noexcept
        {
            const auto wrapped = static_cast<std::uint64_t>(
                ((index % cells) + cells) % cells);
            const auto h = mix(reelSeed_ ^ mix(static_cast<std::uint64_t>(octave) * 0x51ull
                                               + wrapped * 0x9e37ull));
            // The top 53 bits, so the value does not depend on the low bits a
            // multiplier leaves least mixed.
            return static_cast<double>(h >> 11) / 9007199254740992.0;
        }

        // Gaussian in log-scale: full weight at the flake size, falling away
        // either side. Octave `o` has a lattice spacing of `span / 2^o`.
        // THE SCALE FRACTURE PICKS, which is the flake's own size and not a
        // number of its own. See `flakeMetres`.
        [[nodiscard]] double striationScale() const noexcept
        {
            return std::max(1.0e-9, constants_.flakeMetres);
        }

        [[nodiscard]] double octaveWeight(int o, double span) const noexcept
        {
            const double scale = span / static_cast<double>(std::int64_t(1) << o);
            const double octavesAway = std::log2(scale
                / std::max(1.0e-9, striationScale()));
            const double sigma = std::max(0.25, constants_.striationOctaves);
            const double z = octavesAway / sigma;
            return std::exp(-0.5 * z * z);
        }

        void buildStriation()
        {
            striation_.clear();
            // `striationMetres` is resolved in `prepare` and is positive by the
            // time this runs; `striationBand` is the switch.
            if (constants_.striationMetres <= 0.0 || constants_.striationBand <= 0.0)
                return;

            const double res = constants_.striationMetres;
            const auto cap = static_cast<std::size_t>(
                std::max(64.0, constants_.striationMaxMetres / res));
            // A LOOP'S FIELD MUST CLOSE ON THE LOOP. If it does not, the seam
            // carries a discontinuity that arrives once per lap -- exactly the
            // periodic edge this field exists to get away from.
            const auto cells = circular_
                ? std::clamp<std::size_t>(
                      static_cast<std::size_t>(std::llround(lengthMetres_ / res)),
                      64, cap)
                : cap;
            striation_.assign(cells, 0.0f);

            const double finest = std::max(res, constants_.striationFinestMetres);
            const double span = static_cast<double>(cells) * res;
            (void) finest;
            const int octaves = std::clamp(
                static_cast<int>(std::ceil(std::log2(
                    std::max(2.0, span / finest)))), 1, 16);

            // HOISTED, because it depends on the OCTAVE and not on the cell.
            // Left in the inner loop it was a `log2` and an `exp` per cell per
            // octave -- a million transcendentals to compute sixteen numbers,
            // and it put 145 ms on every `prepare`.
            std::array<double, 17> weight{};
            for (int o = 0; o < octaves && o < 17; ++o)
                weight[static_cast<std::size_t>(o)] = octaveWeight(o, span);

            for (std::size_t i = 0; i < cells; ++i)
            {
                double acc = 0.0;
                for (int o = 0; o < octaves; ++o)
                {
                    const std::int64_t lattices = std::int64_t(1) << o;
                    const double u = static_cast<double>(i)
                                   * static_cast<double>(lattices)
                                   / static_cast<double>(cells);
                    const auto cell = static_cast<std::int64_t>(std::floor(u));
                    const double f = u - static_cast<double>(cell);
                    const double smooth = f * f * (3.0 - 2.0 * f);
                    // Offset the octave index off the grain's, so a weak place
                    // in the coating and a striation are independent facts.
                    const double a = lattice(o + 32, cell, lattices);
                    const double b = lattice(o + 32, cell + 1, lattices);
                    acc += weight[static_cast<std::size_t>(o)]
                         * (a + (b - a) * smooth);
                }
                striation_[i] = static_cast<float>(acc);
            }

            // UNIFORM BY CONSTRUCTION, and this is what makes the threshold
            // mean what it says. A sum of octaves is roughly Gaussian, and
            // thresholding a Gaussian at `s` does NOT remove a fraction `s` --
            // so the region's bookkeeping and what the head actually hears
            // would drift apart with no way to see it. Ranking is a monotone
            // transform, so it flattens the histogram exactly while leaving
            // the field's spatial continuity untouched.
            std::vector<std::uint32_t> order(cells);
            for (std::size_t i = 0; i < cells; ++i)
                order[i] = static_cast<std::uint32_t>(i);
            std::sort(order.begin(), order.end(),
                      [this](std::uint32_t a, std::uint32_t b)
                      {
                          if (striation_[a] != striation_[b])
                              return striation_[a] < striation_[b];
                          return a < b;          // total order, so it is stable
                      });
            const double last = static_cast<double>(cells - 1);
            for (std::size_t r = 0; r < cells; ++r)
                striation_[order[r]] =
                    static_cast<float>(static_cast<double>(r) / last);
        }

        void buildGrain(std::size_t regions)
        {
            grain_.assign(regions, 0.5f);
            if (regions == 0 || constants_.grainDepth <= 0.0)
                return;

            // From the whole tape down to `grainFinestMetres`, so the grain has
            // the same character on a loop and on a reel.
            const double finest = std::max(constants_.regionMetres,
                                           constants_.grainFinestMetres);
            const int octaves = std::clamp(
                static_cast<int>(std::ceil(std::log2(
                    std::max(2.0, lengthMetres_ / finest)))), 1, 12);

            for (std::size_t i = 0; i < regions; ++i)
            {
                double acc = 0.0;
                for (int o = 0; o < octaves; ++o)
                {
                    const std::int64_t cells = std::int64_t(1) << o;
                    const double u = static_cast<double>(i)
                                   * static_cast<double>(cells)
                                   / static_cast<double>(regions);
                    const auto cell = static_cast<std::int64_t>(std::floor(u));
                    const double f = u - static_cast<double>(cell);
                    const double smooth = f * f * (3.0 - 2.0 * f);
                    const double a = lattice(o, cell, cells);
                    const double b = lattice(o, cell + 1, cells);
                    acc += std::pow(0.5, o) * (a + (b - a) * smooth);
                }
                grain_[i] = static_cast<float>(acc);
            }

            // NORMALISED TO ITS OWN RANGE, so `grainDepth` means what it says.
            // Dividing by the sum of the octave amplitudes is the obvious move
            // and is wrong by a lot: independent octaves average toward the
            // middle, so a five-octave sum spans a few per cent of [0, 1] and a
            // 6% grain depth became 0.6%. Measured before this line existed,
            // fresh tape ran 0.9665 to 0.9724 -- a spread of six thousandths
            // where six hundredths was asked for.
            const auto lo = *std::min_element(grain_.begin(), grain_.end());
            const auto hi = *std::max_element(grain_.begin(), grain_.end());
            const double span = static_cast<double>(hi) - static_cast<double>(lo);
            if (span > 1.0e-9)
                for (auto& g : grain_)
                    g = static_cast<float>((static_cast<double>(g)
                                            - static_cast<double>(lo)) / span);
            else
                std::fill(grain_.begin(), grain_.end(), 0.5f);

            double acc = 0.0;
            for (auto g : grain_)
                acc += g;
            grainMean_ = acc / static_cast<double>(grain_.size());
        }

        WearConstants constants_{};
        std::vector<float> grain_;

        // The striation field, uniform on [0, 1], addressed in SPACE. Periodic
        // on the loop for a circular medium and tiled on a reel.
        std::vector<float> striation_;
        double grainMean_ = 0.5;
        std::uint64_t reelSeed_ = 0;
        bool circular_ = false;
        std::vector<float> coating_;
        // The intact AREA fraction, which spallation reduces. See `State`.
        std::vector<float> intact_;
        std::vector<float> lubricant_;
        // Accumulated binder stress, per region. Not serialised: it is a
        // fraction of a capacity and rebuilds within a few passes, where the
        // coating it produces is the thing that has to survive a save.
        std::vector<float> fatigue_;
        double lengthMetres_ = 0.0;
        double binderAge_ = 0.0;

        // What the HEAD is adding to friction this pass. One, a clean head,
        // until a caller says otherwise; see `pass`.
        // The mean intact area the LAST `pass` took off, so a caller with
        // several heads can charge each head the dirt IT picked up rather than
        // splitting the reel's total between them. Over a lap they are the
        // same; rocking the transport is where they are not.
        double lastShed_ = 0.0;
        double drag_ = 1.0;

        // How much of the head stack is on the tape this pass; see `pass`.
        double contact_ = 1.0;
    };

    // ---------------------------------------------------------------------
    // A REEL'S CONDITION, AS BYTES.
    //
    // Deliberately a blob and not a file format. ROADMAP.md wants this in a
    // custom RIFF chunk beside `data`, so a reel opens and plays in any editor
    // with its state riding along invisibly -- but there is no reel file yet,
    // and the plugin has to save its wear NOW or a session's tape damage is
    // lost when the project closes. The encoding is the durable half and the
    // container is not, so the encoding is what gets built: the same bytes go
    // in a plugin's state today and in the chunk when there is one.
    //
    // DETERMINISTIC BY CONSTRUCTION (`PRINCIPLES §5`). Fixed little-endian
    // integers throughout, including the doubles, which are carried as scaled
    // integers rather than as a memcpy of an IEEE bit pattern -- a reel must
    // open identically on any computer, and "identically" includes the last
    // bit.
    //
    // Sixteen bits a region, not thirty-two. One pass of a fresh reel removes
    // about 9e-4 of the coating and a 16-bit step is 1.5e-5, so the
    // quantisation is a sixtieth of the smallest thing that can happen; a
    // half-hour Capstan reel is 274 KB against about 7 GB of audio.
    namespace wear
    {
        inline constexpr std::uint32_t kMagic = 0x52574d52;   // "RMWR", little-endian
        // 2 added the reel seed, without which a reel's future wear cannot be
        // reproduced. 3 added the INTACT AREA, which spallation reduces and
        // which is a different quantity from the coating's thickness.
        inline constexpr std::uint32_t kVersion = 3;

        // Millimetres and parts-per-million, so lengths and fractions are
        // integers before they are bytes.
        inline constexpr double kLengthScale = 1.0e6;   // metres -> micrometres
        inline constexpr double kUnitScale = 65535.0;

        inline void putU32(std::vector<unsigned char>& out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
        }

        inline void putU64(std::vector<unsigned char>& out, std::uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
        }

        inline void putU16(std::vector<unsigned char>& out, std::uint16_t v)
        {
            out.push_back(static_cast<unsigned char>(v & 0xffu));
            out.push_back(static_cast<unsigned char>((v >> 8) & 0xffu));
        }

        [[nodiscard]] inline std::uint32_t getU32(const unsigned char* p) noexcept
        {
            std::uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
                v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
            return v;
        }

        [[nodiscard]] inline std::uint64_t getU64(const unsigned char* p) noexcept
        {
            std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
            return v;
        }

        [[nodiscard]] inline std::uint16_t getU16(const unsigned char* p) noexcept
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        [[nodiscard]] inline std::uint16_t quantise(float v) noexcept
        {
            const double clamped = std::clamp(static_cast<double>(v), 0.0, 1.0);
            return static_cast<std::uint16_t>(std::lround(clamped * kUnitScale));
        }

        [[nodiscard]] inline float dequantise(std::uint16_t v) noexcept
        {
            return static_cast<float>(static_cast<double>(v) / kUnitScale);
        }
    }

    // Header, then the coating map, then the lubricant map.
    [[nodiscard]] inline std::vector<unsigned char> serialise(const WearMap& map)
    {
        std::vector<unsigned char> out;
        const auto& coating = map.coatingMap();
        const auto& lubricant = map.lubricantMap();
        out.reserve(40 + 6 * coating.size());

        wear::putU32(out, wear::kMagic);
        wear::putU32(out, wear::kVersion);
        wear::putU32(out, static_cast<std::uint32_t>(coating.size()));
        // The GEOMETRY the map was written on, so a reel that comes back onto a
        // different grid can be put onto it (see `deserialise`).
        wear::putU64(out, static_cast<std::uint64_t>(
            std::llround(map.lengthMetres() * wear::kLengthScale)));
        wear::putU64(out, static_cast<std::uint64_t>(
            std::llround(map.constants().regionMetres * wear::kLengthScale)));
        wear::putU16(out, wear::quantise(static_cast<float>(map.binderAge())));
        wear::putU64(out, map.reelSeed());

        for (auto c : coating)
            wear::putU16(out, wear::quantise(c));
        for (auto a : map.intactMap())
            wear::putU16(out, wear::quantise(a));
        for (auto l : lubricant)
            wear::putU16(out, wear::quantise(l));
        return out;
    }

    // Reads a blob back onto a map that has ALREADY been prepared, because the
    // machine decides the geometry and the file does not.
    //
    // THE GRIDS DO NOT HAVE TO MATCH, and this is where the maps being in
    // METRES pays for itself. A reel saved at one region size, or on a longer
    // or shorter tape, is resampled onto the grid it is being loaded into --
    // the same interpolation in space that `at()` does, because it is the same
    // question. Tape beyond what was saved comes back FRESH, which is the
    // honest reading of loading a longer reel: the extra tape is new.
    //
    // Returns false and touches nothing on a blob that is not one of ours.
    [[nodiscard]] inline bool deserialise(WearMap& map,
                                          const unsigned char* data,
                                          std::size_t bytes)
    {
        constexpr std::size_t kHeader = 4 + 4 + 4 + 8 + 8 + 2 + 8;
        if (data == nullptr || bytes < kHeader)
            return false;
        if (wear::getU32(data) != wear::kMagic)
            return false;
        if (wear::getU32(data + 4) != wear::kVersion)
            return false;

        const auto regions = static_cast<std::size_t>(wear::getU32(data + 8));
        if (regions == 0 || bytes < kHeader + 6 * regions)
            return false;

        const double storedLength =
            static_cast<double>(wear::getU64(data + 12)) / wear::kLengthScale;
        const double storedRegion =
            static_cast<double>(wear::getU64(data + 20)) / wear::kLengthScale;
        if (! (storedRegion > 0.0))
            return false;

        map.setBinderAge(wear::dequantise(wear::getU16(data + 28)));
        map.setReelSeed(wear::getU64(data + 30));

        const unsigned char* coating = data + kHeader;
        const unsigned char* intact = coating + 2 * regions;
        const unsigned char* lubricant = intact + 2 * regions;

        // Region CENTRES, in metres, in both grids -- which is the only frame
        // the two have in common.
        const double region = map.constants().regionMetres;
        auto& outCoating = map.coatingMap();
        auto& outIntact = map.intactMap();
        auto& outLubricant = map.lubricantMap();

        for (std::size_t i = 0; i < outCoating.size(); ++i)
        {
            const double metres = (static_cast<double>(i) + 0.5) * region;
            if (metres > storedLength)
            {
                outCoating[i] = 1.0f;
                outIntact[i] = 1.0f;
                outLubricant[i] = 1.0f;
                continue;
            }
            const double x = std::clamp(metres / storedRegion - 0.5,
                                        0.0, static_cast<double>(regions - 1));
            const auto a = static_cast<std::size_t>(x);
            const auto b = std::min(a + 1, regions - 1);
            const double f = x - static_cast<double>(a);
            const auto mix = [f](float lo, float hi)
            {
                return static_cast<float>(lo + (hi - lo) * f);
            };
            outCoating[i] = mix(wear::dequantise(wear::getU16(coating + 2 * a)),
                                wear::dequantise(wear::getU16(coating + 2 * b)));
            outIntact[i] = mix(wear::dequantise(wear::getU16(intact + 2 * a)),
                               wear::dequantise(wear::getU16(intact + 2 * b)));
            outLubricant[i] = mix(wear::dequantise(wear::getU16(lubricant + 2 * a)),
                                  wear::dequantise(wear::getU16(lubricant + 2 * b)));
        }
        return true;
    }
}

