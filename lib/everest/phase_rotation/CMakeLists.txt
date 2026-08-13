# Phase Rotation

add_library(phase_rotation STATIC)
add_library(everest::phase_rotation ALIAS phase_rotation)
ev_register_library_target(phase_rotation)

target_sources(phase_rotation
    PRIVATE
        src/phase_rotation_utils.cpp
)

target_include_directories(phase_rotation
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

add_dependencies(phase_rotation generate_cpp_files)

target_link_libraries(phase_rotation
    PUBLIC
        everest::framework
)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
