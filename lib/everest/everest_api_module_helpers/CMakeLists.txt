add_library(everest_api_module_helpers OBJECT)
add_library(everest::everest_api_module_helpers ALIAS everest_api_module_helpers)
ev_register_library_target(everest_api_module_helpers)

target_compile_options(everest_api_module_helpers
  PUBLIC -Wall -Wextra -pedantic -Werror=switch)

target_include_directories(everest_api_module_helpers
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

target_sources(everest_api_module_helpers
  PRIVATE
    src/ApiHelper.cpp
    src/ValidatingMqttProxy.cpp
)

add_dependencies(everest_api_module_helpers generate_cpp_files)

target_link_libraries(everest_api_module_helpers
  PUBLIC
    everest::everest_api_types
    everest::framework
)

install(TARGETS everest_api_module_helpers
  EXPORT everest_api_module_helpers-targets
  LIBRARY
)

install(
  DIRECTORY include/everest_api_module_helpers
  DESTINATION include
)
