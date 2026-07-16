include(Catch)

add_executable(test_tbd_controller_monitor tbd_controller_monitor_test.cpp)

target_link_libraries(test_tbd_controller_monitor
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_tbd_controller_monitor)
