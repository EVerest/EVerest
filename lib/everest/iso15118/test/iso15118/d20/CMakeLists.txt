include(Catch)

add_executable(test_timeouts timeouts.cpp)

target_link_libraries(test_timeouts
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_timeouts)

add_executable(test_rational_number rational_number.cpp)

target_link_libraries(test_rational_number
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_rational_number)
