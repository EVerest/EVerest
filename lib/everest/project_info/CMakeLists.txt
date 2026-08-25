add_library(everest_project_info STATIC src/project_info.cpp)
add_library(everest::project_info ALIAS everest_project_info)

target_include_directories(everest_project_info
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

add_dependencies(everest_project_info generate_cpp_files)
