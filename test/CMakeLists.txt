# Catch2, guarded so a parent that already provides it is reused rather than
# fetching a second copy.
if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1)
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(chalkwalk_tape_tests
    MediumTests.cpp
    HeadsTests.cpp
    SeamTests.cpp
    LayerStackTests.cpp
    MarkerLaneTests.cpp
    ResamplerTests.cpp
    DeckTests.cpp
    SpectralMeasureTests.cpp
    LossEffectsTests.cpp
    HeadLengthLossTests.cpp
    HeadKernelsTests.cpp
    HysteresisTests.cpp
    HysteresisBatchTests.cpp
    BiasTests.cpp
    TransportTests.cpp
    WearMapTests.cpp
    CompanderTests.cpp
    SlidingBandTests.cpp
    LambdaCutTests.cpp)

target_link_libraries(chalkwalk_tape_tests
    # chalkwalk::dsp is the SUITE's dependency and not the library's: the
    # spectral measure lives there now, shared with every other project that
    # asserts a spectral claim. Never chalkwalk::dsp::measure -- that carries
    # libebur128, and none of these assertions need BS.1770.
    PRIVATE chalkwalk::tape chalkwalk::dsp Catch2::Catch2WithMain)

# ---- NO FUSED MULTIPLY-ADD IN THE SUITE ----
#
# `HysteresisBatchTests` asserts the batch equals the scalar solver BIT FOR BIT,
# because the batch performs the same operations in the same order, and a
# tolerance would let a transcription error hide. That claim only holds if the
# COMPILER also performs the same operations -- and with contraction on it may
# fuse `a*b + c` into one rounding in one path and not the other. Clang does so
# by default wherever the target has FMA, which is every Apple Silicon Mac: the
# macOS job failed by exactly one ulp on the first sample, and an x86 build
# with `-mfma` reproduces it, lane and sample identical, while `-ffp-contract=
# off` clears it.
#
# So the precondition is stated here, for the suite, rather than relaxed into a
# tolerance. It says nothing about consumers' builds, which may contract: there
# the batch and the scalar path can differ by an ulp, which is inaudible.
# MSVC does not contract under its default `/fp:precise`.
if(NOT MSVC)
    target_compile_options(chalkwalk_tape_tests PRIVATE -ffp-contract=off)
endif()

# MSVC does not speak GCC's warning flags, and passing them is a hard error
# rather than a warning -- so an unguarded list breaks the Windows build at the
# command line, before a single file is compiled.
if(MSVC)
    target_compile_options(chalkwalk_tape_tests PRIVATE /W4)
else()
    target_compile_options(chalkwalk_tape_tests
        PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

add_test(NAME chalkwalk_tape COMMAND chalkwalk_tape_tests)

# The dependency on chalkwalk-dsp runs ONE WAY. A cycle between two INTERFACE
# libraries builds and passes; what it destroys is the ability to take either
# one on its own, and that is discovered far too late. See the script.
add_test(NAME chalkwalk_tape_layering
         COMMAND ${CMAKE_COMMAND}
                 -DDSP_DIR=$<TARGET_PROPERTY:chalkwalk_dsp,SOURCE_DIR>
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckLayering.cmake)
