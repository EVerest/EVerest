add_library(iso15118_conversions STATIC)
add_library(everest::iso15118_conversions ALIAS iso15118_conversions)
ev_register_library_target(iso15118_conversions)

target_sources(iso15118_conversions
    PRIVATE
        src/v2g_message_id.cpp
)

target_include_directories(iso15118_conversions
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

target_compile_options(iso15118_conversions
    PRIVATE
        -Wimplicit-fallthrough
        -Werror=switch-enum
)

add_dependencies(iso15118_conversions generate_cpp_files)

target_link_libraries(iso15118_conversions
    PUBLIC
        everest::iso15118
        nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
