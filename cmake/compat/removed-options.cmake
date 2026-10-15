
# ignoring these would silently build something else than asked for
foreach(_opt IN ITEMS EVEREST_LIBS_ONLY EVEREST_INCLUDE_LIBS EVEREST_EXCLUDE_LIBS EVEREST_IO_WITH_MQTT)
    if(DEFINED ${_opt})
        message(FATAL_ERROR "${_opt} was removed, select the packages to build with EVEREST_PACKAGES "
            "(base;iso15118;ocpp;core;runtime). In an existing build directory, remove it with -U ${_opt}.")
    endif()
endforeach()

# these have no effect: what is built is installed, and BUILD_TESTING / EVEREST_TEST_PACKAGES select the tests
foreach(_opt IN ITEMS
        LOG_INSTALL TIMER_INSTALL EVEREST_SQLITE_INSTALL EVSE_SECURITY_INSTALL TLS_INSTALL FRAMEWORK_INSTALL
        OCPP_INSTALL ISO15118_INSTALL CB_V2G_INSTALL IEEE2030_INSTALL
        everest-framework_BUILD_TESTING everest-log_BUILD_TESTING everest-timer_BUILD_TESTING
        everest-sqlite_BUILD_TESTING everest-evse_security_BUILD_TESTING EVEREST_LIBLOG_BUILD_TESTING
        LIBEVSE_SECURITY_BUILD_TESTING ISO15118_BUILD_TESTING IEEE2030_BUILD_TESTING ocpp_BUILD_TESTING
        CB_V2G_BUILD_TESTS BUILD_CBV2G_JSON_WRAPPER_TESTS
        ISO15118_USE_EXPORTED_BUILD IEEE2030_USE_EXPORTED_BUILD
        DISABLE_ISO15118_LOCAL_DEPENDENCIES DISABLE_IEEE2030_LOCAL_DEPENDENCIES
        LIBLOG_USE_BOOST_FILESYSTEM EVEREST_SQLITE_USE_BOOST_FILESYSTEM LIBEVSE_SECURITY_USE_BOOST_FILESYSTEM
        LIBOCPP_USE_BOOST_FILESYSTEM EVEREST_CORE_BUILD_TESTING)
    if(DEFINED ${_opt})
        message(WARNING "${_opt} was removed and has no effect: what is built is installed, "
            "and BUILD_TESTING / EVEREST_TEST_PACKAGES select the tests.")
    endif()
endforeach()
