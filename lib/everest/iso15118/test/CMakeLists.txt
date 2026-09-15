add_compile_options(${ISO15118_COMPILE_OPTIONS_WARNING})

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
            everest::iso15118
    )
endif()
