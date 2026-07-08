# OCPP module common
#
# Support code shared by the OCPP modules (conversions between EVerest and
# OCPP types, transaction handling, error handling/mapping and the EVerest
# device model storage).

add_library(ocpp_module_common STATIC)
add_library(everest::ocpp_module_common ALIAS ocpp_module_common)
ev_register_library_target(ocpp_module_common)

target_sources(ocpp_module_common
    PRIVATE
        src/conversions.cpp
        src/error_handling.cpp
        src/transaction_handler.cpp
        src/v16/conversions.cpp
        src/device_model/composed_device_model_storage.cpp
        src/device_model/definitions.cpp
        src/device_model/everest_device_model_storage.cpp
)

target_include_directories(ocpp_module_common
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "$<BUILD_INTERFACE:$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>>"
)

target_compile_options(ocpp_module_common
    PRIVATE
        -Wimplicit-fallthrough
        -Werror=switch-enum
)

add_dependencies(ocpp_module_common generate_cpp_files)

target_link_libraries(ocpp_module_common
    PUBLIC
        everest::ocpp
        everest::framework
    PRIVATE
        everest::ocpp_conversions
)

if (DISABLE_EDM)
    install(
        TARGETS ocpp_module_common
        EXPORT everest-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/everest
    )
    evc_setup_package_no_export(
        NAME everest-ocpp-module-common
        LIBRARY_NAME ocpp_module_common
        NAMESPACE everest
        TYPE STATIC
    )
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/everest
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

if(EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif()
