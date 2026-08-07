include(Catch)

function(create_fsm_test_target NAME)
    add_executable(test_fsm_${NAME} ${NAME}.cpp)
    target_sources(test_fsm_${NAME}
        PRIVATE
            helper.cpp
    )
    target_link_libraries(test_fsm_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_fsm_${NAME})
endfunction()

create_fsm_test_target(session_setup)
create_fsm_test_target(service_detail)
create_fsm_test_target(dc_pre_charge)
create_fsm_test_target(der_iec_charge_parameter_discovery)
create_fsm_test_target(der_iec_charge_loop)

add_executable(test_d20_transitions d20_transitions.cpp)

target_sources(test_d20_transitions
    PRIVATE
        helper.cpp
)

target_link_libraries(test_d20_transitions
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_d20_transitions)

# Pre-20 SECC state machines: no helper.cpp, they use the harnesses in secc_helper.hpp
function(create_secc_fsm_test_target NAME)
    add_executable(test_fsm_${NAME} ${NAME}.cpp)
    target_link_libraries(test_fsm_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_fsm_${NAME})
endfunction()

create_secc_fsm_test_target(din_transitions)
create_secc_fsm_test_target(d2_transitions)
