#pragma once

// Duinker and Geurst's head-length loss factor, Table II (SOURCES section 17).
//
// THE EXACT SOLUTION, TRANSCRIBED RATHER THAN REIMPLEMENTED. Their equation (36)
// is four nested improper integrals with logarithms and arcsines, and the
// Philips Computing Centre evaluated it for them in 1963. Recomputing that would
// be reproducing a published numerical result with worse tools; the table is the
// result, and it is six hundred numbers.
//
// `H(P, theta)` is the flux a head of overall length `L` collects, relative to
// what it would collect from the gap alone, where
//
//     theta = L / lambda        -- head length in wavelengths
//     P     = pi * R / L        -- how much of the head's length is ROUNDED
//
// and `P` is the parameter that matters for this project, because it is the one
// WEAR MOVES. Their words: a sharp-edged head (`P = 0`) "shows an overshoot in
// its response of about 20 per cent"; rounding makes "these fluctuations die
// down more quickly"; and at `P = pi/2` "the fluctuations have disappeared
// altogether and the response approaches the unity level monotonically". A head
// worn flat has a smaller curved fraction, so wear moves a machine towards the
// left-hand column and a louder head bump (SOURCES section 18).
//
// THE MODEL IS A PLATE-TYPE HEAD: infinitely long pole pieces of infinite
// permeability and infinite height. That is why the long-wavelength limit is 0.5
// -- half the flux closes through the air above the tape -- rather than 0, which
// is what a head of finite height gives. Their section 4 does the finite-height
// case with the tape's wrapping angle as a parameter, and we do not have a
// wrapping angle for any machine, so this is the table we take.
//
// TRANSCRIPTION AND HOW IT WAS CHECKED. Read twice from a library scan: once
// through the scan's OCR layer, and once from the page image. The two readings
// were compared over a 54-cell sample and agreed everywhere. Then four
// properties the paper states in prose were checked against the numbers:
//
//   * the P = 0 overshoot is 1.195, against "about 20 per cent"
//   * the P = pi/2 column rises monotonically, as the text says it must
//   * the theta = 0.1 row falls with P, the rounding costing output at DC
//   * every column is within a few per cent of unity by theta = 10
//
// The last of those is worth a note: the `P = 0` column is still oscillating at
// theta = 10, reaching 0.956. That is not a transcription error -- sharp edges
// are the slowest-decaying case, and figure 5 shows it visibly rippling at the
// right-hand edge of the plot.
//
// ONE CELL IS UNCERTAIN. At theta = 0.4, `P = pi/2`, the OCR gives "0.8?4" and
// the page image reads 0.824. That value keeps the column monotonic, so it is
// used; but the step either side of it (0.035 then 0.045) is the only place in
// the table where a column's steps grow, and 0.834 would smooth it. A cleaner
// scan would settle it. Nothing depends on it: that column is the fully rounded
// head, which is the case with no head bump at all.
//
// JUCE-free by design. Promotion target: chalkwalk-tape.

#include <array>
#include <cmath>
#include <cstddef>

namespace chalkwalk::tape
{
    // The six values of P the paper tabulates, as multiples of pi:
    // 0, 1/16, 1/8, 1/4, 3/8, 1/2.
    inline constexpr std::array<double, 6> kHeadLengthRoundings = {
        0.0,
        M_PI / 16.0,
        M_PI / 8.0,
        M_PI / 4.0,
        3.0 * M_PI / 8.0,
        M_PI / 2.0,
    };

    inline constexpr int kHeadLengthRows = 100;      // theta = 0.1 .. 10.0
    inline constexpr double kHeadLengthStep = 0.1;

    // Table II itself, row-major by theta, column-major by the roundings above.
    inline constexpr std::array<std::array<double, 6>, kHeadLengthRows>
        kHeadLengthLossFactor = { {
        { 0.701, 0.698, 0.692, 0.677, 0.660, 0.643 },   // theta = 0.1
        { 0.851, 0.843, 0.830, 0.799, 0.762, 0.728 },   // theta = 0.2
        { 0.974, 0.961, 0.943, 0.895, 0.840, 0.789 },   // theta = 0.3
        { 1.069, 1.055, 1.032, 0.972, 0.901, 0.824 },   // theta = 0.4
        { 1.138, 1.122, 1.098, 1.031, 0.948, 0.869 },   // theta = 0.5

        { 1.179, 1.165, 1.142, 1.075, 0.985, 0.895 },   // theta = 0.6
        { 1.195, 1.185, 1.166, 1.106, 1.013, 0.916 },   // theta = 0.7
        { 1.188, 1.184, 1.173, 1.124, 1.034, 0.932 },   // theta = 0.8
        { 1.162, 1.165, 1.165, 1.133, 1.050, 0.944 },   // theta = 0.9
        { 1.122, 1.134, 1.145, 1.135, 1.062, 0.954 },   // theta = 1.0

        { 1.074, 1.094, 1.117, 1.130, 1.070, 0.962 },   // theta = 1.1
        { 1.024, 1.050, 1.083, 1.120, 1.076, 0.968 },   // theta = 1.2
        { 0.975, 1.006, 1.048, 1.107, 1.079, 0.974 },   // theta = 1.3
        { 0.933, 0.966, 1.013, 1.091, 1.081, 0.978 },   // theta = 1.4
        { 0.900, 0.933, 0.981, 1.075, 1.081, 0.982 },   // theta = 1.5

        { 0.881, 0.908, 0.954, 1.058, 1.081, 0.985 },   // theta = 1.6
        { 0.874, 0.894, 0.934, 1.041, 1.079, 0.987 },   // theta = 1.7
        { 0.880, 0.890, 0.920, 1.025, 1.077, 0.989 },   // theta = 1.8
        { 0.898, 0.895, 0.912, 1.010, 1.074, 0.991 },   // theta = 1.9
        { 0.925, 0.910, 0.912, 0.996, 1.071, 0.992 },   // theta = 2.0

        { 0.957, 0.935, 0.917, 0.984, 1.067, 0.993 },   // theta = 2.1
        { 0.992, 0.956, 0.928, 0.974, 1.064, 0.994 },   // theta = 2.2
        { 1.026, 0.983, 0.942, 0.966, 1.059, 0.995 },   // theta = 2.3
        { 1.056, 1.010, 0.959, 0.960, 1.055, 0.996 },   // theta = 2.4
        { 1.079, 1.034, 0.977, 0.955, 1.051, 0.997 },   // theta = 2.5

        { 1.092, 1.054, 0.994, 0.953, 1.046, 0.997 },   // theta = 2.6
        { 1.097, 1.067, 1.011, 0.951, 1.042, 0.998 },   // theta = 2.7
        { 1.091, 1.075, 1.026, 0.952, 1.037, 0.998 },   // theta = 2.8
        { 1.077, 1.075, 1.037, 0.953, 1.033, 0.998 },   // theta = 2.9
        { 1.056, 1.070, 1.046, 0.956, 1.029, 0.999 },   // theta = 3.0

        { 1.031, 1.059, 1.051, 0.960, 1.024, 0.999 },   // theta = 3.1
        { 1.003, 1.043, 1.052, 0.965, 1.020, 0.999 },   // theta = 3.2
        { 0.976, 1.026, 1.050, 0.970, 1.016, 0.999 },   // theta = 3.3
        { 0.952, 1.007, 1.046, 0.976, 1.012, 0.999 },   // theta = 3.4
        { 0.934, 0.988, 1.038, 0.982, 1.008, 0.999 },   // theta = 3.5

        { 0.923, 0.972, 1.029, 0.988, 1.005, 0.999 },   // theta = 3.6
        { 0.920, 0.958, 1.019, 0.994, 1.002, 1.000 },   // theta = 3.7
        { 0.925, 0.949, 1.009, 0.999, 0.998, 1.000 },   // theta = 3.8
        { 0.937, 0.945, 0.998, 1.005, 0.996, 1.000 },   // theta = 3.9
        { 0.954, 0.945, 0.989, 1.010, 0.993, 1.000 },   // theta = 4.0

        { 0.976, 0.950, 0.981, 1.014, 0.990, 1.000 },   // theta = 4.1
        { 0.999, 0.958, 0.974, 1.018, 0.988, 1.000 },   // theta = 4.2
        { 1.022, 0.970, 0.969, 1.021, 0.986, 1.000 },   // theta = 4.3
        { 1.042, 0.983, 0.967, 1.023, 0.984, 1.000 },   // theta = 4.4
        { 1.057, 0.997, 0.966, 1.024, 0.983, 1.000 },   // theta = 4.5

        { 1.066, 1.010, 0.968, 1.025, 0.981, 1.000 },   // theta = 4.6
        { 1.069, 1.022, 0.971, 1.025, 0.980, 1.000 },   // theta = 4.7
        { 1.065, 1.032, 0.976, 1.025, 0.979, 1.000 },   // theta = 4.8
        { 1.054, 1.039, 0.982, 1.024, 0.978, 1.000 },   // theta = 4.9
        { 1.039, 1.042, 0.988, 1.022, 0.978, 1.000 },   // theta = 5.0

        { 1.020, 1.042, 0.995, 1.020, 0.977, 1.000 },   // theta = 5.1
        { 1.000, 1.038, 1.002, 1.017, 0.977, 1.000 },   // theta = 5.2
        { 0.980, 1.032, 1.008, 1.014, 0.977, 1.000 },   // theta = 5.3
        { 0.962, 1.023, 1.014, 1.011, 0.977, 1.000 },   // theta = 5.4
        { 0.949, 1.013, 1.018, 1.007, 0.978, 1.000 },   // theta = 5.5

        { 0.941, 1.003, 1.022, 1.004, 0.978, 1.000 },   // theta = 5.6
        { 0.939, 0.992, 1.024, 1.001, 0.979, 1.000 },   // theta = 5.7
        { 0.943, 0.983, 1.024, 0.998, 0.979, 1.000 },   // theta = 5.8
        { 0.952, 0.975, 1.023, 0.995, 0.980, 1.000 },   // theta = 5.9
        { 0.966, 0.970, 1.020, 0.992, 0.981, 1.000 },   // theta = 6.0

        { 0.983, 0.967, 1.018, 0.990, 0.982, 1.000 },   // theta = 6.1
        { 1.001, 0.967, 1.013, 0.988, 0.983, 1.000 },   // theta = 6.2
        { 1.019, 0.970, 1.009, 0.986, 0.984, 1.000 },   // theta = 6.3
        { 1.034, 0.975, 1.004, 0.985, 0.986, 1.000 },   // theta = 6.4
        { 1.046, 0.981, 0.999, 0.984, 0.987, 1.000 },   // theta = 6.5

        { 1.053, 0.989, 0.994, 0.984, 0.988, 1.000 },   // theta = 6.6
        { 1.055, 0.997, 0.990, 0.984, 0.989, 1.000 },   // theta = 6.7
        { 1.052, 1.005, 0.987, 0.985, 0.991, 1.000 },   // theta = 6.8
        { 1.043, 1.013, 0.984, 0.986, 0.992, 1.000 },   // theta = 6.9
        { 1.030, 1.019, 0.982, 0.987, 0.994, 1.000 },   // theta = 7.0

        { 1.015, 1.023, 0.982, 0.988, 0.995, 1.000 },   // theta = 7.1
        { 0.999, 1.026, 0.982, 0.990, 0.996, 1.000 },   // theta = 7.2
        { 0.983, 1.026, 0.984, 0.992, 0.998, 1.000 },   // theta = 7.3
        { 0.968, 1.025, 0.986, 0.994, 0.999, 1.000 },   // theta = 7.4
        { 0.958, 1.021, 0.989, 0.996, 1.000, 1.000 },   // theta = 7.5

        { 0.951, 1.016, 0.993, 0.998, 1.001, 1.000 },   // theta = 7.6
        { 0.950, 1.010, 0.997, 1.000, 1.003, 1.000 },   // theta = 7.7
        { 0.953, 1.004, 1.000, 1.002, 1.004, 1.000 },   // theta = 7.8
        { 0.961, 0.997, 1.004, 1.004, 1.005, 1.000 },   // theta = 7.9
        { 0.972, 0.991, 1.007, 1.006, 1.006, 1.000 },   // theta = 8.0

        { 0.986, 0.985, 1.010, 1.007, 1.007, 1.000 },   // theta = 8.1
        { 1.002, 0.982, 1.012, 1.009, 1.007, 1.000 },   // theta = 8.2
        { 1.016, 0.979, 1.014, 1.010, 1.008, 1.000 },   // theta = 8.3
        { 1.029, 0.978, 1.014, 1.011, 1.009, 1.000 },   // theta = 8.4
        { 1.039, 0.979, 1.014, 1.011, 1.009, 1.000 },   // theta = 8.5

        { 1.045, 0.982, 1.013, 1.011, 1.010, 1.000 },   // theta = 8.6
        { 1.047, 0.986, 1.011, 1.011, 1.010, 1.000 },   // theta = 8.7
        { 1.044, 0.990, 1.009, 1.011, 1.011, 1.000 },   // theta = 8.8
        { 1.036, 0.996, 1.006, 1.010, 1.011, 1.000 },   // theta = 8.9
        { 1.025, 1.001, 1.003, 1.009, 1.011, 1.000 },   // theta = 9.0

        { 1.012, 1.006, 1.000, 1.008, 1.011, 1.000 },   // theta = 9.1
        { 0.998, 1.011, 0.997, 1.007, 1.011, 1.000 },   // theta = 9.2
        { 0.985, 1.015, 0.995, 1.005, 1.011, 1.000 },   // theta = 9.3
        { 0.972, 1.017, 0.992, 1.004, 1.011, 1.000 },   // theta = 9.4
        { 0.963, 1.018, 0.990, 1.002, 1.011, 1.000 },   // theta = 9.5

        { 0.958, 1.018, 0.989, 1.001, 1.011, 1.000 },   // theta = 9.6
        { 0.956, 1.016, 0.988, 0.999, 1.010, 1.000 },   // theta = 9.7
        { 0.959, 1.013, 0.989, 0.998, 1.010, 1.000 },   // theta = 9.8
        { 0.966, 1.009, 0.989, 0.996, 1.010, 1.000 },   // theta = 9.9
        { 0.976, 1.005, 0.990, 0.995, 1.009, 1.000 },   // theta = 10.0
    } };

    // H(P, theta), bilinear between the tabulated points.
    //
    // OUTSIDE THE TABLE IT HOLDS THE EDGE, and both edges are the right answer
    // rather than a fallback. Above theta = 10 every shape has converged to
    // within a few per cent of unity, which is what the head-length loss factor
    // means: at wavelengths short against the head, only the gap matters. Below
    // theta = 0.1 the table stops and so does the paper's plot; a head reading a
    // wavelength ten times its own length is outside anything anybody measured.
    [[nodiscard]] inline double headLengthLossFactor(double rounding,
                                                     double theta) noexcept
    {
        // Locate the rounding between two tabulated columns.
        std::size_t lo = 0;
        while (lo + 2 < kHeadLengthRoundings.size()
               && rounding > kHeadLengthRoundings[lo + 1])
            ++lo;
        const std::size_t hi = lo + 1;
        const double span = kHeadLengthRoundings[hi] - kHeadLengthRoundings[lo];
        double u = (span > 0.0) ? (rounding - kHeadLengthRoundings[lo]) / span : 0.0;
        u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);

        // And theta between two rows.
        const double position = theta / kHeadLengthStep - 1.0;
        const double clamped = position < 0.0
            ? 0.0
            : (position > kHeadLengthRows - 1 ? kHeadLengthRows - 1 : position);
        const auto row = static_cast<std::size_t>(clamped);
        const std::size_t next = (row + 1 < kHeadLengthRows) ? row + 1 : row;
        const double v = clamped - static_cast<double>(row);

        const double a = kHeadLengthLossFactor[row][lo] * (1.0 - u)
                       + kHeadLengthLossFactor[row][hi] * u;
        const double b = kHeadLengthLossFactor[next][lo] * (1.0 - u)
                       + kHeadLengthLossFactor[next][hi] * u;
        return a * (1.0 - v) + b * v;
    }

    // The same, from a wavelength and a head: theta is the head length in
    // wavelengths, so this is where `faceLengthMetres` enters.
    [[nodiscard]] inline double headLengthLossFactor(double waveNum,
                                                     double faceLengthMetres,
                                                     double rounding) noexcept
    {
        if (waveNum <= 0.0 || faceLengthMetres <= 0.0)
            return kHeadLengthLossFactor[0][0];   // the longest wavelength we have
        const double lambda = 2.0 * M_PI / waveNum;
        return headLengthLossFactor(rounding, faceLengthMetres / lambda);
    }
}
