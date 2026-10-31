include(Catch)

# Select one source file's tests with: test_ev_session -# "[#<file stem>]"
add_executable(test_ev_session
    config_validation.cpp
    controller.cpp
    controller_header.cpp
    controller_transport.cpp
    feedback.cpp
    integration_walk.cpp
    integration_walk_d2.cpp
    integration_walk_din.cpp
    protocol_handover.cpp
    sap_offer.cpp
    session_lifecycle.cpp
    session_reactor.cpp
    stop_reason.cpp
    timeouts.cpp
)
target_link_libraries(test_ev_session
    PRIVATE
        iso15118_ev
        Catch2::Catch2WithMain
        OpenSSL::Crypto
        ${CMAKE_DL_LIBS}
)
catch_discover_tests(test_ev_session DISCOVERY_MODE PRE_TEST)
