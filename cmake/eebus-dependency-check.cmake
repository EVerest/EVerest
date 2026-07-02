include_guard(GLOBAL)

# Checks whether all dependencies required to build the EEBUS module
# are available, without failing the configure step. Mirrors the
# decision logic of setup_grpc() from everest-cmake: GRPC_EDM selects
# between the EDM-provided gRPC and a system installation.
#
# eebus_check_dependencies(
#     RESULT_VAR <variable receiving TRUE/FALSE>
#     REASON_VAR <variable receiving the list of missing dependencies>
# )
function(eebus_check_dependencies)
    set(one_value_args
        RESULT_VAR
        REASON_VAR
    )
    cmake_parse_arguments(arg "" "${one_value_args}" "" ${ARGN})
    if (NOT arg_RESULT_VAR)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION} RESULT_VAR not set")
    endif()
    if (NOT arg_REASON_VAR)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION} REASON_VAR not set")
    endif()

    set(missing "")

    if (GRPC_EDM AND NOT DISABLE_EDM)
        if (NOT DEFINED grpc_SOURCE_DIR)
            list(APPEND missing "gRPC (GRPC_EDM=ON but the grpc dependency was not fetched, check EVEREST_DEPENDENCY_ENABLED_GRPC)")
        endif()
    else()
        find_package(Protobuf CONFIG QUIET)
        find_package(gRPC CONFIG QUIET)
        if (NOT Protobuf_FOUND OR NOT gRPC_FOUND)
            list(APPEND missing "gRPC/Protobuf (no system installation found, try GRPC_EDM=ON)")
        endif()
    endif()

    find_program(EEBUS_GO_EXECUTABLE go)
    if (NOT EEBUS_GO_EXECUTABLE)
        list(APPEND missing "Go toolchain")
    endif()

    if (NOT CPM_PACKAGE_eebus-grpc_SOURCE_DIR)
        list(APPEND missing "eebus-grpc sources (check EVEREST_DEPENDENCY_ENABLED_EEBUS_GRPC)")
    endif()

    if (missing)
        set(${arg_RESULT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${arg_RESULT_VAR} TRUE PARENT_SCOPE)
    endif()
    set(${arg_REASON_VAR} "${missing}" PARENT_SCOPE)
endfunction()
