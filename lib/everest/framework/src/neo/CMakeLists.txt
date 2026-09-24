add_executable(everest-neo)
target_sources(everest-neo
    PRIVATE
        main.cpp
        module_host.cpp
)

evc_generate_version_information()
target_include_directories(everest-neo
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated/include
)

target_link_libraries(everest-neo
    PRIVATE
        everest::framework
        Boost::program_options
        ${CMAKE_DL_LIBS}
)

target_compile_options(everest-neo PRIVATE ${COMPILER_WARNING_OPTIONS})

target_compile_features(everest-neo PRIVATE cxx_std_17)

install(
    TARGETS everest-neo
    RUNTIME
)
