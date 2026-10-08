set(DERATING_CURVE_TEST_TARGET ${PROJECT_NAME}_derating_curve_tests)

add_executable(${DERATING_CURVE_TEST_TARGET}
    test_derating_curve.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../derating/derating_curve.cpp
)

target_include_directories(${DERATING_CURVE_TEST_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${DERATING_CURVE_TEST_TARGET} PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

add_test(${DERATING_CURVE_TEST_TARGET} ${DERATING_CURVE_TEST_TARGET})
ev_register_test_target(${DERATING_CURVE_TEST_TARGET})

set(MODULE_TEST_TARGET ${PROJECT_NAME}_ac_temperature_derating_module_tests)

add_executable(${MODULE_TEST_TARGET}
    test_ac_temperature_derating.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../AcTemperatureDerating.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../derating/derating_curve.cpp
)

add_dependencies(${MODULE_TEST_TARGET} ${MODULE_NAME})

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${MODULE_TEST_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../tests/include
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/AcTemperatureDerating
)

target_link_libraries(${MODULE_TEST_TARGET} PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    everest::framework
    everest::timer
    date::date
)

add_test(${MODULE_TEST_TARGET} ${MODULE_TEST_TARGET})
ev_register_test_target(${MODULE_TEST_TARGET})
