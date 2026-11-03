add_library(iso15118_ev_session STATIC)
add_library(everest::iso15118_ev_session ALIAS iso15118_ev_session)
ev_register_library_target(iso15118_ev_session)

target_sources(iso15118_ev_session
    PRIVATE
        src/contract_store.cpp
        src/der_publish_names.cpp
        src/sae_profile_config.cpp
        src/session_callbacks.cpp
        src/session_setup.cpp
)

target_include_directories(iso15118_ev_session
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

# The enum conversions are exhaustive switches; a new enumerator must fail the build.
target_compile_options(iso15118_ev_session
    PRIVATE
        -Werror=switch
)

add_dependencies(iso15118_ev_session generate_cpp_files)

# The public headers include generated types, which include nlohmann/json.hpp.
target_link_libraries(iso15118_ev_session
    PUBLIC
        everest::iso15118_ev
        nlohmann_json::nlohmann_json
    PRIVATE
        everest::log
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
