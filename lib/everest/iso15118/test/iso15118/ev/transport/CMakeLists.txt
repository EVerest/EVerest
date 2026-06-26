include(Catch)

function(create_ev_transport_test_target NAME)
    add_executable(test_ev_${NAME} ${NAME}.cpp)
    target_link_libraries(test_ev_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_ev_${NAME})
endfunction()

create_ev_transport_test_target(sdp_client)
create_ev_transport_test_target(data_client)
