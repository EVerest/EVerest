#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
target_include_directories(${MODULE_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(${MODULE_NAME}
    PRIVATE
        sunspec/limit_policy.cpp
        sunspec/sunspec_reader.cpp
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        modbus-client
)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
