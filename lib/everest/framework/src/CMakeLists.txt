add_subdirectory(management_api)

add_executable(manager)
target_sources(manager
    PRIVATE
        system_unix.cpp
        manager.cpp
)
# generate version information header
evc_generate_version_information()
target_include_directories(manager
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated/include
)

target_link_libraries(manager
    PRIVATE
        everest::framework
        Boost::program_options
        management_api
)

target_compile_options(manager PRIVATE ${COMPILER_WARNING_OPTIONS})

target_compile_features(manager PRIVATE cxx_std_17)

install(
    TARGETS manager
    RUNTIME
)
