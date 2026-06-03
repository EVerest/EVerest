// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   authorize_response:
//   cancel_reservation:
//   enable_disable:                        <used>
//   external_ready_to_start_charging:      <used> (tested in GenericOcppTester, init)
//   force_unlock:                          <used>
//   get_evse:                              <used>
//   pause_charging:                        <used>
//   reserve:
//   resume_charging:
//   set_plug_and_charge_configuration:     <used>
//   stop_transaction:                      <used>
//   update_allowed_energy_transfer_modes:  <used>
//   withdraw_authorization:
//
// vars:
//   car_manufacturer:
//   enforced_limits:
//   ev_info:                               <used>
//   evse_id:
//   hlc_session_failed:
//   hw_capabilities:                       <used>
//   limits:
//   powermeter:                            <used>
//   powermeter_public_key_ocmf:            <used in OCPP 1.6>
//   ready:                                 <used> (tested in GenericOcppTester, init)
//   selected_protocol:
//   session_event:                         <used>
//   supported_energy_transfer_modes:       <used>
//   telemetry:
//   waiting_for_external_ready:            <used> (tested in GenericOcppTester, init)

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>
#include <string>

#include "everest/logging.hpp"
#include "generated/types/evse_manager.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ::testing::_;
using ::testing::Return;

// ----------------------------------------------------------------------------
// cmds

TEST_F(GenericOcppRequiresTester, callEnableDisable) {
    // call_enable_disable() used in cb_connector_effective_operative_status

    using ocpp::v2::OperationalStatusEnum;

    std::vector<json> received;
    interfaces->subscribe_var("evse_manager", "call_enable_disable",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result("true"_json);

    std::int32_t evse_id = 1;
    std::int32_t connector_id = 2;
    auto new_status{OperationalStatusEnum::Inoperative};

    EXPECT_CALL(chargepoint, on_enabled(_, _)).Times(0);
    EXPECT_CALL(chargepoint, on_unavailable(evse_id, connector_id)).Times(1);

    ocpp->cb_connector_effective_operative_status(evse_id, connector_id, new_status);

    interfaces->add_cmd_result("false"_json);
    EXPECT_CALL(chargepoint, on_unavailable(evse_id, connector_id)).Times(0);

    ocpp->cb_connector_effective_operative_status(evse_id, connector_id, OperationalStatusEnum::Operative);

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(
        received[0],
        R"({"cmd_source":{"enable_priority":5000,"enable_source":"CSMS","enable_state":"Disable"},"connector_id":2})"_json);
    EXPECT_EQ(
        received[1],
        R"({"cmd_source":{"enable_priority":5000,"enable_source":"CSMS","enable_state":"Enable"},"connector_id":2})"_json);
}

TEST_F(GenericOcppRequiresTester, callForceUnlock) {
    // call_force_unlock() used in cb_unlock_connector

    using ocpp::v2::UnlockConnectorResponse;
    using ocpp::v2::UnlockStatusEnum;

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_force_unlock", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_force_unlock", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    interfaces->add_cmd_result("true"_json);
    auto result = ocpp->cb_unlock_connector(1, 1);
    // UnlockStatusEnum status;
    // std::optional<StatusInfo> statusInfo;
    // std::optional<CustomData> customData;
    EXPECT_EQ(result.status, UnlockStatusEnum::Unlocked);

    interfaces->add_cmd_result("false"_json);
    result = ocpp->cb_unlock_connector(1, 1);
    EXPECT_EQ(result.status, UnlockStatusEnum::UnlockFailed);

    interfaces->add_cmd_result("true"_json);
    result = ocpp->cb_unlock_connector(2, 1);
    EXPECT_EQ(result.status, UnlockStatusEnum::Unlocked);

    // interfaces->add_cmd_result("true"_json);
    result = ocpp->cb_unlock_connector(3, 1);
    EXPECT_EQ(result.status, UnlockStatusEnum::UnknownConnector);

    ASSERT_EQ(received_0.size(), 2);
    EXPECT_EQ(received_0[0], R"({"connector_id":1})"_json);
    EXPECT_EQ(received_0[1], R"({"connector_id":1})"_json);

    ASSERT_EQ(received_1.size(), 1);
    EXPECT_EQ(received_1[0], R"({"connector_id":1})"_json);
}

TEST_F(GenericOcppRequiresTester, callGetEvse) {
    // call_get_evse() used in get_connector_structure()

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_get_evse", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_get_evse", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    interfaces->add_cmd_result(R"({"id":1,"connectors":[{"id":1}]})"_json);
    interfaces->add_cmd_result(R"({"id":2,"connectors":[{"id":1},{"id":2},{"id":3}]})"_json);

    const auto [structure, v16_mapping] = ocpp->get_connector_structure();

    ASSERT_EQ(received_0.size(), 1);
    EXPECT_EQ(received_0[0], json{});

    ASSERT_EQ(received_1.size(), 1);
    EXPECT_EQ(received_1[0], json{});

    // evseId -> number of connectors
    std::map<std::int32_t, std::int32_t> expected_structure{{1, 1}, {2, 3}};
    std::map<std::int32_t, std::map<std::int32_t, std::int32_t>> expected_mapping{
        {1, {{1, 1}}},
        {2, {{1, 2}, {2, 3}, {3, 4}}},
    };
    EXPECT_EQ(structure, expected_structure);
    EXPECT_EQ(v16_mapping, expected_mapping);
}

TEST_F(GenericOcppRequiresTester, callPauseCharging) {
    // call_pause_charging() used in cb_pause_charging()

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_pause_charging", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_pause_charging", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    // cb_pause_charging ignores the result
    interfaces->add_cmd_result(R"(false)"_json);
    interfaces->add_cmd_result(R"(true)"_json);

    ocpp->cb_pause_charging(1);
    ocpp->cb_pause_charging(2);

    ASSERT_EQ(received_0.size(), 1);
    EXPECT_EQ(received_0[0], json{});

    ASSERT_EQ(received_1.size(), 1);
    EXPECT_EQ(received_1[0], json{});
}

TEST_F(GenericOcppRequiresTester, callResumeCharging) {
    // call_resume_charging() used in cb_resume_charging()

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_resume_charging", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_resume_charging", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    // cb_pause_charging ignores the result
    interfaces->add_cmd_result(R"(false)"_json);
    interfaces->add_cmd_result(R"(true)"_json);

    ocpp->cb_resume_charging(1);
    ocpp->cb_resume_charging(2);

    ASSERT_EQ(received_0.size(), 1);
    EXPECT_EQ(received_0[0], json{});

    ASSERT_EQ(received_1.size(), 1);
    EXPECT_EQ(received_1[0], json{});
}

TEST_F(GenericOcppRequiresTester, callSetPlugAndChargeConfiguration) {
    // call_set_plug_and_charge_configuration() used in
    // - ready_module_configuration()
    // - cb_variable_set(ocpp::v2::SetVariableData)
    //
    // ISO15118Ctrlr:
    // - PnCEnabled
    // - CentralContractValidationAllowed
    // - ContractCertificateInstallationEnabled

    using ocpp::v2::Component;
    using ocpp::v2::SetVariableData;
    using ocpp::v2::Variable;

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_set_plug_and_charge_configuration", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_set_plug_and_charge_configuration", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    // cb_pause_charging ignores the result
    // interfaces->add_cmd_result(R"(false)"_json);
    // interfaces->add_cmd_result(R"(true)"_json);

    ocpp->ready_module_configuration();
    SetVariableData data{"true", {{"ISO15118Ctrlr"}}, {{"PnCEnabled"}}};
    // CiString<2500> attributeValue;
    // Component component;
    // Variable variable;
    // std::optional<AttributeEnum> attributeType;
    // std::optional<CustomData> customData;

    data.attributeValue = "false";

    data.variable.name = "PnCEnabled";
    ocpp->cb_variable_set(data);
    data.variable.name = "CentralContractValidationAllowed";
    ocpp->cb_variable_set(data);
    data.variable.name = "ContractCertificateInstallationEnabled";
    ocpp->cb_variable_set(data);

    data.attributeValue = "true";

    data.variable.name = "PnCEnabled";
    ocpp->cb_variable_set(data);
    data.variable.name = "CentralContractValidationAllowed";
    ocpp->cb_variable_set(data);
    data.variable.name = "ContractCertificateInstallationEnabled";
    ocpp->cb_variable_set(data);

    const std::vector<json> expected{
        R"({"plug_and_charge_configuration":{"central_contract_validation_allowed":true,"contract_certificate_installation_enabled":true,"pnc_enabled":true}})"_json,
        R"({"plug_and_charge_configuration":{"pnc_enabled":false}})"_json,
        R"({"plug_and_charge_configuration":{"central_contract_validation_allowed":false}})"_json,
        R"({"plug_and_charge_configuration":{"contract_certificate_installation_enabled":false}})"_json,
        R"({"plug_and_charge_configuration":{"pnc_enabled":true}})"_json,
        R"({"plug_and_charge_configuration":{"central_contract_validation_allowed":true}})"_json,
        R"({"plug_and_charge_configuration":{"contract_certificate_installation_enabled":true}})"_json,
    };

    ASSERT_EQ(received_0.size(), expected.size());
    ASSERT_EQ(received_1.size(), expected.size());

    for (std::uint8_t i = 0; i < expected.size(); i++) {
        SCOPED_TRACE(std::to_string(i));
        EXPECT_EQ(received_0[i], expected[i]);
        EXPECT_EQ(received_1[i], expected[i]);
    }
}

TEST_F(GenericOcppRequiresTester, callStopTransaction) {
    // call_stop_transaction() used in cb_stop_transaction()

    using ocpp::v2::RequestStartStopStatusEnum;
    using types::evse_manager::StopTransactionReason;

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_stop_transaction", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_stop_transaction", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    interfaces->add_cmd_result(R"(true)"_json);
    interfaces->add_cmd_result(R"(false)"_json);

    const auto result_0 = ocpp->cb_stop_transaction(1, StopTransactionReason::DeAuthorized);
    const auto result_1 = ocpp->cb_stop_transaction(2, StopTransactionReason::StoppedByEV);

    ASSERT_EQ(received_0.size(), 1);
    EXPECT_EQ(received_0[0], R"({"request":{"reason":"DeAuthorized"}})"_json);

    ASSERT_EQ(received_1.size(), 1);
    EXPECT_EQ(received_1[0], R"({"request":{"reason":"StoppedByEV"}})"_json);

    EXPECT_EQ(result_0, RequestStartStopStatusEnum::Accepted);
    EXPECT_EQ(result_1, RequestStartStopStatusEnum::Rejected);
}

TEST_F(GenericOcppRequiresTester, callUpdateAllowedEnergyTransferModes) {
    GTEST_SKIP() << "transaction handling moved to v2_chargepoint";
    // test needs updating

    // call_update_allowed_energy_transfer_modes() used in
    // cb_update_allowed_energy_transfer_modes()

    using ocpp::DateTime;
    using ocpp::v2::EnergyTransferModeEnum;
    using ocpp::v2::IdToken;
    using types::authorization::AuthorizationType;
    using types::authorization::IdTokenType;
    using types::authorization::ProvidedIdToken;
    using types::evse_manager::SessionEventEnum;
    using types::evse_manager::SessionStarted;
    using types::evse_manager::StartSessionReason;

    // there are 2 EVSE Managers - check routing to the correct manager
    std::vector<json> received_0;
    std::vector<json> received_1;
    interfaces->subscribe_var(
        "evse_manager", "call_update_allowed_energy_transfer_modes", 0,
        [&received_0](const auto&, const auto&, const auto& data) { received_0.push_back(data); });
    interfaces->subscribe_var(
        "evse_manager", "call_update_allowed_energy_transfer_modes", 1,
        [&received_1](const auto&, const auto&, const auto& data) { received_1.push_back(data); });

    const std::vector<EnergyTransferModeEnum> allowed_energy_transfer_modes{EnergyTransferModeEnum::AC_single_phase,
                                                                            EnergyTransferModeEnum::AC_three_phase};
    ocpp::CiString<36> transaction_id{"1234567890"};

    // start a transaction
    ProvidedIdToken token;
    token.id_token = {"ABCDEF", IdTokenType::Local};
    token.authorization_type = AuthorizationType::RFID;

    const IdToken id_token{"ABCDEF", "Local"};

    SessionStarted started{};
    started.reason = StartSessionReason::Authorized;
    started.id_tag = token;

    types::evse_manager::SessionEvent session_event;
    session_event.uuid = transaction_id;
    session_event.timestamp = DateTime{"2026-01-01T00:00:00Z"};
    session_event.event = SessionEventEnum::SessionStarted;
    session_event.connector_id = 1;
    session_event.session_started = started;
    // std::optional<types::evse_manager::SessionFinished> session_finished;
    // std::optional<types::evse_manager::TransactionStarted> transaction_started;
    // std::optional<types::evse_manager::TransactionFinished> transaction_finished;
    // std::optional<types::evse_manager::ChargingPausedEVSEReasons> charging_paused_evse;
    // std::optional<types::evse_manager::ChargingStateChangedEvent> charging_state_changed_event;
    // std::optional<types::evse_manager::AuthorizationEvent> authorization_event;
    // std::optional<types::evse_manager::EnableDisableSource> source;

    EXPECT_CALL(chargepoint, on_authorized(1, 1, id_token)).Times(1);

    // - Accepted
    // - IncompatibleEnergyTransfer
    // - ServiceRenegotiationFailed
    // - NoHlc
    interfaces->add_cmd_result(R"("Accepted")"_json);

    ocpp->process_session_event(1, session_event);

    const auto result_A = ocpp->cb_update_allowed_energy_transfer_modes(allowed_energy_transfer_modes, transaction_id);
    transaction_id = "0123456789"; // no transaction
    const auto result_B = ocpp->cb_update_allowed_energy_transfer_modes(allowed_energy_transfer_modes, transaction_id);

    ASSERT_EQ(received_0.size(), 1);
    EXPECT_EQ(received_0[0],
              R"({"allowed_energy_transfer_modes":["AC_single_phase_core","AC_three_phase_core"]})"_json);

    ASSERT_EQ(received_1.size(), 0);

    EXPECT_TRUE(result_A);
    EXPECT_FALSE(result_B);
}

// ----------------------------------------------------------------------------
// vars

TEST_F(GenericOcppRequiresTester, subscribeEvInfo) {
    // subscribe_ev_info() calls cb_ev_info()

    types::evse_manager::EVInfo ev_info;
    ev_info.soc = 57.5;
    ev_info.evcc_id = "1122334455667788";

    // used:
    // std::optional<float> soc;
    // std::optional<std::string> evcc_id;

    interfaces->publish(0, "ev_info", ev_info);

    EXPECT_EQ(ocpp->evse_evcc_id().handle()->at(1), ev_info.evcc_id);
    EXPECT_EQ(ocpp->evse_soc_map().handle()->at(1), ev_info.soc);

    ev_info.soc = 12.48;
    ev_info.evcc_id = "AA223344556677ZZ";
    interfaces->publish(1, "ev_info", ev_info);

    // note evse_id is index + 1
    // - publish uses index
    EXPECT_EQ(ocpp->evse_evcc_id().handle()->at(2), ev_info.evcc_id);
    EXPECT_EQ(ocpp->evse_soc_map().handle()->at(2), ev_info.soc);
}

TEST_F(GenericOcppRequiresTester, subscribeHwCapabilities) {
    // subscribe_hw_capabilities() calls cb_hw_capabilities()

    types::evse_board_support::HardwareCapabilities hw_capabilities;
    hw_capabilities.max_current_A_import = 45.5;
    hw_capabilities.min_current_A_import = 8.;
    hw_capabilities.max_phase_count_import = 3;
    hw_capabilities.min_phase_count_import = 1;
    hw_capabilities.max_current_A_export = 0.;
    hw_capabilities.min_current_A_export = 0.;
    hw_capabilities.max_phase_count_export = 0;
    hw_capabilities.min_phase_count_export = 0;
    hw_capabilities.supports_changing_phases_during_charging = true;
    hw_capabilities.supports_cp_state_E = true;
    hw_capabilities.connector_type = types::evse_board_support::Connector_type::IEC62196Type2Socket;
    hw_capabilities.max_plug_temperature_C = 60;

    interfaces->publish(0, "hw_capabilities", hw_capabilities);

    const auto& evse_hardware_capabilities_map = ocpp->evse_hardware_capabilities_map();

    ASSERT_FALSE(evse_hardware_capabilities_map.empty());
    EXPECT_EQ(evse_hardware_capabilities_map.at(1), hw_capabilities);

    hw_capabilities.max_current_A_export = 80.;
    hw_capabilities.min_current_A_export = 40.;
    hw_capabilities.max_phase_count_export = 3;
    hw_capabilities.min_phase_count_export = 3;
    hw_capabilities.supports_changing_phases_during_charging = false;
    hw_capabilities.supports_cp_state_E = false;
    interfaces->publish(1, "hw_capabilities", hw_capabilities);

    // note evse_id is index + 1
    // - publish uses index
    EXPECT_EQ(ocpp->evse_hardware_capabilities_map().at(2), hw_capabilities);
}

TEST_F(GenericOcppRequiresTester, subscribePowermeter) {
    // subscribe_powermeter() calls cb_powermeter()

    using ocpp::DateTime;
    using ocpp::v2::LocationEnum;
    using ocpp::v2::MeasurandEnum;
    using ocpp::v2::ReadingContextEnum;
    using types::units::Energy;

    DateTime now{"2026-01-01T00:00:00Z"};
    DateTime now_next{"2026-01-01T00:10:00Z"};
    types::powermeter::Powermeter power_meter_0;
    power_meter_0.timestamp = now;
    power_meter_0.energy_Wh_import = {10.};
    // std::string timestamp;
    // types::units::Energy energy_Wh_import;
    // std::optional<std::string> meter_id;
    // std::optional<bool> phase_seq_error;
    // std::optional<types::units::Energy> energy_Wh_export;
    // std::optional<types::units::Power> power_W;
    // std::optional<types::units::Voltage> voltage_V;
    // std::optional<types::units::ReactivePower> VAR;
    // std::optional<types::units::Current> current_A;
    // std::optional<types::units::Frequency> frequency_Hz;
    // std::optional<types::units_signed::Energy> energy_Wh_import_signed;
    // std::optional<types::units_signed::Energy> energy_Wh_export_signed;
    // std::optional<types::units_signed::Power> power_W_signed;
    // std::optional<types::units_signed::Voltage> voltage_V_signed;
    // std::optional<types::units_signed::ReactivePower> VAR_signed;
    // std::optional<types::units_signed::Current> current_A_signed;
    // std::optional<types::units_signed::Frequency> frequency_Hz_signed;
    // std::optional<types::units_signed::SignedMeterValue> signed_meter_value;
    // std::optional<std::vector<types::temperature::Temperature>> temperatures;

    types::powermeter::Powermeter power_meter_1;
    power_meter_1.timestamp = now_next;
    power_meter_1.energy_Wh_import = {30.5};

    EXPECT_CALL(chargepoint, on_meter_value(1, _, power_meter_0)).Times(1);
    EXPECT_CALL(chargepoint, on_meter_value(1, _, power_meter_1)).Times(1);

    interfaces->publish(0, "powermeter", power_meter_0);
    interfaces->publish(0, "powermeter", power_meter_1);
}

TEST_F(GenericOcppRequiresTester, subscribePowermeterPublicKeyOcmf) {
    // subscribe_powermeter_public_key_ocmf() calls cb_powermeter_public_key_ocmf()

    EXPECT_CALL(chargepoint, set_powermeter_public_key(1, "PUBLIC_KEY_1")).WillOnce(Return(true));
    EXPECT_CALL(chargepoint, set_powermeter_public_key(2, "PUBLIC_KEY_2")).WillOnce(Return(true));

    interfaces->publish(0, "powermeter_public_key_ocmf", R"("PUBLIC_KEY_1")"_json);
    interfaces->publish(1, "powermeter_public_key_ocmf", R"("PUBLIC_KEY_2")"_json);
}

TEST_F(GenericOcppRequiresTester, subscribeSessionEvent) {
    // subscribe_session_event() calls cb_session_event() ...

    using ocpp::DateTime;
    using ocpp::v2::EnergyTransferModeEnum;
    using ocpp::v2::IdToken;
    using types::authorization::AuthorizationType;
    using types::authorization::IdTokenType;
    using types::authorization::ProvidedIdToken;
    using types::evse_manager::SessionEvent;
    using types::evse_manager::SessionEventEnum;
    using types::evse_manager::SessionStarted;
    using types::evse_manager::StartSessionReason;

    ProvidedIdToken token;
    token.id_token = {"ABCDEF", IdTokenType::Local};
    token.authorization_type = AuthorizationType::RFID;

    const IdToken id_token{"ABCDEF", "Local"};

    SessionStarted started{};
    started.reason = StartSessionReason::Authorized;
    started.id_tag = token;

    SessionEvent session_event;
    session_event.uuid = "66885522";
    session_event.timestamp = DateTime{"2026-01-01T00:00:00Z"};
    session_event.event = SessionEventEnum::SessionStarted;
    session_event.connector_id = 1;
    session_event.session_started = started;
    // std::optional<types::evse_manager::SessionFinished> session_finished;
    // std::optional<types::evse_manager::TransactionStarted> transaction_started;
    // std::optional<types::evse_manager::TransactionFinished> transaction_finished;
    // std::optional<types::evse_manager::ChargingPausedEVSEReasons> charging_paused_evse;
    // std::optional<types::evse_manager::ChargingStateChangedEvent> charging_state_changed_event;
    // std::optional<types::evse_manager::AuthorizationEvent> authorization_event;
    // std::optional<types::evse_manager::EnableDisableSource> source;

    EXPECT_CALL(chargepoint, on_authorized(1, 1, id_token)).Times(1);

    interfaces->publish(0, "session_event", session_event);
}

TEST_F(GenericOcppRequiresTester, subscribeSupportedEnergyTransferModes) {
    // subscribe_supported_energy_transfer_modes() calls
    // cb_supported_energy_transfer_modes()

    using types::iso15118::EnergyTransferMode;

    const std::vector<EnergyTransferMode> modes_0{EnergyTransferMode::DC, EnergyTransferMode::DC_BPT};
    const std::vector<EnergyTransferMode> modes_1{EnergyTransferMode::AC_three_phase_core,
                                                  EnergyTransferMode::AC_single_phase_core};

    const auto modes_json_0 = R"(["DC","DC_BPT"])"_json;
    const auto modes_json_1 = R"(["AC_three_phase_core","AC_single_phase_core"])"_json;

    interfaces->publish(0, "supported_energy_transfer_modes", modes_json_0);
    interfaces->publish(1, "supported_energy_transfer_modes", modes_json_1);

    const auto& energy_transfer_modes = ocpp->evse_supported_energy_transfer_modes();
    EXPECT_EQ(energy_transfer_modes.at(1), modes_0);
    EXPECT_EQ(energy_transfer_modes.at(2), modes_1);
}

} // namespace
