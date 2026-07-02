# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

add_subdirectory(grpc_libs)

# Guard: the committed Python protobuf gencode must share a protobuf version
# (major.minor) with the committed C++ gencode. Both are generated from the
# same eebus-grpc definitions; if the Python stubs are regenerated with a
# different protoc they fail to import under the pinned protobuf runtime.
add_test(
    NAME eebus_gencode_version_parity
    COMMAND ${CMAKE_COMMAND}
        -DPY_GEN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/grpc_libs/generated
        -DCPP_GEN_DIR=${CMAKE_SOURCE_DIR}/modules/EnergyManagement/EEBUS/grpc_libs/generated
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_gencode_version_parity.cmake
)
