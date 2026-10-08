# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Fails if the committed EEBUS Python protobuf gencode version (major.minor)
# diverges from the committed C++ gencode version. Both trees are generated
# from the same eebus-grpc protobuf definitions and must share one protobuf
# toolchain version (pinned via the `grpc` git_tag in dependencies.yaml). When
# the Python stubs are regenerated with a different protoc than the C++ stubs,
# they fail to import under the pinned protobuf runtime -- the exact failure
# this guard exists to catch.
#
# Invoked as:
#   cmake -DPY_GEN_DIR=<dir> -DCPP_GEN_DIR=<dir> -P check_gencode_version_parity.cmake

function(_first_version out_var files pattern)
    foreach(file ${files})
        file(STRINGS "${file}" matched_line REGEX "${pattern}")
        if(matched_line)
            string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${matched_line}")
            set(${out_var} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

file(GLOB_RECURSE py_files "${PY_GEN_DIR}/*_pb2.py")
file(GLOB_RECURSE cpp_files "${CPP_GEN_DIR}/*.pb.cc")

_first_version(py_version "${py_files}" "Protobuf Python Version:")
_first_version(cpp_version "${cpp_files}" "Protobuf C\\+\\+ Version:")

if(NOT py_version)
    message(FATAL_ERROR "No '# Protobuf Python Version' stamp found under ${PY_GEN_DIR}")
endif()
if(NOT cpp_version)
    message(FATAL_ERROR "No '// Protobuf C++ Version' stamp found under ${CPP_GEN_DIR}")
endif()

if(NOT py_version STREQUAL cpp_version)
    message(FATAL_ERROR
        "EEBUS protobuf gencode version mismatch: "
        "Python ${py_version} vs C++ ${cpp_version} (major.minor).\n"
        "Regenerate tests/eebus_tests/grpc_libs/generated with the grpcio-tools "
        "release matching the 'grpc' git_tag in dependencies.yaml so both gencode "
        "trees share one protobuf version.")
endif()

message(STATUS "EEBUS gencode parity OK: Python ${py_version} == C++ ${cpp_version} (major.minor)")
