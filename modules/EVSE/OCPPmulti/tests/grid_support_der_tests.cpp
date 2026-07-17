// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

#include <generated/types/grid_support.hpp>

namespace {
using namespace stubs;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

namespace gs = types::grid_support;

gs::Directive make_directive(const std::string& id, gs::DirectiveType type) {
    gs::Directive directive;
    directive.id = id;
    directive.directive_type = type;
    directive.priority = 0;
    directive.is_default = false;
    directive.source = "ocpp";
    directive.received_at = "2026-06-16T00:00:00Z";
    return directive;
}

gs::DERCapability make_capability(const std::vector<gs::DirectiveType>& supported_types) {
    gs::DERCapability capability;
    capability.supported_types = supported_types;
    capability.nameplate.max_w_W = 1000.0f;
    capability.nameplate.max_va_VA = 1000.0f;
    capability.nameplate.v_nom_V = 230.0f;
    gs::ACCapability ac;
    capability.ac = ac;
    return capability;
}

// Return every set_variables write as Accepted (config writes succeed).
std::vector<ocpp::v2::SetVariableResult> accept_all_set(const std::vector<ocpp::v2::SetVariableData>& in,
                                                        const std::string&) {
    std::vector<ocpp::v2::SetVariableResult> out;
    out.reserve(in.size());
    for (const auto& data : in) {
        ocpp::v2::SetVariableResult result;
        result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Accepted;
        result.component = data.component;
        result.variable = data.variable;
        result.attributeType = data.attributeType;
        out.push_back(result);
    }
    return out;
}

// Answer a get_variables read of the AC DERCtrlr Enabled with the persisted value; reject anything else so
// read_persisted_der_enabled falls through to the DC controller (also rejected here) and then to nullopt.
std::vector<ocpp::v2::GetVariableResult> persisted_ac_enabled(const std::vector<ocpp::v2::GetVariableData>& in,
                                                              bool value) {
    std::vector<ocpp::v2::GetVariableResult> out;
    out.reserve(in.size());
    for (const auto& data : in) {
        ocpp::v2::GetVariableResult result;
        result.component = data.component;
        result.variable = data.variable;
        result.attributeType = data.attributeType;
        if (data.component.name == "ACDERCtrlr" and data.variable.name == "Enabled") {
            result.attributeStatus = ocpp::v2::GetVariableStatusEnum::Accepted;
            result.attributeValue = value ? "true" : "false";
        } else {
            result.attributeStatus = ocpp::v2::GetVariableStatusEnum::Rejected;
        }
        out.push_back(result);
    }
    return out;
}

void expect_init_and_ready(ChargePointStub& chargepoint, ConfigStub& config) {
    EXPECT_CALL(chargepoint, init(_)).Times(1);
    EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
    EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
        .Times(1);
    EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
    EXPECT_CALL(chargepoint, connect_websocket()).Times(1);
    EXPECT_CALL(chargepoint, on_der_republish_active_directives()).Times(AnyNumber());
    EXPECT_CALL(chargepoint, set_variables(_, _)).Times(AnyNumber()).WillRepeatedly(accept_all_set);
}

// A DER capability arriving on the live path must honor a CSMS-written Enabled=false already persisted in the
// device model: the EVSE stays disabled, so its built active set is empty even though a matching directive is
// active. This is the regression the fix targets - the live handler previously ignored the persisted value.
TEST(GridSupportDer, LiveCapabilityHonorsPersistedDisabled) {
    ChargePointStub chargepoint;
    ConfigStub config;
    config.GridSupportHeartbeatS = 0; // no heartbeat push during the test
    ModuleInterfaces interfaces;

    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_grid_support("grid_support_1", 1);
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");
    expect_init_and_ready(chargepoint, config);
    EXPECT_CALL(chargepoint, get_variables(_)).Times(AnyNumber()).WillRepeatedly([](const auto& in) {
        return persisted_ac_enabled(in, false);
    });

    GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                           interfaces.get_requires());
    ocpp.init();
    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    ocpp.ready(interfaces.get_config_service_client());

    // A directive that the incoming capability supports is already active.
    {
        auto handle = ocpp.grid_support_state().handle();
        ASSERT_TRUE(handle->capabilities_live());
        handle->set_active_directives({make_directive("d-voltvar", gs::DirectiveType::VoltVar)});
    }

    gs::EVSECapability evse_capability;
    evse_capability.evse_id = 1;
    evse_capability.capability = make_capability({gs::DirectiveType::VoltVar});
    ocpp.on_grid_support_capability(evse_capability);

    auto handle = ocpp.grid_support_state().handle();
    const auto active = handle->build_active_set(1);
    EXPECT_EQ(active.evse_id, 1);
    EXPECT_TRUE(active.directives.empty()) << "persisted Enabled=false must keep the EVSE disabled on live arrival";
    // The capability is still registered even while disabled.
    EXPECT_TRUE(handle->capability_for(1).has_value());
}

// The mirror case: a persisted Enabled=true (or a fresh default) leaves the EVSE enabled, so the matching
// directive surfaces in the built set.
TEST(GridSupportDer, LiveCapabilityHonorsPersistedEnabled) {
    ChargePointStub chargepoint;
    ConfigStub config;
    config.GridSupportHeartbeatS = 0;
    ModuleInterfaces interfaces;

    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_grid_support("grid_support_1", 1);
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");
    expect_init_and_ready(chargepoint, config);
    EXPECT_CALL(chargepoint, get_variables(_)).Times(AnyNumber()).WillRepeatedly([](const auto& in) {
        return persisted_ac_enabled(in, true);
    });

    GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                           interfaces.get_requires());
    ocpp.init();
    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    ocpp.ready(interfaces.get_config_service_client());

    {
        auto handle = ocpp.grid_support_state().handle();
        handle->set_active_directives({make_directive("d-voltvar", gs::DirectiveType::VoltVar)});
    }

    gs::EVSECapability evse_capability;
    evse_capability.evse_id = 1;
    evse_capability.capability = make_capability({gs::DirectiveType::VoltVar});
    ocpp.on_grid_support_capability(evse_capability);

    auto handle = ocpp.grid_support_state().handle();
    const auto active = handle->build_active_set(1);
    ASSERT_EQ(active.directives.size(), 1u);
    EXPECT_EQ(active.directives.at(0).id, "d-voltvar");
}

// The pre-construction buffer path (flush at ready) still restores the persisted Enabled: a capability
// buffered before ready and flushed with a persisted Enabled=false leaves the EVSE disabled.
TEST(GridSupportDer, BufferedCapabilityHonorsPersistedDisabledOnFlush) {
    ChargePointStub chargepoint;
    ConfigStub config;
    config.GridSupportHeartbeatS = 0;
    ModuleInterfaces interfaces;

    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_grid_support("grid_support_1", 1);
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");
    expect_init_and_ready(chargepoint, config);
    EXPECT_CALL(chargepoint, get_variables(_)).Times(AnyNumber()).WillRepeatedly([](const auto& in) {
        return persisted_ac_enabled(in, false);
    });

    GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                           interfaces.get_requires());
    ocpp.init();

    // Deliver before ready: capabilities are not live yet, so this buffers.
    gs::EVSECapability evse_capability;
    evse_capability.evse_id = 1;
    evse_capability.capability = make_capability({gs::DirectiveType::VoltVar});
    ocpp.on_grid_support_capability(evse_capability);

    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    ocpp.ready(interfaces.get_config_service_client()); // flushes the buffer through apply_der_capability

    auto handle = ocpp.grid_support_state().handle();
    handle->set_active_directives({make_directive("d-voltvar", gs::DirectiveType::VoltVar)});
    const auto active = handle->build_active_set(1);
    EXPECT_TRUE(active.directives.empty()) << "buffered capability must inherit persisted Enabled=false at flush";
    EXPECT_TRUE(handle->capability_for(1).has_value());
}

} // namespace
