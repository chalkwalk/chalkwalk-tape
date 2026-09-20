#pragma once

// Vector width without a distribution decision (DESIGN.md section 4.2).
//
// JUCE-free by design. Promotion target: chalkwalk-tape.

// WIDER VECTORS WITHOUT A WIDER BASELINE.
//
// A contiguous hot loop is worth about 1.4x at 256-bit over the SSE2 the x86-64
// baseline guarantees. Taking that by raising -march would be the wrong trade:
// non-AVX x86 parts were still shipping new in 2021-22 (Intel's Atom-derived
// Celeron and Pentium lines have no AVX at all), studio machines are kept for a
// decade on purpose, and the failure mode is SIGILL at plugin-scan time with
// nothing pointing at the cause.
//
// Function multiversioning gets both from ONE binary: the compiler emits a
// baseline clone and an AVX2 clone, and an ifunc resolver picks at load time.
// Measured on a 512-tap dot product, 41.4 ns baseline against 30.8 ns cloned --
// the same figure a whole -march=x86-64-v3 build produced.
//
// IT DOES NOT CHANGE RESULTS, which is not luck and is worth stating. The
// "avx2" target enables AVX2 and NOT FMA, so no contraction happens and the two
// clones agree bit for bit -- PlaybackFilter's float-path residual reads
// -136.227 dB either way. A clone list including "fma" would break that, and
// with it PRINCIPLES section 5's promise that a project opens identically on
// any computer.
//
// GUARDED, because it is a GNU extension resting on ifunc: glibc has it, and
// Apple and MSVC do not. Elsewhere this expands to nothing and the baseline
// build is exactly what it was.
#if defined(__GNUC__) && !defined(__clang__) && defined(__x86_64__) \
    && !defined(__APPLE__) && !defined(_WIN32)
  #define REMANENCE_MULTIVERSION __attribute__((target_clones("default","avx2")))
#else
  #define REMANENCE_MULTIVERSION
#endif
