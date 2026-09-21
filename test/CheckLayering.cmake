# ---------------------------------------------------------------------------
# THE EDGE GOES ONE WAY: tape may use dsp; dsp may never use tape.
#
# chalkwalk-tape depends on chalkwalk-dsp (see the root CMakeLists for why).
# That is the right direction -- dsp holds primitives and knows no domain, tape
# is a machine built from them -- and it is only safe while it stays a DAG.
#
# A CYCLE BETWEEN TWO HEADER-ONLY LIBRARIES DOES NOT FAIL LOUDLY. Both are
# INTERFACE targets: CMake will happily let two of them reference each other,
# every consumer that pulls in both still builds, and every test still passes.
# What is lost is the thing the split was for -- neither library can be taken
# on its own any more, and the discovery is somebody, much later, finding that
# "just use chalkwalk-dsp" means vendoring a tape deck.
#
# So it is checked, in the only place a compiler will not: the source.
#
# ONE INCLUDE IS ALL IT TAKES, which is why this greps rather than reasons. The
# failure mode is somebody in dsp wanting one type from tape -- a Medium, a
# Kernel -- and reaching for it because it is right there in the build tree.
#
#   cmake -DDSP_DIR=<chalkwalk-dsp root> -P test/CheckLayering.cmake
# ---------------------------------------------------------------------------

if(NOT DEFINED DSP_DIR)
    message(FATAL_ERROR "DSP_DIR must be set")
endif()

if(NOT EXISTS "${DSP_DIR}/include")
    message(FATAL_ERROR "DSP_DIR does not look like chalkwalk-dsp: ${DSP_DIR}")
endif()

# Its headers and its own sources. NOT its tests: a dsp test may legitimately
# want a tape deck to measure, and that creates no dependency for a consumer.
file(GLOB_RECURSE dsp_sources
     "${DSP_DIR}/include/*.h" "${DSP_DIR}/include/*.hpp"
     "${DSP_DIR}/src/*.h" "${DSP_DIR}/src/*.cpp")
list(SORT dsp_sources)

set(offenders "")
set(scanned 0)

foreach(file IN LISTS dsp_sources)
    file(RELATIVE_PATH rel "${DSP_DIR}" "${file}")
    math(EXPR scanned "${scanned} + 1")

    file(STRINGS "${file}" lines)
    set(n 0)
    foreach(line IN LISTS lines)
        math(EXPR n "${n} + 1")
        # Comments may discuss the relationship -- several explain why it is
        # one-way. Strip them before looking, exactly as the JUCE-free guards do.
        string(REGEX REPLACE "//.*" "" code "${line}")
        if(code MATCHES "chalkwalk/tape|chalkwalk::tape|chalkwalk_tape")
            list(APPEND offenders "  ${rel}:${n}: ${line}")
        endif()
    endforeach()
endforeach()

if(offenders)
    string(REPLACE ";" "\n" report "${offenders}")
    message(FATAL_ERROR
        "\nchalkwalk-dsp reaches for chalkwalk-tape:\n\n${report}\n\n"
        "The edge runs one way: tape is built FROM dsp. dsp holds primitives\n"
        "and knows no domain, which is what lets anything take it.\n\n"
        "If dsp genuinely needs this, it is not a tape type -- promote it DOWN\n"
        "into dsp rather than reaching UP for it.\n")
endif()

message(STATUS "layering: ${scanned} chalkwalk-dsp sources, none reach for tape")
