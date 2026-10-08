# EVerest run application helper functions

find_package(Boost COMPONENTS program_options headers REQUIRED)

if(DISABLE_EDM)
    find_package(fmt REQUIRED)
endif()

add_library(everest_run_application STATIC)
add_library(everest::run_application ALIAS everest_run_application)
ev_register_library_target(everest_run_application)

target_sources(everest_run_application
    PRIVATE
        src/run_application.cpp
)

target_compile_features(everest_run_application PUBLIC cxx_std_17)

target_include_directories(everest_run_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(everest_run_application
    PRIVATE
        Boost::headers
        fmt::fmt
        everest::log
)

set_target_properties(everest_run_application PROPERTIES EXPORT_NAME run_application)

if (DISABLE_EDM)
    install(
        TARGETS everest_run_application
        EXPORT everest-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/everest
        TYPE INCLUDE
    )

    evc_setup_package_no_export(
        NAME everest-run-application
        LIBRARY_NAME run_application
        IMPORTED_LOCATION_FILENAME libeverest_run_application.a
        NAMESPACE everest
        TYPE STATIC
    )
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
