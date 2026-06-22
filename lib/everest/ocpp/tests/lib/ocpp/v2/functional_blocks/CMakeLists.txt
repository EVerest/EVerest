target_include_directories(libocpp_unit_tests PUBLIC
        ../mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/mocks
        ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(libocpp_unit_tests PRIVATE
        test_data_transfer.cpp
        test_reservation.cpp
        test_smart_charging.cpp)


set(TEST_FUNCTIONAL_BLOCK_CONTEXT_SOURCES ${LIBOCPP_LIB_PATH}/ocpp/v2/average_meter_values.cpp
                                          ${LIBOCPP_LIB_PATH}/ocpp/v2/component_state_manager.cpp
                                          ${LIBOCPP_LIB_PATH}/ocpp/v2/connector.cpp
                                          ${LIBOCPP_LIB_PATH}/ocpp/v2/database_handler.cpp
                                          ${LIBOCPP_LIB_PATH}/ocpp/v2/evse.cpp
                                          ${LIBOCPP_LIB_PATH}/ocpp/v2/transaction.cpp)


set(TEST_SECURITY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../device_model_test_helper.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../stubs/timer/timer_stub.cpp
            ${TEST_FUNCTIONAL_BLOCK_CONTEXT_SOURCES}
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/security.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/SecurityEventNotification.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/CertificateSigned.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/DeleteCertificate.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetInstalledCertificateIds.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetCompositeSchedule.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/InstallCertificate.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/SignCertificate.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/Get15118EVCertificate.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/Reset.cpp
            ${LIBOCPP_TEST_INCLUDE_COMMON_SOURCES}
            ${LIBOCPP_TEST_INCLUDE_V2_SOURCES}
)

target_sources(libocpp_test_security PRIVATE
        ${TEST_SECURITY_SOURCES})

target_include_directories(libocpp_test_security PUBLIC
        ${LIBOCPP_INCLUDE_PATH}
        ${LIBOCPP_3RDPARTY_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/../stubs/timer
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/mocks
        ${LIBOCPP_TESTS_V2_ROOT_DIR}
)


set(TEST_AUTHORIZATION_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../device_model_test_helper.cpp
            ${TEST_FUNCTIONAL_BLOCK_CONTEXT_SOURCES}
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/authorization.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/ctrlr_component_variables.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/Authorize.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/ClearCache.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetCompositeSchedule.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetLocalListVersion.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/SendLocalList.cpp
            ${LIBOCPP_TEST_INCLUDE_COMMON_SOURCES}
            ${LIBOCPP_TEST_INCLUDE_V2_SOURCES}
)

target_sources(libocpp_test_authorization PRIVATE
        ${TEST_AUTHORIZATION_SOURCES})

target_include_directories(libocpp_test_authorization PUBLIC
        ${LIBOCPP_INCLUDE_PATH}
        ${LIBOCPP_3RDPARTY_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/mocks
        ${LIBOCPP_TESTS_V2_ROOT_DIR}
)

set(TEST_AVAILABILITY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../device_model_test_helper.cpp
            ${TEST_FUNCTIONAL_BLOCK_CONTEXT_SOURCES}
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/availability.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/ctrlr_component_variables.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/ChangeAvailability.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetCompositeSchedule.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/Heartbeat.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/StatusNotification.cpp
            ${LIBOCPP_TEST_INCLUDE_COMMON_SOURCES}
            ${LIBOCPP_TEST_INCLUDE_V2_SOURCES}
)

target_sources(libocpp_test_availability PRIVATE
        ${TEST_AVAILABILITY_SOURCES})

target_include_directories(libocpp_test_availability PUBLIC
        ${LIBOCPP_INCLUDE_PATH}
        ${LIBOCPP_3RDPARTY_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/mocks
        ${LIBOCPP_TESTS_V2_ROOT_DIR}
)

set(TEST_REMOTE_TRANSACTION_CONTROL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../device_model_test_helper.cpp
            ${TEST_FUNCTIONAL_BLOCK_CONTEXT_SOURCES}
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/remote_transaction_control.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/ctrlr_component_variables.cpp
            # smart_charging.cpp defines the stream operator for ProfileValidationResultEnum, which the smart
            # charging mock needs; profile.cpp and the charging profile related messages are its dependencies.
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/smart_charging.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/profile.cpp
            # smart_charging.cpp references the DynamicScheduleManager and its v2.1 messages.
            ${LIBOCPP_LIB_PATH}/ocpp/v2/dynamic_schedule_manager.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v21/messages/PullDynamicScheduleUpdate.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v21/messages/UpdateDynamicSchedule.cpp
            # ChangeAvailability and Get15118EVCertificate are not used by the tests directly, but the mocks of
            # the availability and security interfaces need their vtables and stream operators.
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/ChangeAvailability.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/ClearChargingProfile.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/Get15118EVCertificate.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetChargingProfiles.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetCompositeSchedule.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/LogStatusNotification.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/NotifyEVChargingNeeds.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/ReportChargingProfiles.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/RequestStartTransaction.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/RequestStopTransaction.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/SetChargingProfile.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/TriggerMessage.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/UnlockConnector.cpp
            ${LIBOCPP_TEST_INCLUDE_COMMON_SOURCES}
            ${LIBOCPP_TEST_INCLUDE_V2_SOURCES}
)

target_sources(libocpp_test_remote_transaction_control PRIVATE
        ${TEST_REMOTE_TRANSACTION_CONTROL_SOURCES})

target_include_directories(libocpp_test_remote_transaction_control PUBLIC
        ${LIBOCPP_INCLUDE_PATH}
        ${LIBOCPP_3RDPARTY_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
        ${LIBOCPP_TESTS_V2_ROOT_DIR}
)

set(TEST_TARIFF_AND_COST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../device_model_test_helper.cpp
            ${TEST_FUNCTIONAL_BLOCK_CONTEXT_SOURCES}
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/tariff_and_cost.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/functional_blocks/display_message.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/ctrlr_component_variables.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/ClearDisplayMessage.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetCompositeSchedule.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/GetDisplayMessages.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/NotifyDisplayMessages.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/SetDisplayMessage.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/CostUpdated.cpp
            ${LIBOCPP_LIB_PATH}/ocpp/v2/messages/TransactionEvent.cpp
            ${LIBOCPP_TEST_INCLUDE_COMMON_SOURCES}
            ${LIBOCPP_TEST_INCLUDE_V2_SOURCES}
)

target_sources(libocpp_test_tariff_and_cost PRIVATE
        ${TEST_TARIFF_AND_COST_SOURCES})

target_include_directories(libocpp_test_tariff_and_cost PUBLIC
        ${LIBOCPP_INCLUDE_PATH}
        ${LIBOCPP_3RDPARTY_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/mocks
        ${LIBOCPP_TESTS_V2_ROOT_DIR}
)
