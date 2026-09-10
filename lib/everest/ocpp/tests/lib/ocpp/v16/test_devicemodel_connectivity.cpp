// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_devicemodel_connectivity.cpp
/// \brief Unit tests for the device-model-backed v16 connectivity overrides on
/// ocpp::v16::ChargePointConfigurationDeviceModel.
///
/// Unlike the JSON backend (single slot-1 profile, hardcoded ocppInterface Any, slot-ignoring websocket options),
/// the device-model adapter sources per-slot connection details from the v2 device-model NetworkConfiguration[N]
/// components; the same data 2.x uses.
/// These tests exercise the four overrides against a real sqlite-backed ocpp::v2::DeviceModel seeded with the shipped
/// example component config (which carries NetworkConfiguration_1 / _2 slots):
///   * get_network_configuration_priority() - DM CSL, 2.x-version filtering, empty->active-slot fallback,
///   * read_network_connection_profile(slot) - configured slot, legacy-synthesis fallback, version filter, nullopt,
///   * get_websocket_connection_options(slot) - per-slot fields + identity/password fallbacks + base equivalence
///     + nullopt rejections (':' in identity, URL scheme vs security profile),
///   * set_active_network_profile_slot(slot) - persistence + getter redirection,
///   * a ConnectivityManager smoke test built over the adapter, and
///   * the ocpp16 component-config patcher pinning NetworkConfiguration[slot].OcppInterface to "Any".

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/common/websocket/websocket_uri.hpp>
#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_configuration_devicemodel.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp16_component_config_patcher.hpp>
#include <ocpp/v2/ocpp_enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>

#include "evse_security_mock.hpp"
#include "ocpp16_test_config.hpp"
#include "v2/device_model_test_helper.hpp"

using ::testing::NiceMock;

namespace ocpp {
namespace v16 {
namespace {

namespace NC = ocpp::v2::NetworkConfigurationComponentVariables;
namespace CC = ocpp::v2::ControllerComponentVariables;

using ocpp::v2::AttributeEnum;
using ocpp::v2::OCPPInterfaceEnum;
using ocpp::v2::OCPPTransportEnum;
using ocpp::v2::SetVariableStatusEnum;

/// \brief Non-owning forwarding proxy for a v2::DeviceModelInterface. The ChargePointConfigurationDeviceModel
/// constructor takes ownership of a unique_ptr<DeviceModelInterface>, but the tests need to keep a raw handle to
/// the underlying DeviceModel to seed per-slot NetworkConfiguration values. The proxy is handed to the config
/// (single owner) while the test retains the real DeviceModel* owned by DeviceModelTestHelper.
class DeviceModelProxy : public ocpp::v2::DeviceModelInterface {
public:
    using VariableAttribute = ocpp::v2::VariableAttribute;
    using Component = ocpp::v2::Component;
    using ComponentVariable = ocpp::v2::ComponentVariable;
    using ComponentCriterionEnum = ocpp::v2::ComponentCriterionEnum;
    using Variable = ocpp::v2::Variable;
    using GetVariableStatusEnum = ocpp::v2::GetVariableStatusEnum;
    using VariableMonitoringMeta = ocpp::v2::VariableMonitoringMeta;
    using SetMonitoringData = ocpp::v2::SetMonitoringData;
    using SetMonitoringResult = ocpp::v2::SetMonitoringResult;
    using VariableMonitorType = ocpp::v2::VariableMonitorType;
    using VariableMonitoringPeriodic = ocpp::v2::VariableMonitoringPeriodic;
    using MonitoringCriterionEnum = ocpp::v2::MonitoringCriterionEnum;
    using MonitoringData = ocpp::v2::MonitoringData;
    using ClearMonitoringResult = ocpp::v2::ClearMonitoringResult;
    using MutabilityEnum = ocpp::v2::MutabilityEnum;
    using VariableCharacteristics = ocpp::v2::VariableCharacteristics;
    using VariableMetaData = ocpp::v2::VariableMetaData;
    using ReportData = ocpp::v2::ReportData;
    using ReportBaseEnum = ocpp::v2::ReportBaseEnum;

    explicit DeviceModelProxy(ocpp::v2::DeviceModelInterface& storage) : storage(storage) {
    }

    GetVariableStatusEnum get_variable(const Component& component_id, const Variable& variable_id,
                                       const AttributeEnum& attribute_enum, std::string& value,
                                       bool allow_write_only) const override {
        return storage.get_variable(component_id, variable_id, attribute_enum, value, allow_write_only);
    }

    SetVariableStatusEnum set_value(const Component& component_id, const Variable& variable_id,
                                    const AttributeEnum& attribute_enum, const std::string& value,
                                    const std::string& source, bool allow_read_only) override {
        return storage.set_value(component_id, variable_id, attribute_enum, value, source, allow_read_only);
    }

    SetVariableStatusEnum set_read_only_value(const Component& component_id, const Variable& variable_id,
                                              const AttributeEnum& attribute_enum, const std::string& value,
                                              const std::string& source) override {
        return storage.set_read_only_value(component_id, variable_id, attribute_enum, value, source);
    }

    SetVariableStatusEnum clear_value(const Component& component_id, const Variable& variable_id,
                                      const AttributeEnum& attribute_enum, const std::string& source) override {
        return storage.clear_value(component_id, variable_id, attribute_enum, source);
    }

    std::optional<MutabilityEnum> get_mutability(const Component& component_id, const Variable& variable_id,
                                                 const AttributeEnum& attribute_enum) override {
        return storage.get_mutability(component_id, variable_id, attribute_enum);
    }

    std::optional<VariableMetaData> get_variable_meta_data(const Component& component_id,
                                                           const Variable& variable_id) override {
        return storage.get_variable_meta_data(component_id, variable_id);
    }

    std::vector<ReportData> get_base_report_data(const ReportBaseEnum& report_base) override {
        return storage.get_base_report_data(report_base);
    }

    std::vector<ReportData>
    get_custom_report_data(const std::optional<std::vector<ComponentVariable>>& component_variables,
                           const std::optional<std::vector<ComponentCriterionEnum>>& component_criteria) override {
        return storage.get_custom_report_data(component_variables, component_criteria);
    }

    std::vector<SetMonitoringResult>
    set_monitors(const std::vector<SetMonitoringData>& requests,
                 const VariableMonitorType type = VariableMonitorType::CustomMonitor) override {
        return storage.set_monitors(requests, type);
    }

    bool update_monitor_reference(std::int32_t monitor_id, const std::string& reference_value) override {
        return storage.update_monitor_reference(monitor_id, reference_value);
    }

    std::vector<VariableMonitoringPeriodic> get_periodic_monitors() override {
        return storage.get_periodic_monitors();
    }

    std::vector<MonitoringData> get_monitors(const std::vector<MonitoringCriterionEnum>& criteria,
                                             const std::vector<ComponentVariable>& component_variables) override {
        return storage.get_monitors(criteria, component_variables);
    }

    std::vector<ClearMonitoringResult> clear_monitors(const std::vector<int>& request_ids,
                                                      bool allow_protected) override {
        return storage.clear_monitors(request_ids, allow_protected);
    }

    std::int32_t clear_custom_monitors() override {
        return storage.clear_custom_monitors();
    }

    void register_variable_listener(
        std::function<void(const std::unordered_map<std::int64_t, VariableMonitoringMeta>& monitors,
                           const Component& component, const Variable& variable,
                           const VariableCharacteristics& characteristics, const VariableAttribute& attribute,
                           const std::string& value_previous, const std::string& value_current)>&& listener) override {
        return storage.register_variable_listener(std::move(listener));
    }

    void register_monitor_listener(
        std::function<void(const VariableMonitoringMeta& updated_monitor, const Component& component,
                           const Variable& variable, const VariableCharacteristics& characteristics,
                           const VariableAttribute& attribute, const std::string& current_value)>&& listener) override {
        return storage.register_monitor_listener(std::move(listener));
    }

    void check_integrity(const std::map<std::int32_t, std::int32_t>& evse_connector_structure) override {
        return storage.check_integrity(evse_connector_structure);
    }

private:
    ocpp::v2::DeviceModelInterface& storage;
};

/// \brief Build a fully-specified NetworkConnectionProfile for a slot (URL + security profile + interface).
ocpp::v2::NetworkConnectionProfile make_slot_profile(const std::string& url, int security_profile,
                                                     OCPPInterfaceEnum iface) {
    ocpp::v2::NetworkConnectionProfile p;
    p.ocppCsmsUrl = url;
    p.securityProfile = security_profile;
    p.ocppInterface = iface;
    p.ocppTransport = OCPPTransportEnum::JSON;
    p.messageTimeout = 30;
    return p;
}

// ---------------------------------------------------------------------------
// Fixture: real in-memory sqlite v2 DeviceModel over the shipped example
// component config (InternalCtrlr, OCPPCommCtrlr, SecurityCtrlr,
// NetworkConfiguration_1 / _2). The ChargePointConfigurationDeviceModel under
// test owns a forwarding proxy; the fixture keeps the raw DeviceModel* to seed
// per-slot values.
// ---------------------------------------------------------------------------
class DeviceModelConnectivityTest : public ::testing::Test {
protected:
    ocpp::v2::DeviceModelTestHelper dm_helper;
    ocpp::v2::DeviceModel* dm{nullptr};
    std::unique_ptr<ChargePointConfigurationDeviceModel> config;

    DeviceModelConnectivityTest() : dm_helper() {
        dm = dm_helper.get_device_model();
        config = std::make_unique<ChargePointConfigurationDeviceModel>(CONFIG_DIR_V16,
                                                                       std::make_unique<DeviceModelProxy>(*dm));
    }

    void write_slot_profile(int slot, const ocpp::v2::NetworkConnectionProfile& profile) {
        ASSERT_TRUE(NC::write_profile_to_device_model(*dm, slot, profile, "test"))
            << "seeding NetworkConfiguration[" << slot << "] failed";
    }

    void set_priority(const std::string& csl) {
        ASSERT_TRUE(CC::NetworkConfigurationPriority.variable.has_value());
        ASSERT_EQ(dm->set_value(CC::NetworkConfigurationPriority.component,
                                CC::NetworkConfigurationPriority.variable.value(), AttributeEnum::Actual, csl, "test"),
                  SetVariableStatusEnum::Accepted)
            << "setting NetworkConfigurationPriority to '" << csl << "' was rejected";
    }

    void set_slot_ocpp_version(int slot, const std::string& version) {
        ASSERT_EQ(dm->set_value(NC::get_component_variable(slot, NC::OcppVersion).component,
                                NC::get_component_variable(slot, NC::OcppVersion).variable.value(),
                                AttributeEnum::Actual, version, "test"),
                  SetVariableStatusEnum::Accepted)
            << "setting NetworkConfiguration[" << slot << "].OcppVersion to '" << version << "' was rejected";
    }

    void set_slot_identity(int slot, const std::string& identity) {
        ASSERT_EQ(dm->set_value(NC::get_component_variable(slot, NC::Identity).component,
                                NC::get_component_variable(slot, NC::Identity).variable.value(), AttributeEnum::Actual,
                                identity, "test"),
                  SetVariableStatusEnum::Accepted);
    }

    void set_slot_hostname(int slot, const std::string& hostname) {
        const auto cv = NC::get_component_variable(slot, ocpp::v2::Variable{"HostName"});
        ASSERT_TRUE(cv.variable.has_value());
        // HostName is ReadOnly in the component config; seed it the way write_profile_to_device_model writes
        // profile fields (forced set_value).
        ASSERT_EQ(dm->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual, hostname, "test", true),
                  SetVariableStatusEnum::Accepted);
    }
};

// Compare two WebsocketConnectionOptions field-by-field. Uri::string() is non-const, so parameters are taken by
// value.
void expect_ws_options_equal(WebsocketConnectionOptions a, WebsocketConnectionOptions b) {
    EXPECT_EQ(a.ocpp_versions, b.ocpp_versions);
    EXPECT_EQ(a.csms_uri.string(), b.csms_uri.string());
    EXPECT_EQ(a.security_profile, b.security_profile);
    EXPECT_EQ(a.authorization_key, b.authorization_key);
    EXPECT_EQ(a.message_timeout, b.message_timeout);
    EXPECT_EQ(a.retry_backoff_random_range_s, b.retry_backoff_random_range_s);
    EXPECT_EQ(a.retry_backoff_repeat_times, b.retry_backoff_repeat_times);
    EXPECT_EQ(a.retry_backoff_wait_minimum_s, b.retry_backoff_wait_minimum_s);
    EXPECT_EQ(a.max_connection_attempts, b.max_connection_attempts);
    EXPECT_EQ(a.supported_ciphers_12, b.supported_ciphers_12);
    EXPECT_EQ(a.supported_ciphers_13, b.supported_ciphers_13);
    EXPECT_EQ(a.ping_interval_s, b.ping_interval_s);
    EXPECT_EQ(a.ping_payload, b.ping_payload);
    EXPECT_EQ(a.pong_timeout_s, b.pong_timeout_s);
    EXPECT_EQ(a.use_ssl_default_verify_paths, b.use_ssl_default_verify_paths);
    EXPECT_EQ(a.additional_root_certificate_check, b.additional_root_certificate_check);
    EXPECT_EQ(a.hostName, b.hostName);
    EXPECT_EQ(a.verify_csms_common_name, b.verify_csms_common_name);
    EXPECT_EQ(a.use_tpm_tls, b.use_tpm_tls);
    EXPECT_EQ(a.verify_csms_allow_wildcards, b.verify_csms_allow_wildcards);
    EXPECT_EQ(a.iface, b.iface);
    EXPECT_EQ(a.enable_tls_keylog, b.enable_tls_keylog);
    EXPECT_EQ(a.keylog_file, b.keylog_file);
    EXPECT_EQ(a.everest_version, b.everest_version);
}

// ---------------------------------------------------------------------------
// get_network_configuration_priority()
// ---------------------------------------------------------------------------

// An empty NetworkConfigurationPriority yields no usable slots, so the priority falls back to the active slot as a
// single-element list.
TEST_F(DeviceModelConnectivityTest, PriorityDefaultsToActiveSlotWhenDmEmpty) {
    config->set_active_network_profile_slot(2, "test");
    set_priority("");

    EXPECT_EQ(config->get_network_configuration_priority(), "2");
}

// The device-model CSL is exposed verbatim when every listed slot is usable for OCPP 1.6 (OcppVersion unset).
TEST_F(DeviceModelConnectivityTest, PriorityExposesDeviceModelCsl) {
    set_priority("1,2");

    EXPECT_EQ(config->get_network_configuration_priority(), "1,2");
}

// Slots pinned to a 2.x-only OcppVersion are filtered out of the priority list; when all listed slots are filtered
// the result falls back to the active slot.
TEST_F(DeviceModelConnectivityTest, PriorityFiltersTwoXOnlySlots) {
    // Slot 2 is pinned to a 2.x-only version and must be dropped, leaving slot 1.
    set_priority("1,2");
    set_slot_ocpp_version(2, "OCPP201");
    EXPECT_EQ(config->get_network_configuration_priority(), "1");

    // All listed slots filtered -> fall back to the active slot (1).
    config->set_active_network_profile_slot(1, "test");
    set_priority("2");
    EXPECT_EQ(config->get_network_configuration_priority(), "1");
}

// ---------------------------------------------------------------------------
// read_network_connection_profile(slot)
// ---------------------------------------------------------------------------

// An unconfigured ACTIVE slot (default slot 1 has an empty OcppCsmsUrl) falls back to the legacy single-profile
// synthesis: byte-equivalent to the base ChargePointConfigurationConnectivity behavior (Any / JSON / 10 s + global
// URL/identity/password getters).
TEST_F(DeviceModelConnectivityTest, ReadProfileFallsBackToLegacySynthesisWhenUnconfigured) {
    // Active slot defaults to 1; do not seed slot 1 so read_profile_from_device_model returns nullopt.
    const auto fallback = config->read_network_connection_profile(1);
    ASSERT_TRUE(fallback.has_value()) << "Unconfigured active slot must fall back to legacy synthesis";

    // Constant parts of the legacy synthesis.
    EXPECT_EQ(fallback->ocppInterface, OCPPInterfaceEnum::Any);
    EXPECT_EQ(fallback->ocppTransport, OCPPTransportEnum::JSON);
    EXPECT_EQ(fallback->messageTimeout, 10);

    // Byte-equivalence with the global getters.
    EXPECT_EQ(fallback->ocppCsmsUrl.get(), config->getCentralSystemURI());
    EXPECT_EQ(fallback->securityProfile, config->getSecurityProfile());
    if (const auto id = config->getChargePointId(); !id.empty()) {
        ASSERT_TRUE(fallback->identity.has_value());
        EXPECT_EQ(fallback->identity->get(), id);
    }

    // The override result must be identical to the base-class synthesis, field for field.
    const auto base = config->ChargePointConfigurationConnectivity::read_network_connection_profile(1);
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(fallback->ocppCsmsUrl.get(), base->ocppCsmsUrl.get());
    EXPECT_EQ(fallback->securityProfile, base->securityProfile);
    EXPECT_EQ(fallback->ocppInterface, base->ocppInterface);
    EXPECT_EQ(fallback->ocppTransport, base->ocppTransport);
    EXPECT_EQ(fallback->messageTimeout, base->messageTimeout);
    EXPECT_EQ(fallback->identity.has_value(), base->identity.has_value());
    if (fallback->identity.has_value() && base->identity.has_value()) {
        EXPECT_EQ(fallback->identity->get(), base->identity->get());
    }
    EXPECT_EQ(fallback->basicAuthPassword.has_value(), base->basicAuthPassword.has_value());
    if (fallback->basicAuthPassword.has_value() && base->basicAuthPassword.has_value()) {
        EXPECT_EQ(fallback->basicAuthPassword->get(), base->basicAuthPassword->get());
    }
}

// A configured (non-active) slot is returned verbatim, including the interface selection and APN sub-fields.
TEST_F(DeviceModelConnectivityTest, ReadProfileReturnsConfiguredSlot) {
    auto profile = make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0);
    ocpp::v2::APN apn;
    apn.apn = "internet";
    apn.apnAuthentication = ocpp::v2::APNAuthenticationEnum::AUTO;
    apn.apnUserName = "user";
    apn.apnPassword = "pass";
    profile.apn = apn;
    write_slot_profile(2, profile);

    const auto result = config->read_network_connection_profile(2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ocppCsmsUrl.get(), "wss://slot2.example.com/ocpp");
    EXPECT_EQ(result->ocppInterface, OCPPInterfaceEnum::Wireless0);
    ASSERT_TRUE(result->apn.has_value());
    EXPECT_EQ(result->apn->apn, "internet");
    EXPECT_EQ(result->apn->apnUserName, "user");
    EXPECT_EQ(result->apn->apnPassword, "pass");
}

// A configured slot pinned to a 2.x-only OcppVersion is filtered out and reads as nullopt.
TEST_F(DeviceModelConnectivityTest, ReadProfileFiltersOcppVersion) {
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));
    set_slot_ocpp_version(2, "OCPP201");

    EXPECT_FALSE(config->read_network_connection_profile(2).has_value())
        << "A 2.x-pinned slot must not be usable for OCPP 1.6";
}

// Regression: a 2.x-only OcppVersion pin on the active slot must not dead-end the charge point; the read falls
// back to the legacy synthesis rather than returning nullopt.
TEST_F(DeviceModelConnectivityTest, ReadProfilePinnedActiveSlotFallsBackToLegacySynthesis) {
    write_slot_profile(1, make_slot_profile("wss://active.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));
    config->set_active_network_profile_slot(1, "test");
    set_slot_ocpp_version(1, "OCPP201");

    const auto result = config->read_network_connection_profile(1);
    ASSERT_TRUE(result.has_value()) << "Pinned active slot must fall back to legacy synthesis, not dead-end";

    // Synthesis (Any / JSON / 10 s), not the stored per-slot profile (which was Wireless0).
    EXPECT_EQ(result->ocppInterface, OCPPInterfaceEnum::Any);
    EXPECT_EQ(result->ocppTransport, OCPPTransportEnum::JSON);
    EXPECT_EQ(result->messageTimeout, 10);

    const auto base = config->ChargePointConfigurationConnectivity::read_network_connection_profile(1);
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(result->ocppCsmsUrl.get(), base->ocppCsmsUrl.get());
    EXPECT_EQ(result->securityProfile, base->securityProfile);
}

// An unconfigured NON-active slot reads as nullopt (no legacy fallback - that only applies to the active slot).
TEST_F(DeviceModelConnectivityTest, ReadProfileNullForUnconfiguredNonActiveSlot) {
    // Active slot is 1 by default; slot 2 has no configured URL.
    EXPECT_FALSE(config->read_network_connection_profile(2).has_value());
}

// ---------------------------------------------------------------------------
// get_websocket_connection_options(slot)
// ---------------------------------------------------------------------------

// A configured slot yields per-slot uri/securityProfile/identity/password/hostname; remaining fields come from the
// global getters, including iface == getIFace().
TEST_F(DeviceModelConnectivityTest, WsOptionsPerSlotFields) {
    auto profile = make_slot_profile("ws://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0);
    profile.identity = "slot2id";
    profile.basicAuthPassword = ocpp::CiString<64>("Slot2PasswordSlot2Pw");
    write_slot_profile(2, profile);
    set_slot_hostname(2, "slot2.host");

    auto opts = config->get_websocket_connection_options(2); // non-const: Uri accessors are non-const
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->security_profile, 1);
    EXPECT_EQ(opts->csms_uri.get_chargepoint_id(), "slot2id") << "per-slot Identity must drive the websocket URI";
    EXPECT_EQ(opts->csms_uri.get_hostname(), "slot2.example.com");
    ASSERT_TRUE(opts->authorization_key.has_value());
    EXPECT_EQ(opts->authorization_key.value(), "Slot2PasswordSlot2Pw");
    ASSERT_TRUE(opts->hostName.has_value());
    EXPECT_EQ(opts->hostName.value(), "slot2.host");
    EXPECT_EQ(opts->message_timeout, std::chrono::seconds(30))
        << "per-slot NetworkConfiguration[N].MessageTimeout must drive the message timeout";
    EXPECT_EQ(opts->iface, config->getIFace()) << "static iface default must come from Internal/IFace (getIFace())";
}

// Per-slot options honor OCPPCommCtrlr/NetworkProfileConnectionAttempts so the websocket gives up on an
// unreachable CSMS and the ConnectivityManager can advance to the next priority slot (multi-slot failover).
TEST_F(DeviceModelConnectivityTest, WsOptionsUseNetworkProfileConnectionAttempts) {
    write_slot_profile(2, make_slot_profile("ws://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));

    ASSERT_TRUE(CC::NetworkProfileConnectionAttempts.variable.has_value());
    ASSERT_EQ(dm->set_value(CC::NetworkProfileConnectionAttempts.component,
                            CC::NetworkProfileConnectionAttempts.variable.value(), AttributeEnum::Actual, "5", "test"),
              SetVariableStatusEnum::Accepted);

    auto opts = config->get_websocket_connection_options(2); // non-const: Uri accessors are non-const
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->max_connection_attempts, 5)
        << "device-model slots must use a finite attempt count, not the legacy unlimited (-1)";
}

// A stripped component config without OCPPCommCtrlr/NetworkProfileConnectionAttempts falls back to a finite
// default (3, the shipped config value) instead of -1 (retry forever), which would silently disable
// websocket-failure-driven failover.
TEST_F(DeviceModelConnectivityTest, WsOptionsDefaultNetworkProfileConnectionAttemptsWhenAbsent) {
    write_slot_profile(2, make_slot_profile("ws://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));

    ASSERT_TRUE(CC::NetworkProfileConnectionAttempts.variable.has_value());
    ASSERT_EQ(dm->clear_value(CC::NetworkProfileConnectionAttempts.component,
                              CC::NetworkProfileConnectionAttempts.variable.value(), AttributeEnum::Actual, "test"),
              SetVariableStatusEnum::Accepted)
        << "fixture precondition: clearing NetworkProfileConnectionAttempts must succeed";

    auto opts = config->get_websocket_connection_options(2); // non-const: Uri accessors are non-const
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->max_connection_attempts, 3);
}

// When a configured slot has no per-slot Identity, the websocket identity falls back to SecurityCtrlr/Identity.
TEST_F(DeviceModelConnectivityTest, WsOptionsPerSlotFallsBackToSecurityCtrlrIdentity) {
    // Configure slot 2 with a URL but leave the per-slot Identity empty (default).
    write_slot_profile(2, make_slot_profile("ws://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));

    const auto expected_identity = dm->get_value<std::string>(CC::SecurityCtrlrIdentity);
    ASSERT_FALSE(expected_identity.empty()) << "fixture precondition: SecurityCtrlr/Identity must be set";

    auto opts = config->get_websocket_connection_options(2); // non-const: Uri accessors are non-const
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->csms_uri.get_chargepoint_id(), expected_identity);
}

// An unconfigured slot delegates to the base ChargePointConfigurationConnectivity implementation, producing options
// identical to the base (which ignores the slot and uses the global getters of the active slot) - except for
// max_connection_attempts, where the base's -1 (retry forever) would disable failover to the other slots.
TEST_F(DeviceModelConnectivityTest, WsOptionsFallbackEqualsBaseExceptFiniteAttempts) {
    // Make the active slot (1) configured with a valid URL + identity so the base impl produces a real result.
    auto active = make_slot_profile("ws://active.example.com/ocpp", 1, OCPPInterfaceEnum::Any);
    active.identity = "cp001";
    write_slot_profile(1, active);

    // Slot 3 is unconfigured -> override delegates to base.
    const auto via_override = config->get_websocket_connection_options(3);
    auto via_base = config->ChargePointConfigurationConnectivity::get_websocket_connection_options(3);
    ASSERT_EQ(via_override.has_value(), via_base.has_value());
    ASSERT_TRUE(via_override.has_value()) << "base synthesis over a configured active slot must yield options";
    EXPECT_EQ(via_base->max_connection_attempts, -1) << "precondition: the base still retries forever";
    EXPECT_EQ(via_override->max_connection_attempts, 3)
        << "fallback options must use OCPPCommCtrlr/NetworkProfileConnectionAttempts (shipped default 3), not -1";
    via_base->max_connection_attempts = via_override->max_connection_attempts;
    expect_ws_options_equal(via_override.value(), via_base.value());
}

// A ':' in the resolved identity is invalid in the basic-auth user; the options read must yield nullopt so the
// ConnectivityManager falls back to another profile instead of attempting a doomed connect.
TEST_F(DeviceModelConnectivityTest, WsOptionsNulloptForIdentityWithColon) {
    auto profile = make_slot_profile("ws://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0);
    profile.identity = "cp:001";
    write_slot_profile(2, profile);

    EXPECT_FALSE(config->get_websocket_connection_options(2).has_value())
        << "an identity containing ':' must not produce connection options";
}

// A per-slot URL whose scheme contradicts the slot's security profile (insecure ws:// with a TLS profile) fails
// Uri::parse_and_validate; the options read must yield nullopt instead of letting the exception escape.
TEST_F(DeviceModelConnectivityTest, WsOptionsNulloptForSchemeSecurityProfileMismatch) {
    write_slot_profile(2, make_slot_profile("ws://slot2.example.com/ocpp", 2, OCPPInterfaceEnum::Wireless0));

    EXPECT_FALSE(config->get_websocket_connection_options(2).has_value())
        << "an insecure ws:// URL with a TLS security profile must not produce connection options";
}

// ---------------------------------------------------------------------------
// set_active_network_profile_slot(slot)
// ---------------------------------------------------------------------------

// Persisting the active slot must redirect the active-slot getters (getCentralSystemURI) to that slot's URL.
TEST_F(DeviceModelConnectivityTest, SetActiveNetworkProfileSlotPersistsAndRedirectsGetters) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 1, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));

    config->set_active_network_profile_slot(2, "test");

    // The persisted ActiveNetworkProfile is observable and drives the active-slot getters.
    EXPECT_EQ(dm->get_value<int>(CC::ActiveNetworkProfile), 2);
    EXPECT_EQ(config->getCentralSystemURI(), "wss://slot2.example.com/ocpp");

    config->set_active_network_profile_slot(1, "test");
    EXPECT_EQ(dm->get_value<int>(CC::ActiveNetworkProfile), 1);
    EXPECT_EQ(config->getCentralSystemURI(), "wss://slot1.example.com/ocpp");
}

// ---------------------------------------------------------------------------
// ConnectivityManager smoke test over the adapter
// ---------------------------------------------------------------------------

// The shared ConnectivityManager, built over the v16 device-model adapter with two configured slots and priority
// "1,2", must cache both slots with their respective interfaces - proving the adapter feeds the failover machinery.
TEST_F(DeviceModelConnectivityTest, ConnectivityManagerCachesBothConfiguredSlots) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 1, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));
    set_priority("1,2");

    auto evse_security = std::make_shared<NiceMock<ocpp::EvseSecurityMock>>();
    ocpp::ConnectivityManager cm(*config, evse_security, "");

    const auto slots = cm.get_network_connection_slots();
    ASSERT_EQ(slots.size(), 2u);
    EXPECT_EQ(slots.at(0), 1);
    EXPECT_EQ(slots.at(1), 2);

    const auto p1 = cm.get_network_connection_profile(1);
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(p1->ocppInterface, OCPPInterfaceEnum::Wired0);

    const auto p2 = cm.get_network_connection_profile(2);
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(p2->ocppInterface, OCPPInterfaceEnum::Wireless0);
}

// ---------------------------------------------------------------------------
// Confirmed security profile (SecurityCtrlr.SecurityProfile)
// ---------------------------------------------------------------------------

// The ConnectivityManager-facing get_security_profile() must report the confirmed SecurityCtrlr.SecurityProfile
// cell, not the per-slot value of the (attempt-time persisted) active slot that the OCPP-1.6-key-facing
// getSecurityProfile() keeps serving.
TEST_F(DeviceModelConnectivityTest, GetSecurityProfileReadsConfirmedCellNotActiveSlot) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 3, OCPPInterfaceEnum::Wired0));
    config->set_active_network_profile_slot(1, "test");

    // Shipped component config seeds the confirmed cell with 1.
    EXPECT_EQ(config->get_security_profile(), 1);
    EXPECT_EQ(config->getSecurityProfile(), 3) << "the per-slot getter must keep following the active slot";

    config->set_security_ctrl_security_profile(2, "test");
    EXPECT_EQ(config->get_security_profile(), 2);
    EXPECT_EQ(config->getSecurityProfile(), 3);
}

// Both confirmed-profile setters write the SecurityCtrlr cell (as in the 2.x device model) and leave the
// per-slot NetworkConfiguration value untouched.
TEST_F(DeviceModelConnectivityTest, SetActiveSecurityProfileWritesSecurityCtrlrCell) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 1, OCPPInterfaceEnum::Wired0));

    config->set_active_security_profile(3, "test");

    EXPECT_EQ(dm->get_value<int>(CC::SecurityProfile), 3);
    EXPECT_EQ(config->getSecurityProfile(), 1) << "the per-slot value must not change";
}

// Security profile 0 is legitimate in OCPP 1.6 (and in 2.x with AllowSecurityLevelZeroConnections); the confirmed
// cell must accept it - pins the SecurityCtrlr.json minLimit relaxation to 0.
TEST_F(DeviceModelConnectivityTest, SetActiveSecurityProfileAcceptsZero) {
    config->set_active_security_profile(0, "test");

    EXPECT_EQ(dm->get_value<int>(CC::SecurityProfile), 0);
    EXPECT_EQ(config->get_security_profile(), 0) << "the getter must surface the confirmed 0, not treat it as unset";
}

// A stripped component config without SecurityCtrlr.SecurityProfile must read as confirmed profile 0 ("nothing
// confirmed yet", prunes nothing) instead of throwing into the ConnectivityManager's pre-connect check.
TEST_F(DeviceModelConnectivityTest, GetSecurityProfileZeroWhenCellAbsent) {
    ASSERT_TRUE(CC::SecurityProfile.variable.has_value());
    ASSERT_EQ(dm->clear_value(CC::SecurityProfile.component, CC::SecurityProfile.variable.value(),
                              AttributeEnum::Actual, "test"),
              SetVariableStatusEnum::Accepted)
        << "fixture precondition: clearing SecurityCtrlr.SecurityProfile must succeed";

    EXPECT_EQ(config->get_security_profile(), 0);
}

// Regression: persisting the active slot at attempt time (what the ConnectivityManager does before connecting)
// must not prune lower-security-profile slots - only a confirmed connection may raise the prune threshold.
TEST_F(DeviceModelConnectivityTest, AttemptingSlotDoesNotPruneLowerSecuritySlots) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 1, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 3, OCPPInterfaceEnum::Wireless0));
    set_priority("1,2");

    auto evse_security = std::make_shared<NiceMock<ocpp::EvseSecurityMock>>();
    ocpp::ConnectivityManager cm(*config, evse_security, "");

    // Simulate an attempt on the high-SP slot 2: the active slot is persisted, but nothing was confirmed.
    config->set_active_network_profile_slot(2, "test");
    cm.check_cache_for_invalid_security_profiles();

    EXPECT_EQ(cm.get_network_connection_slots().size(), 2u)
        << "an unconfirmed attempt must not prune the lower-security-profile slot";
}

// A confirmed connection (on_websocket_connected -> set_security_ctrl_security_profile) raises the prune
// threshold: lower-security-profile slots are removed from the failover cache - upgrades are one-way.
TEST_F(DeviceModelConnectivityTest, ConfirmedConnectionRaisesCellAndPrunesLowerSlots) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 3, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));
    set_priority("1,2");

    auto evse_security = std::make_shared<NiceMock<ocpp::EvseSecurityMock>>();
    ocpp::ConnectivityManager cm(*config, evse_security, "");

    config->set_security_ctrl_security_profile(3, "test"); // what a successful SP-3 connect writes
    cm.check_cache_for_invalid_security_profiles();

    const auto slots = cm.get_network_connection_slots();
    ASSERT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots.at(0), 1);
    EXPECT_FALSE(cm.get_network_connection_profile(2).has_value());

    // Lowering the confirmed cell afterwards must not resurrect the pruned slot: the cache only ever shrinks
    // until a rebuild (reload_network_profiles) re-reads the priority string.
    config->set_active_security_profile(1, "test");
    cm.check_cache_for_invalid_security_profiles();

    const auto slots_after_downgrade = cm.get_network_connection_slots();
    ASSERT_EQ(slots_after_downgrade.size(), 1u) << "lowering the confirmed profile must not resurrect pruned slots";
    EXPECT_EQ(slots_after_downgrade.at(0), 1);
    EXPECT_FALSE(cm.get_network_connection_profile(2).has_value());
}

// A cache rebuild resurrects every slot listed in the priority string; the next check must re-prune against the
// confirmed profile instead of early-returning on an unchanged level - otherwise a reload would reopen a
// security downgrade window.
TEST_F(DeviceModelConnectivityTest, ReloadDoesNotResurrectPrunedLowerSlots) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 3, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));
    set_priority("1,2");

    auto evse_security = std::make_shared<NiceMock<ocpp::EvseSecurityMock>>();
    ocpp::ConnectivityManager cm(*config, evse_security, "");

    config->set_security_ctrl_security_profile(3, "test");
    cm.check_cache_for_invalid_security_profiles();
    ASSERT_EQ(cm.get_network_connection_slots().size(), 1u);

    // The rebuild restores both slots from the priority string; the check that runs before every connect attempt
    // must then re-prune.
    cm.reload_network_profiles();
    cm.check_cache_for_invalid_security_profiles();

    const auto slots = cm.get_network_connection_slots();
    ASSERT_EQ(slots.size(), 1u) << "the confirmed-SP threshold must be re-applied after a cache rebuild";
    EXPECT_EQ(slots.at(0), 1);
}

// A reload must keep naming the same active slot when the priority list is reordered or extended - keeping the
// raw index would silently point at a different slot (wrong security-profile confirms and slot reporting).
TEST_F(DeviceModelConnectivityTest, ReloadRemapsActivePriorityToSameSlot) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 1, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 1, OCPPInterfaceEnum::Wireless0));
    set_priority("1,2");

    auto evse_security = std::make_shared<NiceMock<ocpp::EvseSecurityMock>>();
    ocpp::ConnectivityManager cm(*config, evse_security, "");
    ASSERT_EQ(cm.get_active_network_configuration_slot(), 1);

    // Prepending slot 2 shifts slot 1 to index 1; the reload must follow the slot, not the index.
    set_priority("2,1");
    cm.reload_network_profiles();
    EXPECT_EQ(cm.get_active_network_configuration_slot(), 1);
}

// The slot-pinned setter must write the named slot's SecurityProfile even when another slot is active - the
// security-profile switch captures the slot at switch time so a late revert cannot clobber a failover slot.
TEST_F(DeviceModelConnectivityTest, SetSecurityProfileForSlotWritesNamedSlotNotActive) {
    write_slot_profile(1, make_slot_profile("wss://slot1.example.com/ocpp", 1, OCPPInterfaceEnum::Wired0));
    write_slot_profile(2, make_slot_profile("wss://slot2.example.com/ocpp", 3, OCPPInterfaceEnum::Wireless0));
    config->set_active_network_profile_slot(2, "test");

    config->set_security_profile_for_slot(1, 2);

    EXPECT_EQ(dm->get_value<int>(NC::get_component_variable(1, NC::SecurityProfile)), 2)
        << "the named slot must be written";
    EXPECT_EQ(dm->get_value<int>(NC::get_component_variable(2, NC::SecurityProfile)), 3)
        << "the active slot must stay untouched";
}

// Missing device-model cells in the legacy synthesis must yield nullopt instead of an exception escaping into
// the ConnectivityManager's profile caching (which runs at construction and on reload without a try/catch).
// Reachable e.g. when the active slot names a NetworkConfiguration component that does not exist.
TEST_F(DeviceModelConnectivityTest, ReadProfileSynthesisForMissingComponentYieldsNulloptNotThrow) {
    config->set_active_network_profile_slot(7, "test"); // no NetworkConfiguration(7) component exists

    std::optional<ocpp::v2::NetworkConnectionProfile> result;
    EXPECT_NO_THROW(result = config->read_network_connection_profile(7));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// ocpp16 component-config patcher: NetworkConfiguration[slot].OcppInterface pinning
// ---------------------------------------------------------------------------

const std::string STANDARD_CONFIGS_PATH = "./resources/example_config/v2/component_config";

// Locate the Actual-attribute value of NetworkConfiguration[slot].OcppInterface in a component-config map.
std::optional<std::string>
read_nc_interface(std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& configs, int slot) {
    for (auto& [key, vars] : configs) {
        if (key.name != "NetworkConfiguration" || !key.instance.has_value() ||
            key.instance.value() != std::to_string(slot)) {
            continue;
        }
        for (auto& var : vars) {
            if (var.name != "OcppInterface") {
                continue;
            }
            for (auto& attr : var.attributes) {
                if (attr.variable_attribute.type == AttributeEnum::Actual) {
                    if (attr.variable_attribute.value.has_value()) {
                        return attr.variable_attribute.value->get();
                    }
                    return std::nullopt;
                }
            }
        }
    }
    return std::nullopt;
}

// Force an explicit Actual-attribute value onto NetworkConfiguration[slot].OcppInterface, mimicking an integrator
// who set the interface explicitly in their component config.
void set_nc_interface(std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& configs, int slot,
                      const std::string& value) {
    for (auto& [key, vars] : configs) {
        if (key.name != "NetworkConfiguration" || !key.instance.has_value() ||
            key.instance.value() != std::to_string(slot)) {
            continue;
        }
        for (auto& var : vars) {
            if (var.name != "OcppInterface") {
                continue;
            }
            for (auto& attr : var.attributes) {
                if (attr.variable_attribute.type == AttributeEnum::Actual) {
                    attr.variable_attribute.value = value;
                    return;
                }
            }
        }
    }
    FAIL() << "NetworkConfiguration[" << slot << "].OcppInterface Actual attribute not found";
}

// The migration pins NetworkConfiguration[slot].OcppInterface to "Any": legacy 1.6 configs have no interface notion,
// and pinning "Any" keeps post-migration behavior identical to the pre-device-model single-profile synthesis. The
// shipped config's "Wired0" default lives in the default value and does not block the pin.
TEST(DeviceModelConnectivityPatcher, MigrationPinsOcppInterfaceToAny) {
    const auto ocpp16_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        ocpp::tests::make_ocpp16_test_config_full(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

    auto component_configs = ocpp::v2::get_all_component_configs(STANDARD_CONFIGS_PATH);
    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *ocpp16_config, {}, /*network_config_slot=*/1);

    EXPECT_EQ(read_nc_interface(component_configs, 1), std::optional<std::string>("Any"));
}

// An explicit attribute value set by the integrator wins: the pin uses allow_override=false, so a
// NetworkConfiguration[slot].OcppInterface that already carries an Actual value is left untouched.
TEST(DeviceModelConnectivityPatcher, MigrationKeepsExplicitOcppInterface) {
    const auto ocpp16_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        ocpp::tests::make_ocpp16_test_config_full(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

    auto component_configs = ocpp::v2::get_all_component_configs(STANDARD_CONFIGS_PATH);
    set_nc_interface(component_configs, 1, "Wireless0");

    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *ocpp16_config, {}, /*network_config_slot=*/1);

    EXPECT_EQ(read_nc_interface(component_configs, 1), std::optional<std::string>("Wireless0"))
        << "An explicit OcppInterface value must survive the migration pin";
}

// Locate a variable in a component-config map (component name + optional instance + variable name).
ocpp::v2::DeviceModelVariable*
find_config_variable(std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& configs,
                     const std::string& component_name, const std::optional<std::string>& component_instance,
                     const std::string& variable_name) {
    for (auto& [key, vars] : configs) {
        if (key.name != component_name || key.instance != component_instance) {
            continue;
        }
        for (auto& var : vars) {
            if (var.name == variable_name) {
                return &var;
            }
        }
    }
    return nullptr;
}

// Read a variable's Actual-attribute value from a component-config map.
std::optional<std::string>
read_config_value(std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& configs,
                  const std::string& component_name, const std::optional<std::string>& component_instance,
                  const std::string& variable_name) {
    const auto* var = find_config_variable(configs, component_name, component_instance, variable_name);
    if (var == nullptr) {
        return std::nullopt;
    }
    for (const auto& attr : var->attributes) {
        if (attr.variable_attribute.type == AttributeEnum::Actual && attr.variable_attribute.value.has_value()) {
            return attr.variable_attribute.value->get();
        }
    }
    return std::nullopt;
}

// The migration stamps the confirmed security profile (SecurityCtrlr.SecurityProfile) from the legacy config -
// overriding the shipped component-config value - so migrated installs prune slots against the profile that was
// actually in use.
TEST(DeviceModelConnectivityPatcher, MigrationStampsConfirmedSecurityProfile) {
    auto config_json = nlohmann::json::parse(ocpp::tests::make_ocpp16_test_config_full());
    config_json["Security"]["SecurityProfile"] = 2; // differs from the shipped component-config value (1)
    const auto ocpp16_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(config_json.dump(), CONFIG_DIR_V16,
                                                                                     USER_CONFIG_FILE_LOCATION_V16);

    auto component_configs = ocpp::v2::get_all_component_configs(STANDARD_CONFIGS_PATH);
    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *ocpp16_config, {}, /*network_config_slot=*/1);

    EXPECT_EQ(read_config_value(component_configs, "SecurityCtrlr", std::nullopt, "SecurityProfile"),
              std::optional<std::string>("2"))
        << "the legacy SecurityProfile must override the shipped component-config value";
}

// The migration never modifies NetworkConfigurationPriority or its valuesList: reaching the migrated slot
// through the priority is the integrator's component-config responsibility (a mismatch is only warned about).
// Migrating to slot 2 must still write the slot's values and seed ActiveNetworkProfile.
TEST(DeviceModelConnectivityPatcher, MigrationDoesNotTouchPriorityOrValuesList) {
    const auto ocpp16_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        ocpp::tests::make_ocpp16_test_config_full(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

    auto component_configs = ocpp::v2::get_all_component_configs(STANDARD_CONFIGS_PATH);
    const auto* priority_before =
        find_config_variable(component_configs, "OCPPCommCtrlr", std::nullopt, "NetworkConfigurationPriority");
    ASSERT_NE(priority_before, nullptr);
    const auto values_list_before = priority_before->characteristics.valuesList;

    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *ocpp16_config, {}, /*network_config_slot=*/2);

    EXPECT_EQ(read_config_value(component_configs, "OCPPCommCtrlr", std::nullopt, "NetworkConfigurationPriority"),
              std::optional<std::string>("1"))
        << "the migration must not rewrite the priority, even when the migrated slot is not listed";
    const auto* priority =
        find_config_variable(component_configs, "OCPPCommCtrlr", std::nullopt, "NetworkConfigurationPriority");
    ASSERT_NE(priority, nullptr);
    EXPECT_EQ(priority->characteristics.valuesList, values_list_before)
        << "the migration must not extend the priority's valuesList";

    EXPECT_TRUE(read_config_value(component_configs, "NetworkConfiguration", "2", "OcppCsmsUrl").has_value())
        << "the legacy connection settings must still be migrated into the configured slot";
}

// The migration seeds ActiveNetworkProfile with the migrated slot so the OCPP 1.6 key surface targets it
// already before the first successful connect.
TEST(DeviceModelConnectivityPatcher, MigrationSeedsActiveNetworkProfile) {
    const auto ocpp16_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        ocpp::tests::make_ocpp16_test_config_full(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

    auto component_configs = ocpp::v2::get_all_component_configs(STANDARD_CONFIGS_PATH);
    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *ocpp16_config, {}, /*network_config_slot=*/2);

    EXPECT_EQ(read_config_value(component_configs, "OCPPCommCtrlr", std::nullopt, "ActiveNetworkProfile"),
              std::optional<std::string>("2"));
}

// A slot without a NetworkConfiguration_<N> component config is never synthesized: the network connection
// migration is skipped with an error log, and the rest of the migration is unaffected.
TEST(DeviceModelConnectivityPatcher, MigrationSkipsSlotWithoutComponentConfig) {
    const auto ocpp16_config = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        ocpp::tests::make_ocpp16_test_config_full(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

    auto component_configs = ocpp::v2::get_all_component_configs(STANDARD_CONFIGS_PATH);
    ocpp::v2::patch_component_config_with_ocpp16(component_configs, *ocpp16_config, {}, /*network_config_slot=*/3);

    ocpp::v2::ComponentKey nc3_key;
    nc3_key.name = "NetworkConfiguration";
    nc3_key.instance = "3";
    EXPECT_EQ(component_configs.find(nc3_key), component_configs.end())
        << "no NetworkConfiguration[3] component may be synthesized";
    EXPECT_EQ(read_config_value(component_configs, "OCPPCommCtrlr", std::nullopt, "NetworkConfigurationPriority"),
              std::optional<std::string>("1"));
    EXPECT_NE(read_config_value(component_configs, "OCPPCommCtrlr", std::nullopt, "ActiveNetworkProfile"),
              std::optional<std::string>("3"))
        << "ActiveNetworkProfile must not point at a skipped slot";
    // The non-network migration still ran.
    EXPECT_EQ(read_config_value(component_configs, "SecurityCtrlr", std::nullopt, "SecurityProfile"),
              std::optional<std::string>(std::to_string(ocpp16_config->getSecurityProfile())));
}

} // namespace
} // namespace v16
} // namespace ocpp
