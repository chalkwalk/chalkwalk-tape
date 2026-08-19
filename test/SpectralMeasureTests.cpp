// SPDX-License-Identifier: MIT
//
// The measuring instrument, measured.
//
// ResamplerTests asserts things like "purity above 40 dB" and "the band-limited
// read is quieter than the naive one". Those claims are only as good as the FFT
// underneath them, and this FFT is written here rather than taken from a
// library -- so it is checked against answers that are known in closed form
// before anything is allowed to depend on it.

#include "LegacyCheck.h"
#include "SpectralMeasure.h"

#include <complex>
#include <string>
#include <vector>

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
    CHECK_MSG(std::abs(std::abs(v[n - k]) - static_cast<double>(n) / 2.0) < 1e-9,
              "the mirrored peak is the wrong height");
    for (std::size_t i = 0; i < n; ++i)
      if (i != k && i != n - k)
        CHECK_MSG(std::abs(v[i]) < 1e-9,
                  "energy where there should be none, bin " + std::to_string(i));
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
  CHECK_MSG(dirtyDb < 20.0f,
            "an obviously contaminated tone read as pure: " +
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
