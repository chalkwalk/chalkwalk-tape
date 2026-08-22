# chalkwalk-tape

A tape deck as a library: medium, heads, layers, seams and resampling.
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
| `Resampler.h` | 16-tap polyphase windowed sinc — **including the scatter write** |
| `ChannelView.h` | Non-owning view over caller storage |

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

One: [signalsmith-dsp](https://github.com/Signalsmith-Audio/dsp) (MIT,
header-only), for the Kaiser window the polyphase bank is built from. That is
a deliberate exception to this ecosystem's usual dependency-free rule — a
Kaiser window is a *specification*, it needs a modified Bessel function of the
first kind, and getting that subtly wrong does not fail loudly, it quietly
degrades the stopband. Clone with `--recursive`.

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
