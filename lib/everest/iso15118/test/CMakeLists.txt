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
    if(ISO15118_BUILD_EV_SIDE)
        append_coverage_compiler_flags_to_target(iso15118_ev)
    endif()

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
            everest::iso15118_ev
    )
endif()

# Every header the install ships compiles on its own against the target that installs it.
file(GLOB_RECURSE ISO15118_PUBLIC_HEADERS CONFIGURE_DEPENDS RELATIVE ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/*.hpp)
if(NOT ISO15118_BUILD_EV_SIDE)
    list(FILTER ISO15118_PUBLIC_HEADERS EXCLUDE REGEX "^iso15118/ev/")
endif()
set(ISO15118_PUBLIC_HEADER_SOURCES)
foreach(header IN LISTS ISO15118_PUBLIC_HEADERS)
    set(translation_unit ${CMAKE_CURRENT_BINARY_DIR}/public_headers/${header}.cpp)
    if(NOT EXISTS ${translation_unit})
        file(WRITE ${translation_unit} "#include <${header}>\n")
    endif()
    list(APPEND ISO15118_PUBLIC_HEADER_SOURCES ${translation_unit})
endforeach()
add_library(iso15118_public_headers OBJECT ${ISO15118_PUBLIC_HEADER_SOURCES})
if(ISO15118_BUILD_EV_SIDE)
    target_link_libraries(iso15118_public_headers PRIVATE everest::iso15118_ev)
else()
    target_link_libraries(iso15118_public_headers PRIVATE everest::iso15118)
endif()
