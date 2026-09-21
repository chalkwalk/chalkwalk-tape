# chalkwalk-tape -- a JUCE-free tape deck: medium, heads, layers, resampling.
#
# Written to work BOTH nested inside a parent project and standalone, so that
# `cmake -B build && ctest` works from this directory with nothing else on the
# machine. That is not a convenience: a library that only builds inside its
# parent has not actually been extracted, and the standalone build is the test
# of the boundary.

cmake_minimum_required(VERSION 3.22)
project(chalkwalk_tape VERSION 0.1.0 LANGUAGES CXX)

# STATIC rather than INTERFACE: unlike chalkwalk-music and chalkwalk-dsp, this
# has real translation units. The deck, the layer stack and the medium own
# state and lifetime, which is precisely why they are not header-only.
add_library(chalkwalk_tape STATIC
    src/Medium.cpp
    src/LayerStack.cpp
    src/Deck.cpp)
add_library(chalkwalk::tape ALIAS chalkwalk_tape)

target_include_directories(chalkwalk_tape
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# signalsmith-dsp (MIT, header-only), for one thing: the Kaiser window the
# resampler's polyphase bank is built from.
#
# THE ONE DEPENDENCY IN THIS ECOSYSTEM, and it is deliberate. A Kaiser window
# is a specification -- it needs a modified Bessel function of the first kind,
# and getting that subtly wrong does not fail, it quietly degrades the
# stopband. That is exactly the case where taking a dependency beats writing
# one. SYSTEM so its warnings never trip a consumer's -Werror.
target_include_directories(chalkwalk_tape SYSTEM
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules>)

target_compile_features(chalkwalk_tape PUBLIC cxx_std_17)
set_target_properties(chalkwalk_tape PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(chalkwalk_tape PRIVATE /W4)
else()
    target_compile_options(chalkwalk_tape PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wimplicit-fallthrough)
endif()

# Tests only when built on its own, or when a parent explicitly asks.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHALKWALK_TAPE_TESTS_DEFAULT ON)
else()
    set(CHALKWALK_TAPE_TESTS_DEFAULT OFF)
endif()
option(CHALKWALK_TAPE_TESTS "Build chalkwalk-tape tests" ${CHALKWALK_TAPE_TESTS_DEFAULT})

# ---------------------------------------------------------------------------
# chalkwalk-dsp -- FOR THE SUITE ONLY, and that turned out to be the whole of it.
#
# The question was whether this library should depend on chalkwalk-dsp, and the
# answer came out smaller than the argument for it.
#
# THE ARGUMENT WAS SOUND. Four headers had ended up here -- `MinimumPhase`,
# `FirDesign`, `Interpolator` and a `target_clones` macro -- under
# `chalkwalk::tape`, none of which know what tape is. They were here because
# there was no other way to share them, so the next consumer wanting a cepstral
# filter design would have had to take a tape library or write a third copy.
#
# BUT THE FIX WAS TO MOVE THEM DOWN, NOT TO REACH UP. They are chalkwalk-dsp's
# now, and with them gone nothing in `chalkwalk_tape` uses that library at all.
# The production target keeps NO first-party dependency, and a consumer of this
# library initialises nothing new.
#
# What genuinely needs it is the SUITE -- spectral measurement, shared with
# every other project that asserts a spectral claim. So the whole block is
# inside the tests guard: a parent building only the library never configures
# chalkwalk-dsp, never clones it, and never discovers it exists.
#
# `chalkwalk::dsp` ONLY, NEVER `chalkwalk::dsp::measure`, even here. The latter
# carries libebur128, and asserting that a resampler is quiet does not need a
# BS.1770 meter.
#
# ONE DIRECTION, and `test/CheckLayering.cmake` keeps it that way: dsp is the
# lower layer and must never reference tape. A cycle between two header-only
# libraries does not fail loudly; it just makes neither extractable.
#
# THE OUTER PROJECT'S SHA WINS. A parent that already added chalkwalk-dsp keeps
# its own and the pin here describes only the standalone build -- so this is an
# API-compatibility obligation rather than a version pin. It also means a
# parent must add the LOWER layer first, or this one's pin silently becomes the
# one that describes the build.
# ---------------------------------------------------------------------------
if(CHALKWALK_TAPE_TESTS AND NOT TARGET chalkwalk_dsp)
    if(DEFINED CHALKWALK_DSP_DIR OR DEFINED ENV{CHALKWALK_DSP_DIR})
        if(NOT DEFINED CHALKWALK_DSP_DIR)
            set(CHALKWALK_DSP_DIR "$ENV{CHALKWALK_DSP_DIR}")
        endif()
        set(_cw_dsp_root "${CHALKWALK_DSP_DIR}")
        message(STATUS
            "chalkwalk-dsp: OVERRIDE at ${_cw_dsp_root} "
            "(the submodule SHA does not describe this build)")
    else()
        set(_cw_dsp_root "${CMAKE_CURRENT_SOURCE_DIR}/libs/dsp")
        if(NOT EXISTS "${_cw_dsp_root}/CMakeLists.txt")
            message(FATAL_ERROR
                "No chalkwalk-dsp.\n"
                "  libs/dsp is not checked out and CHALKWALK_DSP_DIR is not "
                "set. Either:\n"
                "    git submodule update --init --recursive\n"
                "  or point at a working checkout:\n"
                "    cmake -B build -DCHALKWALK_DSP_DIR=/path/to/chalkwalk-dsp")
        endif()
    endif()
    add_subdirectory("${_cw_dsp_root}" "${CMAKE_BINARY_DIR}/libs/dsp")
elseif(TARGET chalkwalk_dsp)
    message(STATUS "chalkwalk-dsp: already provided by a parent project")
endif()


if(CHALKWALK_TAPE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
