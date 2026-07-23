include(Catch)

add_executable(test_feedback feedback.cpp)

target_link_libraries(test_feedback
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_feedback)

add_executable(test_session_teardown session_teardown.cpp)

target_link_libraries(test_session_teardown
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_session_teardown)

add_executable(test_tbd_controller_teardown tbd_controller_teardown.cpp)

target_link_libraries(test_tbd_controller_teardown
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_tbd_controller_teardown)

add_executable(test_secc_sap secc_sap.cpp)

target_link_libraries(test_secc_sap
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_secc_sap)
