add_compile_options(${ISO15118_COMPILE_OPTIONS_WARNING})

include(FetchContent)
if (NOT TARGET Catch2::Catch2WithMain)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.10.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_GetProperties(Catch2)
if(catch2_POPULATED)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

add_subdirectory(exi)
add_subdirectory(iso15118)
add_subdirectory(v2gtp)


#
# code coverage specifics
#

if(EVEREST_ENABLE_COVERAGE)
    evc_include(CodeCoverage)
    append_coverage_compiler_flags_to_target(iso15118)

    setup_target_for_coverage_gcovr_html(
        NAME ${PROJECT_NAME}_gcovr_coverage
        EXECUTABLE ctest
    )

    setup_target_for_coverage_gcovr_xml(
        NAME ${PROJECT_NAME}_gcovr_coverage_xml
        EXECUTABLE ctest
    )
endif()

# The evcc test binary exercises the EV side, so it only exists when that side is built.
if(ISO15118_BUILD_EV_SIDE)
    add_executable(evcc)

    target_sources(evcc
        PRIVATE
            evcc.cpp
    )

    target_link_libraries(evcc
        PRIVATE
            iso15118::iso15118
    )
endif()
