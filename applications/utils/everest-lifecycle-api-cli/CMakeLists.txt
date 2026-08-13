find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(everest-lifecycle-api-cli
    src/main.cpp
    src/mqtt_lifecycle_service_client.cpp
    src/command_handlers.cpp
)

target_include_directories(everest-lifecycle-api-cli PRIVATE src)
target_compile_options(everest-lifecycle-api-cli PRIVATE -Wall -Werror -Wextra)
target_compile_features(everest-lifecycle-api-cli PRIVATE cxx_std_17)

target_link_libraries(everest-lifecycle-api-cli
    PRIVATE
        everest::framework
        everest::everest_api_types
        everest::io
        Boost::program_options
)

install(
    TARGETS everest-lifecycle-api-cli
    RUNTIME
)
