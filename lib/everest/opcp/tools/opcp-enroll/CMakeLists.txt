include(GNUInstallDirs)

add_executable(opcp-enroll)

target_sources(opcp-enroll
    PRIVATE
        main.cpp
        direct_security_store.cpp
)

target_link_libraries(opcp-enroll
    PRIVATE
        everest::opcp
        everest::evse_security
        everest::log
)

target_compile_definitions(opcp-enroll
    PRIVATE
        OPCP_DEFAULT_PREFIX="${CMAKE_INSTALL_PREFIX}"
)

target_compile_features(opcp-enroll PRIVATE cxx_std_17)

install(TARGETS opcp-enroll RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
