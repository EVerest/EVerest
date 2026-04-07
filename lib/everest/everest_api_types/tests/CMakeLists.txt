set(TEST_TARGET_NAME ${PROJECT_NAME}_API_serialize_tests)

add_executable(${TEST_TARGET_NAME})

####################### Testing types/*.yaml file hashes against expected values
set(EXPECTED_TYPE_HASHES_CSV "${CMAKE_CURRENT_SOURCE_DIR}/expected_types_file_hashes.csv")
set(ACTUAL_TYPE_HASHES_CSV ${CMAKE_BINARY_DIR}/generated/lib/everest/everest_api_types/tests/actual_types_file_hashes.csv)
file(GLOB ALL_TYPE_FILES "${PROJECT_SOURCE_DIR}/types/*")

set(CREATE_FILE_HASHES_PY ${CMAKE_CURRENT_SOURCE_DIR}/manual_tests/source_file_hash_check/create_file_hashes.py)
set(VERIFY_FILE_HASHES_PY ${CMAKE_CURRENT_SOURCE_DIR}/manual_tests/source_file_hash_check/verify_file_hashes.py)

execute_process(
  COMMAND ${Python3_EXECUTABLE}
        ${CREATE_FILE_HASHES_PY}
        ${EXPECTED_TYPE_HASHES_CSV}
        ${PROJECT_SOURCE_DIR}
        ${ACTUAL_TYPE_HASHES_CSV}
  RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "create_file_hashes.py failed for types (exit code ${_result})")
endif()
# On mismatch: Print a CMake warning during configuration time
execute_process(
  COMMAND ${Python3_EXECUTABLE}
        ${VERIFY_FILE_HASHES_PY}
        ${EXPECTED_TYPE_HASHES_CSV}
        ${ACTUAL_TYPE_HASHES_CSV}
  OUTPUT_VARIABLE _verify_output
  RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
  message(WARNING "!!! HASH MISMATCH DETECTED !!!\n\n${_verify_output}\nUnit tests are going to FAIL!\nCheck the changed files' contents, adapt the API type conversions if necessary and update the expected hashes.\nSee lib/everest/everest_api_types/README.md for further details.")
endif()

add_custom_command(
  OUTPUT ${ACTUAL_TYPE_HASHES_CSV}
  COMMAND ${Python3_EXECUTABLE}
        ${CREATE_FILE_HASHES_PY}
        ${EXPECTED_TYPE_HASHES_CSV}
        ${PROJECT_SOURCE_DIR}
        ${ACTUAL_TYPE_HASHES_CSV}
  DEPENDS ${CREATE_FILE_HASHES_PY} ${EXPECTED_TYPE_HASHES_CSV} ${ALL_TYPE_FILES}
  COMMENT "Regenerating ${ACTUAL_TYPE_HASHES_CSV} from source files..."
)

add_custom_target(generate_actual_hash_csv ALL
    DEPENDS ${ACTUAL_TYPE_HASHES_CSV}
)

####################### Testing interfaces/*.yaml file hashes against expected values
set(EXPECTED_IFC_HASHES_CSV "${CMAKE_CURRENT_SOURCE_DIR}/expected_interfaces_file_hashes.csv")
set(ACTUAL_IFC_HASHES_CSV ${CMAKE_BINARY_DIR}/generated/lib/everest/everest_api_types/tests/actual_interfaces_file_hashes.csv)
file(GLOB ALL_IFC_FILES "${PROJECT_SOURCE_DIR}/interfaces/*")

execute_process(
  COMMAND ${Python3_EXECUTABLE}
        ${CREATE_FILE_HASHES_PY}
        ${EXPECTED_IFC_HASHES_CSV}
        ${PROJECT_SOURCE_DIR}
        ${ACTUAL_IFC_HASHES_CSV}
  RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "create_file_hashes.py failed for interfaces (exit code ${_result})")
endif()
# On mismatch: Print a CMake warning during configuration time
execute_process(
  COMMAND ${Python3_EXECUTABLE}
        ${VERIFY_FILE_HASHES_PY}
        ${EXPECTED_IFC_HASHES_CSV}
        ${ACTUAL_IFC_HASHES_CSV}
  OUTPUT_VARIABLE _verify_output
  RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
  message(WARNING "!!! HASH MISMATCH DETECTED !!!\n\n${_verify_output}\nUnit tests are going to FAIL!\nPlease check your file contents or update the expected hashes.\nSee lib/everest/everest_api_types/README.md for further details.")
endif()

add_custom_command(
  OUTPUT ${ACTUAL_IFC_HASHES_CSV}
  COMMAND ${Python3_EXECUTABLE}
        ${CREATE_FILE_HASHES_PY}
        ${EXPECTED_IFC_HASHES_CSV}
        ${PROJECT_SOURCE_DIR}
        ${ACTUAL_IFC_HASHES_CSV}
  DEPENDS ${CREATE_FILE_HASHES_PY} ${EXPECTED_IFC_HASHES_CSV} ${ALL_IFC_FILES}
  COMMENT "Regenerating ${ACTUAL_IFC_HASHES_CSV} from source files..."
)

add_custom_target(generate_actual_type_hash_csv ALL
    DEPENDS ${ACTUAL_TYPE_HASHES_CSV}
)

add_custom_target(generate_actual_ifc_hash_csv ALL
    DEPENDS ${ACTUAL_IFC_HASHES_CSV}
)

add_test(
  NAME source_file_hash_check
  COMMAND ${Python3_EXECUTABLE} -m pytest
          ${CMAKE_CURRENT_SOURCE_DIR}/manual_tests/source_file_hash_check/test_source_file_hashes.py
          --actual-types-csv ${ACTUAL_TYPE_HASHES_CSV}
          --actual-ifc-csv   ${ACTUAL_IFC_HASHES_CSV}
          -v
)

####################### GTest and automatic (de-)serialization tests

set(INCLUDE_DIR
  "${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/include"
  "${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests"
)

target_include_directories(${TEST_TARGET_NAME} PUBLIC
  ${INCLUDE_DIR}
)

target_sources(${TEST_TARGET_NAME} PRIVATE
  manual_tests/serialization/generic.hpp
  manual_tests/serialization/generic.cpp
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
  GTest::gmock
  GTest::gtest_main
  everest::log
  everest::framework
  everest::everest_api_types
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})

file(GLOB API_FILES ${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/include/everest_api_types/*/API.hpp)
file(GLOB TEST_GENERATOR_PY_FILES ${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests/test_file_autogenerator/*.py)
set (TEST_DISABLE_FILE ${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests/test_file_autogenerator/disable.csv)
set (SERIALIZATION_TEST_HELPERS_HPP ${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests/SerializationTestHelpers.hpp)

set(GENERATED_TESTS_LOCATION ${CMAKE_BINARY_DIR}/generated/lib/everest/everest_api_types/tests/)

#update tests on api file change

set(
    GENERATE_COMMAND
    ${Python3_EXECUTABLE} -B "${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests/test_file_autogenerator/main.py"
     "${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/include/"
     "${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests/"
     "${PROJECT_SOURCE_DIR}/lib/everest/everest_api_types/tests/test_file_autogenerator/disable.csv"
     "${GENERATED_TESTS_LOCATION}"
    VERBATIM
)

add_custom_command(OUTPUT ${GENERATED_TESTS_LOCATION}
    COMMAND
    ${GENERATE_COMMAND}
    DEPENDS ${API_FILES} ${TEST_GENERATOR_PY_FILES} ${TEST_DISABLE_FILE} ${SERIALIZATION_TEST_HELPERS_HPP}
)

add_custom_target(generate_new_tests_on_file_change_only ALL   DEPENDS ${GENERATED_TESTS_LOCATION})

add_dependencies(${TEST_TARGET_NAME} generate_new_tests_on_file_change_only)

#generate tests for the first time

if(NOT EXISTS ${GENERATED_TESTS_LOCATION}/CMakeLists.txt)
    execute_process(COMMAND ${GENERATE_COMMAND})
endif()


add_subdirectory(${GENERATED_TESTS_LOCATION} ${GENERATED_TESTS_LOCATION})
