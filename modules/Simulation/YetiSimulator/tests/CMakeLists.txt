set(TEST_TARGET ${PROJECT_NAME}_YetiSimulator_tests)
add_executable(${TEST_TARGET})

# Depend on module build so ev-cli has generated Conf + Intf headers.
add_dependencies(${TEST_TARGET} ${MODULE_NAME})
get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)
target_include_directories(${TEST_TARGET} PRIVATE
    ..                                                   # module root
    ${GENERATED_INCLUDE_DIR}                             # framework-generated common headers
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME} # module-specific generated headers
)

target_sources(${TEST_TARGET} PRIVATE
    RaiseErrorRouterTest.cpp
    DiodeFaultDetectionTest.cpp
    ../util/cp_signal.cpp
    ../util/errors.cpp
    ../util/raise_error_router.cpp
)

target_link_libraries(${TEST_TARGET}
    PRIVATE
        everest::framework
        everest::log
        everest::util
        everest::everest_api_types
        Catch2::Catch2WithMain
        Threads::Threads
)

if (NOT DISABLE_EDM)
    list(APPEND CMAKE_MODULE_PATH ${CPM_PACKAGE_catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(${TEST_TARGET})
endif ()

add_test(${TEST_TARGET} ${TEST_TARGET})
ev_register_test_target(${TEST_TARGET})
