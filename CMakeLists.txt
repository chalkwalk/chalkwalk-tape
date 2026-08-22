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

if(CHALKWALK_TAPE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
