// SPDX-License-Identifier: MIT
//
// The measuring instrument, measured.
//
// ResamplerTests asserts things like "purity above 40 dB" and "the band-limited
// read is quieter than the naive one". Those claims are only as good as the FFT
// underneath them, and this FFT is written here rather than taken from a
// library -- so it is checked before anything is allowed to depend on it, two
// ways: against answers known in closed form, and against the definition of
// the transform itself.
//
// The second check is the one that earns its keep. The closed-form cases use
// one length and compare magnitudes, which leaves the transform free to run
// backwards -- confirmed: flipping the sign of the twiddle angle used to pass
// the whole file. The cross-check against a naive DFT compares complex values
// at every bin across four lengths, and a scattered handful of bins at the
// 32768 the resampler tests actually use, where a recurrence's error has had
// the furthest to accumulate.

#include "LegacyCheck.h"
#include "SpectralMeasure.h"

#include <complex>
#include <random>
#include <string>
#include <vector>

namespace {

// One bin of the discrete Fourier transform, straight from the definition,
// with the twiddle recomputed from scratch for every term.
//
// This is the reference the fast transform is checked against, and it is a
// reference precisely because it shares none of the fast one's machinery:
// no bit reversal, no radix-2 decomposition, and above all no recurrence --
// `measure::fft` advances its twiddle by repeated multiplication (`w *= wl`),
// so its rounding error accumulates with the transform length, and a check
// that used the same trick could not see that.
//
// O(n) per bin, which is what makes the large-size check below affordable.
std::complex<double> dftBin(const std::vector<std::complex<double>> &x,
                            std::size_t k) {
  std::complex<double> sum(0.0, 0.0);
  const double n = static_cast<double>(x.size());
  for (std::size_t t = 0; t < x.size(); ++t) {
    const double angle = -2.0 * measure::kPi * static_cast<double>(k) *
                         static_cast<double>(t) / n;
    sum += x[t] * std::complex<double>(std::cos(angle), std::sin(angle));
  }
  return sum;
}

// Deterministic noise: every bin excited, nothing symmetric, same every run.
std::vector<std::complex<double>> noise(std::size_t n, std::uint32_t seed) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  std::vector<std::complex<double>> x(n);
  for (auto &v : x)
    v = {dist(gen), dist(gen)};
  return x;
}

// The largest magnitude in a spectrum, so the tolerance below can be relative
// to the signal rather than absolute.
double peakMagnitude(const std::vector<std::complex<double>> &v) {
  double peak = 0.0;
  for (const auto &c : v)
    peak = std::max(peak, std::abs(c));
  return peak;
}

// Relative to the spectral peak, and set from measurement rather than taste.
// The disagreement with the definition, on the exact inputs used below:
//
//     n =    64    1.272e-14        n =  4096    1.384e-12
//     n =   256    5.831e-14        n = 32768    2.135e-12
//     n =  1024    2.910e-13
//
// It grows with length, which is what a twiddle recurrence does. So the bound
// is ~47x the worst of those: loose enough to survive a different libm's sin
// and cos and a compiler that contracts the butterfly into an FMA, which
// could plausibly move the last digits by a factor of two or three -- and
// still tight enough to fail on every defect it was tried against. A bound at
// the measured value would be a test that goes red when somebody changes
// compiler, which teaches nobody anything.
constexpr double kDftTolerance = 1.0e-10;

} // namespace

TEST_CASE("the FFT transforms a known signal to its known spectrum") {
  // A DC signal is all energy in bin 0 and nothing anywhere else.
  {
    std::vector<std::complex<double>> v(64, {1.0, 0.0});
    measure::fft(v);
    CHECK_MSG(std::abs(v[0].real() - 64.0) < 1e-9, "DC should sum into bin 0");
    for (std::size_t i = 1; i < v.size(); ++i)
      CHECK_MSG(std::abs(v[i]) < 1e-9,
                "DC leaked into bin " + std::to_string(i));
  }

  // A sine at exactly bin k puts all its energy in bins k and n-k.
  {
    const std::size_t n = 64, k = 5;
    std::vector<std::complex<double>> v(n);
    for (std::size_t i = 0; i < n; ++i)
      v[i] = {std::sin(2.0 * measure::kPi * static_cast<double>(k * i) /
                       static_cast<double>(n)),
              0.0};
    measure::fft(v);
    CHECK_MSG(std::abs(std::abs(v[k]) - static_cast<double>(n) / 2.0) < 1e-9,
              "the bin-k peak is the wrong height");
    CHECK_MSG(std::abs(std::abs(v[n - k]) - static_cast<double>(n) / 2.0) <
                  1e-9,
              "the mirrored peak is the wrong height");
    for (std::size_t i = 0; i < n; ++i)
      if (i != k && i != n - k)
        CHECK_MSG(std::abs(v[i]) < 1e-9,
                  "energy where there should be none, bin " +
                      std::to_string(i));
  }
}

TEST_CASE("the fast transform agrees with the definition, at every bin") {
  // The two closed-form cases above pin the transform at one length against
  // two very structured signals. This pins it against noise -- every bin
  // excited, no symmetry to hide behind -- across the range of lengths, and
  // it compares COMPLEX values rather than magnitudes.
  //
  // That last part matters: flipping the sign of the twiddle angle conjugates
  // the whole spectrum and leaves every magnitude untouched, so the tests
  // above pass with the transform running backwards. Nothing downstream reads
  // a phase, so that particular defect would be harmless -- but a test that
  // cannot see the direction of the transform cannot see much else about the
  // twiddles either.
  for (const std::size_t n : {std::size_t{64}, std::size_t{256},
                              std::size_t{1024}, std::size_t{4096}}) {
    const auto input = noise(n, 20260819u);
    auto fast = input;
    measure::fft(fast);

    const double peak = peakMagnitude(fast);
    CHECK_MSG(peak > 0.0,
              "the transform produced nothing at n=" + std::to_string(n));

    double worst = 0.0;
    std::size_t worstBin = 0;
    for (std::size_t k = 0; k < n; ++k) {
      const double err = std::abs(fast[k] - dftBin(input, k));
      if (err > worst) {
        worst = err;
        worstBin = k;
      }
    }
    CHECK_MSG(worst / peak < kDftTolerance,
              "n=" + std::to_string(n) + " bin " + std::to_string(worstBin) +
                  " differs from the definition by " +
                  std::to_string(worst / peak) + " of the peak");
  }
}

TEST_CASE("the fast transform still agrees at the length actually used") {
  // ResamplerTests measures at 1 << 15, and the check above stops at 4096
  // because a full naive transform is O(n^2) and 32768 of those is a second
  // of test time per bin set. The recurrence error grows with LENGTH, so the
  // size in use is exactly the one worth checking -- and a single bin is only
  // O(n), so checking a scattered handful costs nothing.
  //
  // The bins are spread rather than clustered: the twiddle is advanced once
  // per butterfly, so error accumulates along the transform and a run of
  // neighbouring bins would sample one part of it.
  const std::size_t n = std::size_t{1} << 15;
  const auto input = noise(n, 20260820u);
  auto fast = input;
  measure::fft(fast);

  const double peak = peakMagnitude(fast);
  CHECK_MSG(peak > 0.0, "the transform produced nothing at n=32768");

  for (const std::size_t k :
       {std::size_t{0}, std::size_t{1}, std::size_t{127}, std::size_t{1024},
        std::size_t{5461}, std::size_t{16384}, std::size_t{24575},
        std::size_t{32767}}) {
    const double err = std::abs(fast[k] - dftBin(input, k)) / peak;
    CHECK_MSG(err < kDftTolerance, "bin " + std::to_string(k) +
                                       " at n=32768 differs from the "
                                       "definition by " +
                                       std::to_string(err) + " of the peak");
  }
}

TEST_CASE("a pure tone reads as pure and a noisy one does not") {
  // The end-to-end claim the resampler tests rest on: purity has to be high for
  // something clean and low for something dirty, or an assertion about it means
  // nothing.
  const double sr = 48000.0, f = 4000.0;
  const int n = 1 << 15;

  std::vector<float> clean(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    clean[static_cast<std::size_t>(i)] =
        static_cast<float>(0.5 * std::sin(2.0 * measure::kPi * f * i / sr));
  CHECK_MSG(measure::sinePurityDb(clean, f, sr) > 70.0f,
            "a clean sine should read very pure");

  // The same tone with a second, unrelated one on top.
  std::vector<float> dirty = clean;
  for (int i = 0; i < n; ++i)
    dirty[static_cast<std::size_t>(i)] += static_cast<float>(
        0.25 * std::sin(2.0 * measure::kPi * 11000.0 * i / sr));
  const float dirtyDb = measure::sinePurityDb(dirty, f, sr);
  CHECK_MSG(dirtyDb < 20.0f, "an obviously contaminated tone read as pure: " +
                                 std::to_string(dirtyDb) + " dB");
  CHECK_MSG(dirtyDb < measure::sinePurityDb(clean, f, sr),
            "contamination should lower purity");
}

TEST_CASE("rms is the rms") {
  CHECK_MSG(measure::rms({}) == 0.0, "an empty signal has no level");
  CHECK_MSG(std::abs(measure::rms({1.0f, 1.0f, 1.0f}) - 1.0) < 1e-9, "DC");
  CHECK_MSG(std::abs(measure::rms({1.0f, -1.0f}) - 1.0) < 1e-9, "a square");
  CHECK_MSG(std::abs(measure::rms({0.0f, 0.0f}) - 0.0) < 1e-9, "silence");
}
