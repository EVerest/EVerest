# installs <NAME>: its export set, a config that finds DEPENDENCIES (find_dependency arguments)
# and includes INCLUDES (files shipped next to the config), and a version file
function(ev_install_package)
    cmake_parse_arguments(OPTNS "" "NAME;EXPORT" "DEPENDENCIES;INCLUDES" ${ARGN})
    set(dir ${CMAKE_INSTALL_LIBDIR}/cmake/${OPTNS_NAME})
    set(content "@PACKAGE_INIT@\n\ninclude(CMakeFindDependencyMacro)\n")
    foreach(dep IN LISTS OPTNS_DEPENDENCIES)
        string(APPEND content "find_dependency(${dep})\n")
    endforeach()
    foreach(file IN LISTS OPTNS_INCLUDES)
        string(APPEND content "include(\${CMAKE_CURRENT_LIST_DIR}/${file})\n")
    endforeach()
    string(APPEND content "include(\${CMAKE_CURRENT_LIST_DIR}/${OPTNS_EXPORT}.cmake)\n")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${OPTNS_NAME}-config.cmake.in "${content}")
    configure_package_config_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${OPTNS_NAME}-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${OPTNS_NAME}-config.cmake
        INSTALL_DESTINATION ${dir}
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${OPTNS_NAME}-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )
    install(
        EXPORT ${OPTNS_EXPORT}
        FILE ${OPTNS_EXPORT}.cmake
        NAMESPACE everest::
        DESTINATION ${dir}
    )
    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/${OPTNS_NAME}-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/${OPTNS_NAME}-config-version.cmake
        DESTINATION ${dir}
    )
endfunction()

# installs the library packages built from this tree
function(ev_install_library_packages)
    include(CMakePackageConfigHelpers)
    if(EVEREST_BUILD_BASE)
        ev_install_package(
            NAME everest-base
            EXPORT everest-base-targets
            DEPENDENCIES
                "Boost COMPONENTS log_setup log"
                "date"
                "SQLite3"
                "OpenSSL 3"
            INCLUDES CollectMigrationFiles.cmake
        )
    endif()
    if(EVEREST_BUILD_ISO15118)
        ev_install_package(
            NAME everest-iso15118
            EXPORT everest-iso15118-targets
            DEPENDENCIES
                "everest-base"
                "OpenSSL 3"
                "Threads"
        )
    endif()
    if(EVEREST_BUILD_OCPP)
        ev_install_package(
            NAME everest-ocpp
            EXPORT everest-ocpp-targets
            DEPENDENCIES
                "everest-base"
                "OpenSSL 3"
                "SQLite3"
                "Threads"
                "date"
                "nlohmann_json"
                "nlohmann_json_schema_validator"
                "ryml"
                "libwebsockets"
        )
    endif()
endfunction()
