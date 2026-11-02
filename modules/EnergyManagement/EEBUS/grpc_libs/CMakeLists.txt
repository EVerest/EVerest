include(${CMAKE_CURRENT_LIST_DIR}/cmake/generate-cpp-from-protobuf.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sanity-checks.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/add-libraries.cmake)

set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

setup_grpc()

if (ENABLE_GRPC_GENERATOR)
    ev_is_python_venv_active(
        RESULT_VAR IS_PYTHON_VENV_ACTIVE
    )
    if (IS_PYTHON_VENV_ACTIVE)
        ev_install_pip_package(NAME grpcio VERSION 1.71.0)
        ev_install_pip_package(NAME grpcio-tools VERSION 1.71.0)
    endif()

    generate_cpp_from_protobuf()
endif()

add_libraries()

if (ENABLE_GRPC_GENERATOR)
    # Force regeneration, if ENABLE_GRPC_GENERATOR is set
    add_dependencies(proto_common_types generate_eebus_grpc_api_proto)
    add_dependencies(proto_control_service generate_eebus_grpc_api_proto)
    add_dependencies(proto_cs_lpc_service generate_eebus_grpc_api_proto)

    sanity_checks()
endif()
