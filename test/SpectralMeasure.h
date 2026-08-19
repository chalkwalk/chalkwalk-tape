// SPDX-License-Identifier: MIT
#pragma once

// Spectral measurement for the resampler's quality assertions.
//
// The resampler's whole purpose is to be quiet where a naive read is not, and
// "quiet" is a spectral claim: an imaging or aliasing artefact is energy at a
// frequency that should not be there. Asserting on samples cannot express it,
// so these tests measure the spectrum.
//
// The FFT is here rather than taken as a dependency because this is TEST code.
// A library that needed one should adopt PFFFT or KISS -- an FFT has a
// specification you can fail to meet, and writing one for production is exactly
// the trade this ecosystem refuses. Forty lines of radix-2 to assert something
// about a window function is a different matter, and it is checked against a
// known answer below before anything relies on it.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace measure {

inline constexpr double kPi = 3.14159265358979323846;

// Number of bins an exclusion band must span to clear a windowed peak's main
// lobe. Blackman-Harris spreads about four bins either side.
inline constexpr int kSpectralBand = 6;

// In-place iterative radix-2 Cooley-Tukey. `v.size()` must be a power of two.
inline void fft(std::vector<std::complex<double>> &v) {
  const std::size_t n = v.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(v[i], v[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * kPi / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (std::size_t k = 0; k < len / 2; ++k) {
        const auto u = v[i + k];
        const auto t = v[i + k + len / 2] * w;
        v[i + k] = u + t;
        v[i + k + len / 2] = u - t;
        w *= wl;
      }
    }
  }
}

// Magnitude spectrum, Blackman-Harris windowed (-92 dB sidelobes, so a pure
// tone reads as genuinely pure) and zero-padded to `fftSize`.
[[nodiscard]] inline std::vector<double> spectrumOf(const std::vector<float> &x,
                                                    int fftOrder = 15) {
  const std::size_t fftSize = std::size_t{1} << fftOrder;
  std::vector<std::complex<double>> buf(fftSize, {0.0, 0.0});

  const std::size_t n = std::min(fftSize, x.size());
  const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
  const double denom = n > 1 ? static_cast<double>(n - 1) : 1.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = 2.0 * kPi * static_cast<double>(i) / denom;
    const double w = a0 - a1 * std::cos(t) + a2 * std::cos(2.0 * t) -
                     a3 * std::cos(3.0 * t);
    buf[i] = {static_cast<double>(x[i]) * w, 0.0};
  }
  fft(buf);

  std::vector<double> mags(fftSize / 2);
  for (std::size_t i = 0; i < fftSize / 2; ++i)
    mags[i] = std::abs(buf[i]);
  return mags;
}

// Energy at the fundamental relative to everything else, in dB. Higher is
// purer: a clean sine reads well above 70; imaging or aliasing pulls it down.
[[nodiscard]] inline float sinePurityDb(const std::vector<float> &x, double f,
                                        double sr, int fftOrder = 15) {
  const auto mags = spectrumOf(x, fftOrder);
  const double binHz = sr / static_cast<double>(std::size_t{1} << fftOrder);
  const int fundBin = static_cast<int>(std::lround(f / binHz));

  double fund = 0.0, rest = 0.0;
  for (int i = 1; i < static_cast<int>(mags.size()); ++i) {
    const double e = mags[static_cast<std::size_t>(i)] *
                     mags[static_cast<std::size_t>(i)];
    if (std::abs(i - fundBin) <= kSpectralBand)
      fund += e;
    else
      rest += e;
  }
  if (rest < 1e-20)
    return 120.0f;
  return static_cast<float>(10.0 * std::log10(fund / rest));
}

[[nodiscard]] inline double rms(const std::vector<float> &x) {
  if (x.empty())
    return 0.0;
  double acc = 0.0;
  for (float v : x)
    acc += static_cast<double>(v) * static_cast<double>(v);
  return std::sqrt(acc / static_cast<double>(x.size()));
}

}  // namespace measure
