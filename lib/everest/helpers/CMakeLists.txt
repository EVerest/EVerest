# EVerest helper functions

add_library(everest_helpers STATIC)
add_library(everest::helpers ALIAS everest_helpers)
ev_register_library_target(everest_helpers)

find_package(Boost REQUIRED COMPONENTS headers)

if(DISABLE_EDM)
    find_package(fmt REQUIRED)
    find_package(nlohmann_json REQUIRED)
endif()

target_sources(everest_helpers
    PRIVATE
        src/helpers.cpp
        src/coverage.cpp
        src/phase_rotation.cpp
)

if (EVEREST_CORE_BUILD_TESTING AND EVEREST_ENABLE_COVERAGE)
    target_compile_definitions(everest_helpers PUBLIC EVEREST_COVERAGE_ENABLED)
endif()

target_include_directories(everest_helpers
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

target_link_libraries(everest_helpers
    PRIVATE
        Boost::headers
        fmt::fmt
        nlohmann_json::nlohmann_json
        everest::tls
)

set_target_properties(everest_helpers
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME helpers
)

add_dependencies(everest_helpers generate_cpp_files)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/everest
    FILES_MATCHING PATTERN "*.hpp"
)

if (BUILD_TESTING)
    # LD_PRELOAD shim that lets integration tests advance a module's monotonic clock
    add_library(everest_clockshift SHARED src/clockshift.cpp)
    target_compile_features(everest_clockshift PRIVATE cxx_std_17)
    target_link_libraries(everest_clockshift PRIVATE ${CMAKE_DL_LIBS})
    install(TARGETS everest_clockshift LIBRARY DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/everest/testing)

    add_subdirectory(tests)
endif()

install(
    TARGETS
        everest_helpers
    EXPORT everest-core-targets
    LIBRARY
)
