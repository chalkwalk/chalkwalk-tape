# chalkwalk-tape

A tape deck as a library: medium, heads, layers, seams, resampling, and
the physics of what a reproduce head does not recover.
JUCE-free, C++17, MIT.

Not a delay line and not a looper — the parts you would build either from.
Audio crosses the boundary as pointer views over caller-owned channels,
transport arrives as a POD snapshot, and the medium is non-owning, so this
cannot reach for a file, a message thread or an allocator on the audio thread
even by accident. The seam is enforced by the dependency list rather than by
discipline.

| | |
|---|---|
| `Medium.h` | The tape: geometry, f32/i16 depth behind one accessor, lazy commit |
| `Heads.h` | Read and write heads doing index arithmetic over the medium |
| `EraseHead.h` | Erase as a first-class head, not a write of zeroes |
| `LayerStack.h` | Layers, and which one you actually hear at a given point |
| `Seam.h` | The loop join, and what happens to a note that crosses it |
| `MarkerLane.h` | Positions on the tape that mean something |
| `Resampler.h` | Polyphase windowed sinc, sized by rate — **including the scatter write** |
| `ChannelView.h` | Non-owning view over caller storage |

## The physics

What a tape machine does that a delay line does not. Each of these is a
function of **wavelength** or of **wear**, not of a parameter somebody tuned.

| | |
|---|---|
| `Hysteresis.h` | Jiles-Atherton magnetisation — the record side, and permanent |
| `HysteresisBatch.h` | The same, vectorised across tracks |
| `Bias.h` | The AC bias that makes the medium linear, and what happens when it is wrong |
| `Transport.h` | Wow, flutter, scrape and motor ripple, from reel and capstan geometry |
| `WearMap.h` | What passing tape does to a head, and what a drawer does to a reel |
| `TapeNoise.h` | Modulation noise and the particulate floor |
| `Compander.h` | dbx-style 2:1 companding, the two halves on opposite sides of the medium |
| `SlidingBand.h` | A sliding-band companding system in the dual-path topology |
| `PowerSupply.h` | Mains hum and rail sag — one supply, every channel |

`PowerSupply` is the borderline one and is here rather than in chalkwalk-dsp
because its figures come from tape-machine service literature and its consumers
are machines. It is the first thing to move down if something that is not a
tape machine wants a sagging rail.

## The reproduce-side losses

A head does not read back what was written. What it loses is not a taste
decision and not a filter somebody voiced — it is four closed forms, each from
a published derivation, each a function of **wavelength** rather than of
frequency, which is why halving the tape speed and halving the frequency give
exactly the same answer.

| | |
|---|---|
| `LossEffects.h` | Spacing, thickness, gap and azimuth loss, and the response they compose into |
| `HeadLengthLoss.h` | Duinker and Geurst's Table II: the low-frequency contour ripple a finite head length causes |
| `TapeEq.h` | The record and reproduce standards (NAB, IEC/CCIR, and the cassette curves) |
| `MinimumPhase.h` | A minimum-phase FIR from a magnitude, by cepstrum |
| `FirDesign.h` | Windowed-sinc design |
| `Interpolator.h` | Band-limited upsampling for an oversampled write path |
| `DesignCache.h` | Filter designs are pure functions of geometry, speed and rate; this remembers them |
| `Simd.h` | The one portability shim the filters need |

These arrived from Remanence, which is where they were derived, specified
(`docs/references/` there) and tested. Their suite moved with them and passes
unchanged here — inside a repository that knows nothing about tape machines,
which is the only evidence an extraction was real rather than a rename.

## The scatter resampler

`Resampler::scatter` **deposits** a kernel into the destination at a
fractional position with 1/rate density compensation. It is the adjoint of
interpolation — the transpose of the usual operation.

Every resampling library that exists (soxr, libsamplerate, zita-resampler,
Signalsmith) is a *gather* resampler: it reads from a source at a fractional
position. Nobody ships the transpose, because playback only ever gathers.
Writing at a variable rate is what a tape machine does, and it is the reason
this library is not simply a wrapper over one of those.

## Dependencies

The library itself has **one**, and it is third-party. A second, first-party
one is used by the SUITE only.

### What the library links

One: [signalsmith-dsp](https://github.com/Signalsmith-Audio/dsp) (MIT,
header-only), for the Kaiser window the polyphase bank is built from. That is
a deliberate exception to this ecosystem's usual dependency-free rule — a
Kaiser window is a *specification*, it needs a modified Bessel function of the
first kind, and getting that subtly wrong does not fail loudly, it quietly
degrades the stopband. Clone with `--recursive`.

### What the suite links

[chalkwalk-dsp](https://github.com/chalkwalk/chalkwalk-dsp) (MIT, JUCE-free),
for spectral measurement — shared with every other project in this ecosystem
that asserts a spectral claim, because the alternative was two identical
radix-2 transforms in two repositories with nothing keeping them honest about
each other.

`chalkwalk::dsp` only, never `chalkwalk::dsp::measure`: that target carries
libebur128, and asserting that a resampler is quiet does not need BS.1770.

**It is inside the tests guard.** A parent building only the library never
configures chalkwalk-dsp, never clones it, and never discovers it exists. The
edge runs one way — dsp holds primitives and knows no domain, this is a machine
built from them — and `chalkwalk_tape_layering` is a ctest that fails if
chalkwalk-dsp ever reaches back. A cycle between two header-only libraries does
not fail loudly; it just quietly makes neither of them extractable.

## Build and test

```sh
git clone --recursive https://github.com/chalkwalk/chalkwalk-tape.git
cd chalkwalk-tape && cmake -B build && cmake --build build && ctest --test-dir build
```

Standalone with nothing else on the machine, which is the test of the boundary
rather than a convenience. A library that only builds inside its parent has not
been extracted.

The resampler's tests measure spectra rather than samples: the whole point of a
band-limited read is being quiet where a naive one is not, and that is a claim
about frequency. The FFT they use is written in the test helper — and is itself
checked against closed-form answers before anything depends on it — because an
FFT in *production* is exactly the kind of thing this ecosystem takes as a
dependency rather than writes.

## Licence

MIT. See [LICENSE](LICENSE).

Part of the [chalkwalk](https://github.com/chalkwalk) plugin ecosystem,
alongside [chalkwalk-music](https://github.com/chalkwalk/chalkwalk-music) and
[chalkwalk-dsp](https://github.com/chalkwalk/chalkwalk-dsp).
