#pragma once

#include <signalsmith-dsp/windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace chalkwalk::tape
{
    // Bandlimited fractional resampler (9.25 R1). A polyphase windowed-sinc
    // (Kaiser window from Signalsmith DSP) with a RATE-AWARE cutoff: reading a
    // source faster than unity (pitch-up) scales its spectrum up, folding content
    // above the destination Nyquist back as aliasing. So for a read rate > 1 the
    // sinc cutoff drops to ~Nyquist/rate, band-limiting the source before it
    // aliases. At or below unity the full-band (Nyquist) kernel is used, so a
    // band-limited signal reads back unchanged and DC always passes at unity.
    //
    // A small bank of fixed-cutoff polyphase tables is built once in the
    // constructor (off the audio thread — allocation happens here); read() selects
    // the bucket for the live rate and is allocation-free / real-time safe. Each
    // phase is normalised to unit DC gain, so a constant reads back as itself at
    // every fraction and every rate. Shared by the sample players (anti-aliased
    // pitch-up) and the looper varispeed write path.
    class Resampler
    {
    public:
        // THE KERNEL LENGTH SCALES WITH THE RATE, and that is the point of the
        // bank rather than an implementation detail.
        //
        // A bucket band-limits to Nyquist/maxRate, so its sinc's zeros are
        // maxRate source samples apart. Representing that in a FIXED sixteen-tap
        // window means, at maxRate 5.66, holding 1.4 zero crossings -- which is
        // not a filter. Measured before this changed: a scatter-write at rate
        // 4.68 rippled by 1.90 dB where a correct interpolating kernel is flat,
        // and the gather rejected a tone half an octave above what the rate can
        // carry by 13 dB.
        //
        // Rejection and ripple both depend on taps/rate rather than on taps:
        //
        //   taps/rate     4        8         12
        //   rejection   -30 dB   -60/-75   -80/-95
        //   ripple       0.38     0.006     ~0
        //
        // So eight per unit of rate, floored at the original sixteen -- nothing
        // at or below rate 2 changes at all.
        //
        // THE BANK REACHES RATE 32, and used to stop at 5.66. What made that a
        // problem is how `bucketFor` behaves past the end: it clamps to the last
        // bucket, so a read at rate 28 was band-limited for 5.66 and everything
        // between the two folded. Measured on music, a shuttle at twelve times
        // play speed aliased at **-39 dB** and at thirty-two times at **-23 dB**,
        // which is hash rather than a quiet muffled cue.
        //
        // Half-octave steps continue to 32, which serves a twelve-times shuttle
        // directly and brings thirty-two times within one halving. Past that the
        // answer is a decimated copy of the source rather than a longer kernel:
        // at rate 64 the taps outnumber the samples they span by eight to one,
        // and storing a band-limited half-rate copy is cheaper than bridging the
        // gap at read time, every time.
        static constexpr int kMinHalf = 8;          // taps either side, to rate 2
        static constexpr int kMaxHalf = 128;        // the ceiling, at the top bucket
        static constexpr int kPhases = 256;         // sub-sample table resolution

        // The highest rate the bank band-limits CORRECTLY. Above it `bucketFor`
        // clamps and the anti-aliasing stops improving, so a caller that can
        // decimate its source should do so to get back under this.
        //
        // Exposed because callers were mirroring it as a literal, which is the
        // kind of duplicated constant that goes stale silently. Ask the bank.
        static constexpr double kTopRate = 32.0;

        // The worst-case bound, for callers that cannot know their rate. It is
        // the LONGEST kernel in the bank rather than the only one.
        //
        // PREFER `halfFor(rate)`. Extending the bank to rate 32 took this from
        // 24 to 128, and anything sizing a safety margin from the constant grew
        // five-fold for a kernel it will never use: the write path runs at or
        // near unity -- a record chain decimates to the medium rate before the
        // head, and a transport that is spooling is not recording -- so its
        // margin should come from its own rate, not from the fastest read the
        // bank can serve. That is what broke the erase-head tests when this
        // number moved, and the fix was to ask for the rate's kernel.
        static constexpr int kHalf = kMaxHalf;
        static constexpr int kTaps = 2 * kHalf;

        Resampler() { buildBank(); }

        // The kernel itself, for callers whose samples are not a flat `const
        // float*` — a Medium is depth-erased and its indices wrap, so the heads
        // (§40.10) run the tap loop themselves rather than handing over a
        // pointer. `taps` is kTaps coefficients for the phase nearest `frac`
        // (which must lie in [0,1]); tap i sits at source index
        // `floor(pos) - (kHalf - 1) + i`.
        //
        // `writeGain` is the scatter-write's |rate| factor (see
        // scatterAddCircular), clamped at the bucket ceiling. Read paths ignore
        // it; write paths multiply the deposit by it. Both share the bucket, so
        // read and write cannot drift apart in their band-limiting.
        struct Kernel
        {
            const float* taps = nullptr;
            int count = 2 * kMinHalf;   // how many coefficients `taps` holds
            int half = kMinHalf;        // tap i sits at floor(pos) - (half-1) + i
            float writeGain = 1.0f;
        };

        // How long the kernel for `rate` actually is, in taps either side.
        //
        // The tight bound: a caller that knows its rate can size a lookahead, a
        // commit span or an erase gap from the kernel it will really use rather
        // than from `kHalf`, which is the whole bank's worst case.
        [[nodiscard]] int halfFor(double rate) const noexcept
        {
            return bucketFor(rate).half;
        }

        [[nodiscard]] Kernel kernelFor(double rate, double frac) const noexcept
        {
            const Bucket& b = bucketFor(rate);
            int ph = static_cast<int>(frac * kPhases + 0.5);
            if (ph < 0) ph = 0;
            if (ph > kPhases) ph = kPhases;
            return { b.table.data() + static_cast<std::size_t>(ph) * (2 * b.half),
                     2 * b.half, b.half,
                     static_cast<float>(std::min(std::abs(rate), b.maxRate)) };
        }

        // Interpolate mono `src` (length `srcLen`) at continuous position `pos`,
        // band-limited for a read `rate` (source samples advanced per output
        // sample; pass the magnitude for reverse). Window indices are clamped at
        // the buffer edges, so a position near either end reads a held edge sample
        // rather than out of bounds.
        [[nodiscard]] float read(const float* src, int srcLen, double pos,
                                 double rate) const noexcept
        {
            const Bucket& b = bucketFor(rate);
            const double baseF = std::floor(pos);
            const int base = static_cast<int>(baseF);
            int ph = static_cast<int>((pos - baseF) * kPhases + 0.5);
            if (ph < 0) ph = 0;
            if (ph > kPhases) ph = kPhases;

            const float* tab = b.table.data() + static_cast<std::size_t>(ph) * (2 * b.half);
            float acc = 0.0f;
            for (int i = 0; i < 2 * b.half; ++i)
            {
                int k = base - (b.half - 1) + i;
                k = k < 0 ? 0 : (k >= srcLen ? srcLen - 1 : k);
                acc += src[k] * tab[static_cast<std::size_t>(i)];
            }
            return acc;
        }

        // read()'s circular twin (9.28.3): the window taps wrap mod `len` instead
        // of clamping at the edges, so a read across the seam of a periodic loop
        // buffer sees the loop's actual continuation — the read-side mirror of
        // scatterAddCircular's wrap. Direction-agnostic; pass any signed rate.
        [[nodiscard]] float readCircular(const float* src, int len, double pos,
                                         double rate) const noexcept
        {
            if (len <= 0) return 0.0f;
            const Bucket& b = bucketFor(rate);
            double p = std::fmod(pos, static_cast<double>(len));
            if (p < 0.0) p += static_cast<double>(len);
            const double baseF = std::floor(p);
            const int base = static_cast<int>(baseF);
            int ph = static_cast<int>((p - baseF) * kPhases + 0.5);
            if (ph < 0) ph = 0;
            if (ph > kPhases) ph = kPhases;

            const float* tab = b.table.data() + static_cast<std::size_t>(ph) * (2 * b.half);
            float acc = 0.0f;
            for (int i = 0; i < 2 * b.half; ++i)
            {
                int k = (base - (b.half - 1) + i) % len;
                if (k < 0) k += len;
                acc += src[k] * tab[static_cast<std::size_t>(i)];
            }
            return acc;
        }

        // Bandlimited scatter-ADD: the transpose of read(). Distribute `in` across
        // the same kernel taps into `dst` at fractional position `pos`, wrapping
        // CIRCULARLY over `len` (a periodic loop buffer). Used for varispeed overdub
        // writes: consecutive input samples land at fractional loop positions, and
        // the windowed spread band-limits the write (rate-aware cutoff) instead of
        // quantising it to the nearest integer. Add-only — the caller owns any
        // decay/feedback, so overlapping windows never multiply existing content.
        // At rate 1 on integer positions the kernel is a delta ⇒ bit-exact `+= in`.
        //
        // 9.28.1 — the deposit is scaled by |rate| (the head signal law, DESIGN
        // §40.10): kernel density on the medium is 1/rate, so an uncompensated
        // write reads back at 1/rate gain (-6 dB at rate 2, +6 dB at half speed)
        // and a stalled head (rate → 0) piles unbounded energy onto one spot.
        // One factor fixes the level, makes the stall write nothing, and lets a
        // scrub through zero fade at the turnaround. Clamped at the bucket
        // ceiling — beyond it the kernel cannot bridge the deposit gaps anyway.
        void scatterAddCircular(float* dst, int len, double pos, double rate,
                                float in) const noexcept
        {
            if (len <= 0) return;
            const Bucket& b = bucketFor(rate);
            in *= static_cast<float>(std::min(std::abs(rate), b.maxRate));
            double p = std::fmod(pos, static_cast<double>(len));
            if (p < 0.0) p += static_cast<double>(len);
            const double baseF = std::floor(p);
            const int base = static_cast<int>(baseF);
            int ph = static_cast<int>((p - baseF) * kPhases + 0.5);
            if (ph < 0) ph = 0;
            if (ph > kPhases) ph = kPhases;

            const float* tab = b.table.data() + static_cast<std::size_t>(ph) * (2 * b.half);
            for (int i = 0; i < 2 * b.half; ++i)
            {
                int k = (base - (b.half - 1) + i) % len;
                if (k < 0) k += len;
                dst[k] += in * tab[static_cast<std::size_t>(i)];
            }
        }

    private:
        struct Bucket
        {
            std::vector<float> table;  // (kPhases+1) rows of 2*half, phase-major
            double maxRate = 1.0;      // serves read rates up to this value
            int half = kMinHalf;       // taps either side; scales with maxRate
        };

        static double sinc(double x) noexcept
        {
            if (std::abs(x) < 1.0e-9) return 1.0;
            constexpr double kPi = 3.14159265358979323846;
            const double px = kPi * x;
            return std::sin(px) / px;
        }

        void buildBank()
        {
            // Half-octave spacing to kTopRate. The first six cover ±24 semitone
            // pitch (4x) plus fine tune, which is what this bank was built for;
            // the rest serve a tape transport shuttling. Each bucket band-limits
            // to Nyquist / maxRate.
            //
            // Cost, stated because it is not nothing: the table is about 900 kB
            // against 154 kB before. It is built once, off the audio thread, and
            // shared process-wide by `sharedKernels()`, and a read touches one
            // phase row -- at most 1 kB -- so the working set is unchanged. What
            // grew is a one-off allocation.
            static constexpr std::array<double, 11> kMaxRates = {
                1.0, 1.41421356, 2.0, 2.82842712, 4.0, 5.65685425,
                8.0, 11.3137085, 16.0, 22.627417, 32.0
            };
            static_assert(kMaxRates.back() == kTopRate,
                          "kTopRate must name the last bucket");
            // Kaiser shape: a ~9-wide main lobe over a 16-tap window gives a clean
            // stopband (~70 dB) with a transition narrow enough to keep the
            // passband flat. operator() is non-const, so keep a mutable instance.
            auto kaiser = signalsmith::windows::Kaiser::withBandwidth(9.0);

            bank_.reserve(kMaxRates.size());
            for (const double mr : kMaxRates)
            {
                const double fc = 0.5 / mr;  // cutoff in cycles/sample (0.5 = Nyquist)
                Bucket b;
                b.maxRate = mr;
                const int wanted = static_cast<int>(std::ceil(4.0 * mr));
                b.half = std::min(kMaxHalf, std::max(kMinHalf, wanted));
                const int taps = 2 * b.half;
                b.table.resize(static_cast<std::size_t>((kPhases + 1) * taps));

                std::vector<double> row(static_cast<std::size_t>(taps));
                for (int ph = 0; ph <= kPhases; ++ph)
                {
                    const double frac = static_cast<double>(ph) / kPhases;
                    double sum = 0.0;
                    for (int i = 0; i < taps; ++i)
                    {
                        // Source offset of tap i from the read point (see read()).
                        const double t = frac + (b.half - 1) - i;
                        const double unit = (t + b.half) / (2.0 * b.half);
                        const double win = (unit >= 0.0 && unit <= 1.0)
                                               ? kaiser(unit) : 0.0;
                        const double h = 2.0 * fc * sinc(2.0 * fc * t) * win;
                        row[static_cast<std::size_t>(i)] = h;
                        sum += h;
                    }
                    // Normalise the phase to unit DC gain.
                    const double inv = (std::abs(sum) > 1.0e-12) ? 1.0 / sum : 1.0;
                    float* dst = b.table.data() + static_cast<std::size_t>(ph) * taps;
                    for (int i = 0; i < taps; ++i)
                        dst[i] = static_cast<float>(row[static_cast<std::size_t>(i)] * inv);
                }
                bank_.push_back(std::move(b));
            }
        }

        [[nodiscard]] const Bucket& bucketFor(double rate) const noexcept
        {
            const double r = std::abs(rate);
            for (const auto& b : bank_)
                if (r <= b.maxRate)
                    return b;
            return bank_.back();  // beyond the last bucket — most band-limited kernel
        }

        std::vector<Bucket> bank_;
    };
}
