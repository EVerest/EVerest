target_sources(libocpp_unit_tests PRIVATE
<<<<<<< HEAD
=======
    test_charging_station_base.cpp
    test_connectivity_manager.cpp
>>>>>>> 3856a96 (fix(ocpp): Various 1.6 crashes (#2870))
    test_database_migration_files.cpp
    test_message_queue.cpp
    test_websocket_uri.cpp
)


set(TEST_UTILS_SOURCES ${LIBOCPP_LIB_PATH}/ocpp/common/utils.cpp)

target_sources(libocpp_utils_tests PRIVATE ${TEST_UTILS_SOURCES})
