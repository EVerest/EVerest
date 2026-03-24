include(Catch)

add_executable(test_timeouts timeouts.cpp)

target_link_libraries(test_timeouts
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_timeouts)

add_executable(test_control_event control_event.cpp)

target_link_libraries(test_control_event
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_control_event)

