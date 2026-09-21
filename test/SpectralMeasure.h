// SPDX-License-Identifier: MIT
#pragma once

// A seam, not an implementation.
//
// The radix-2 transform, the Blackman-Harris window and the six-bin exclusion
// skirt that used to be here are `chalkwalk::dsp::spectrum`'s now. They were
// the same code, line for line, as Remanence's -- two repositories, two
// suites, one algorithm, and nothing keeping the two honest about each other.
//
// `measure::` is kept so the assertions in this suite did not have to move to
// accommodate a refactor. `spectrumOf` keeps its old name for the same reason;
// downstream it is `magnitudes`.
//
// This is a TEST-ONLY dependency. `chalkwalk_tape` itself links nothing from
// chalkwalk-dsp -- see the root CMakeLists -- so a consumer of this library
// initialises no new submodule.

#include <chalkwalk/dsp/Spectrum.h>

#include <cmath>
#include <vector>

namespace measure
{
    namespace spectrum = chalkwalk::dsp::spectrum;

    using spectrum::kPi;
    using spectrum::fft;
    using spectrum::sinePurityDb;
    inline constexpr int kSpectralBand = spectrum::kSpectralSkirt;

    [[nodiscard]] inline std::vector<double> spectrumOf(const std::vector<float>& x,
                                                        int fftOrder = 15)
    {
        return spectrum::magnitudes(x, fftOrder);
    }

    // Stays here: `chalkwalk::dsp::measure::rms` takes a pointer and a count
    // and lives behind the target that carries libebur128. Asserting that a
    // resampler is quiet should not link a BS.1770 meter, so these four lines
    // are cheaper than the dependency they would buy.
    [[nodiscard]] inline double rms(const std::vector<float>& x)
    {
        if (x.empty())
            return 0.0;
        double acc = 0.0;
        for (float v : x)
            acc += static_cast<double>(v) * static_cast<double>(v);
        return std::sqrt(acc / static_cast<double>(x.size()));
    }
}
