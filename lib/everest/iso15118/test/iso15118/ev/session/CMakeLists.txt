include(Catch)

function(create_ev_session_test_target NAME)
    add_executable(test_ev_${NAME} ${NAME}.cpp)
    target_link_libraries(test_ev_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_ev_${NAME})
endfunction()

create_ev_session_test_target(session_pump)
create_ev_session_test_target(controller)
create_ev_session_test_target(integration_walk)
create_ev_session_test_target(feedback)
