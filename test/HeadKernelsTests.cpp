// A head may carry its own kernel bank (`Head::setKernels`).
//
// WHY THE LEVER EXISTS. The bank has two: taps per rate, and how far below
// Nyquist the cutoff is guarded. The defaults were chosen for a tape machine
// that shuttles at twelve times speed, where folding is the dominant defect.
// A consumer doing modest varispeed has no such folding to reject, and a
// library that forced the tape machine's passband on it would be changing that
// consumer's sound as the price of sharing code -- which is exactly the kind of
// change that gets attributed to a refactor and never found.
//
// WHY IT HAS TEETH. Below unity rate the two guards agree exactly, so a test
// written at rate 1 would pass whether `setKernels` worked or was ignored
// entirely. These read ABOVE unity, where the guard is the whole difference,
// and assert a floor on the size of it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/tape/Heads.h>
#include <chalkwalk/tape/Medium.h>

#include <cmath>
#include <vector>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kCap = 4096;

    // A medium holding one tone at `cyclesPerSample`.
    struct Reel
    {
        explicit Reel(double cyclesPerSample)
            : store(static_cast<std::size_t>(kCap))
        {
            chalkwalk::tape::Medium::Config cfg;
            cfg.capacitySamples = kCap;
            cfg.channelsPerSubTrack = 1;
            cfg.numSubTracks = 1;
            cfg.topology = chalkwalk::tape::Topology::Circular;
            medium.bind(cfg, chalkwalk::tape::Store{ store.data(), store.size() });
            medium.ensureCommitted(0, kCap);
            for (int i = 0; i < kCap; ++i)
                medium.write(0, 0, i,
                             static_cast<float>(std::sin(2.0 * kPi * cyclesPerSample * i)));
        }

        std::vector<float> store;
        chalkwalk::tape::Medium medium;
    };

    // RMS of `n` frames read from `reel` at `rate`, through `head`.
    double rmsThrough(chalkwalk::tape::ReadHead& head, const Reel& reel,
                      double rate, int n)
    {
        head.setRate(rate);
        head.setPosition(64.0);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float out = 0.0f;
            head.readFrame(reel.medium, 0, &out, 1);
            sum += static_cast<double>(out) * out;
            head.step(reel.medium);
        }
        return std::sqrt(sum / n);
    }
}

TEST_CASE("a head reads with the shared bank unless it is given one", "[heads][kernels]")
{
    chalkwalk::tape::ReadHead head;
    REQUIRE(&head.kernels() == &chalkwalk::tape::sharedKernels());

    static const chalkwalk::tape::Resampler mine(12.0, 1.0);
    head.setKernels(mine);
    REQUIRE(&head.kernels() == &mine);
}

TEST_CASE("below unity the two guards agree, which is why this is not the test",
          "[heads][kernels]")
{
    // 0.2 cycles/sample, read at 0.75. Both banks pass it whole, so this pair
    // is IDENTICAL -- recorded here so the next reader does not write the real
    // assertion at rate 1 and believe it.
    const Reel reel{ 0.2 };

    chalkwalk::tape::ReadHead shared;
    chalkwalk::tape::ReadHead own;
    static const chalkwalk::tape::Resampler wide(12.0, 1.0);
    own.setKernels(wide);

    const double a = rmsThrough(shared, reel, 0.75, 1024);
    const double b = rmsThrough(own, reel, 0.75, 1024);
    REQUIRE(b == Catch::Approx(a).margin(1.0e-6));
}

TEST_CASE("above unity a head's own bank changes what it passes", "[heads][kernels]")
{
    // 0.35 cycles/sample read at rate 1.25 lands at 0.4375 of the output
    // Nyquist-and-a-bit: inside a guard of 1.0 and being rolled off by a guard
    // of 1.15. That is the band the lever moves.
    const Reel reel{ 0.35 };

    chalkwalk::tape::ReadHead shared;          // guarded at 1.15 (the default)
    chalkwalk::tape::ReadHead own;
    static const chalkwalk::tape::Resampler wide(12.0, 1.0);
    own.setKernels(wide);

    const double guarded = rmsThrough(shared, reel, 1.25, 1024);
    const double widened = rmsThrough(own, reel, 1.25, 1024);

    // The wider guard passes more of it. The FLOOR is what gives this teeth: a
    // `setKernels` that silently did nothing would land these on top of each
    // other, and a margin-style check would call that a pass.
    const double dB = 20.0 * std::log10(widened / guarded);
    REQUIRE(dB > 3.0);
}
