function(evc_setup_package_no_export)
    #
    # handle passed arguments
    #
    set(options "")
    set(one_value_args
        NAME
        LIBRARY_NAME
        NAMESPACE
        VERSION
        COMPATIBILITY
        IMPORTED_LOCATION_FILENAME
        TYPE
    )
    set(multi_value_args
        ADDITIONAL_CONTENT
        PATH_VARS
    )

    cmake_parse_arguments(OPTNS "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (OPTNS_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION} got unknown argument(s): ${OPTNS_UNPARSED_ARGUMENTS}")
    endif()

    if (NOT OPTNS_NAME)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION} requires NAME parameter for the package name")
    endif()

    if (NOT OPTNS_LIBRARY_NAME)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION} requires LIBRARY_NAME parameter for the package name")
    endif()

    if (NOT OPTNS_TYPE)
        message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION} requires TYPE parameter for the library type")
    endif()

    set(LIBRARY_PACKAGE_NAME "${OPTNS_NAME}")
    set(LIBRARY_PACKAGE_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/${LIBRARY_PACKAGE_NAME})
    set(LIBRARY_NAME "${OPTNS_LIBRARY_NAME}")
    set(LIBRARY_TYPE "${OPTNS_TYPE}")

    set(LIBRARY_PACKAGE_VERSION ${PROJECT_VERSION})
    if (OPTNS_VERSION)
        set(LIBRARY_PACKAGE_VERSION ${OPTNS_VERSION})
    endif()

    set(LIBRARY_PACKAGE_VERSION_COMPATIBILITY ExactVersion)
    if (OPTNS_COMPATIBILITY)
        set(LIBRARY_PACKAGE_VERSION_COMPATIBILITY ${OPTNS_COMPATIBILITY})
    endif()

    if (OPTNS_PATH_VARS)
        _parse_path_vars(OPTNS_PATH_VARS PATH_VARS_ARG INLINE_CONTENT PARSE_ERROR)
        if (PARSE_ERROR)
            message(FATAL_ERROR "${CMAKE_CURRENT_FUNCTION}: error parsing PATH_VARS (${PARSE_ERROR})")
        endif()

    endif()

    if (OPTNS_NAMESPACE)
        set(NAMESPACE_ARG "${OPTNS_NAMESPACE}::")
    endif()

    if (${LIBRARY_TYPE} STREQUAL "STATIC" OR ${LIBRARY_TYPE} STREQUAL "SHARED")
        set(EXTENSION "so")
        if (${LIBRARY_TYPE} STREQUAL "STATIC")
            set(EXTENSION "a")
        endif()
        set(IMPORTED_LOCATION_FILENAME "lib${LIBRARY_NAME}.${EXTENSION}")
        if (OPTNS_IMPORTED_LOCATION_FILENAME)
            set(IMPORTED_LOCATION_FILENAME "${OPTNS_IMPORTED_LOCATION_FILENAME}")
        endif()
        set(IMPORTED_LOCATION "        IMPORTED_LOCATION \"\${PACKAGE_PREFIX_DIR}/@CMAKE_INSTALL_LIBDIR@/${IMPORTED_LOCATION_FILENAME}\"")
    endif()


    #
    # generate package config file and install it
    #
    set(PACKAGE_CONFIG_IN_FILE ${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_PACKAGE_NAME}.config.cmake.in)

    string(CONCAT DEFAULT_PACKAGE_CONFIG_CONTENT
        "@PACKAGE_INIT@\n\n"
        "if(NOT TARGET ${NAMESPACE_ARG}${LIBRARY_NAME})\n"
        "    add_library(${NAMESPACE_ARG}${LIBRARY_NAME} ${LIBRARY_TYPE} IMPORTED)\n"
        "    set_target_properties(${NAMESPACE_ARG}${LIBRARY_NAME} PROPERTIES\n"
        "        INTERFACE_INCLUDE_DIRECTORIES \"\${PACKAGE_PREFIX_DIR}/@CMAKE_INSTALL_INCLUDEDIR@\"\n"
        "${IMPORTED_LOCATION}"
        "    )\n"
        "endif()\n\n"
        "check_required_components(${LIBRARY_PACKAGE_NAME})\n"
    )
    file(WRITE ${PACKAGE_CONFIG_IN_FILE} ${DEFAULT_PACKAGE_CONFIG_CONTENT})

    configure_package_config_file(
        ${PACKAGE_CONFIG_IN_FILE}
        ${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_PACKAGE_NAME}-config.cmake
        INSTALL_DESTINATION ${LIBRARY_PACKAGE_CMAKE_INSTALL_DIR}
        ${PATH_VARS_ARG}
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_PACKAGE_NAME}-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY ExactVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_PACKAGE_NAME}-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_PACKAGE_NAME}-config-version.cmake
        DESTINATION ${LIBRARY_PACKAGE_CMAKE_INSTALL_DIR}
    )
endfunction()
