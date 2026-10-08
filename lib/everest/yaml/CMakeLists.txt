if(DISABLE_EDM)
    find_package(nlohmann_json REQUIRED)
    find_package(ryml REQUIRED)
    find_package(fmt REQUIRED)
endif()

add_library(everest_yaml STATIC)
add_library(everest::yaml ALIAS everest_yaml)
ev_register_library_target(everest_yaml)

target_sources(everest_yaml
    PRIVATE
        lib/yaml_loader.cpp
)

target_include_directories(everest_yaml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(everest_yaml PUBLIC cxx_std_17)

set_target_properties(everest_yaml PROPERTIES
    EXPORT_NAME yaml
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(everest_yaml
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        ryml::ryml
        fmt::fmt
        everest::log
)

include(GNUInstallDirs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/everest
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

if (DISABLE_EDM)
    install(
        TARGETS everest_yaml
        EXPORT everest-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/everest
    )

    evc_setup_package_no_export(
        NAME everest-yaml
        LIBRARY_NAME yaml
        IMPORTED_LOCATION_FILENAME libeverest_yaml.a
        NAMESPACE everest
        TYPE STATIC
    )
endif()
