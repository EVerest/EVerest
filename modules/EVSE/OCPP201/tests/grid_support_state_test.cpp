// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "../grid_support/grid_support_state.hpp"

#include <generated/types/grid_support.hpp>

namespace {

namespace gs = types::grid_support;

gs::Directive make_directive(const std::string& id, gs::DirectiveType type, bool is_default = false) {
    gs::Directive directive;
    directive.id = id;
    directive.directive_type = type;
    directive.priority = 0;
    directive.is_default = is_default;
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

gs::GridAlarm make_alarm(gs::GridEventFault fault, bool alarm_ended = false) {
    gs::GridAlarm alarm;
    alarm.fault = fault;
    alarm.alarm_ended = alarm_ended;
    alarm.timestamp = "2026-06-16T00:00:00Z";
    return alarm;
}

// build_active_set returns only directives whose type the EVSE declared as supported, tagged with the EVSE id.
TEST(GridSupportState, BuildActiveSetFiltersToSupportedTypes) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 1;

    state.set_capability(evse_id, make_capability({gs::DirectiveType::VoltVar, gs::DirectiveType::FreqDroop}));

    state.set_active_directives({
        make_directive("d-voltvar", gs::DirectiveType::VoltVar),
        make_directive("d-voltwatt", gs::DirectiveType::VoltWatt), // unsupported by this EVSE
    });

    const auto active = state.build_active_set(evse_id);

    EXPECT_EQ(active.evse_id, evse_id);
    ASSERT_EQ(active.directives.size(), 1u);
    EXPECT_EQ(active.directives.at(0).directive_type, gs::DirectiveType::VoltVar);
    EXPECT_EQ(active.directives.at(0).id, "d-voltvar");
}

TEST(GridSupportState, BuildActiveSetForUnregisteredEvseIsEmpty) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 9;

    state.set_active_directives({make_directive("d-voltvar", gs::DirectiveType::VoltVar)});

    const auto active = state.build_active_set(evse_id);

    EXPECT_EQ(active.evse_id, evse_id);
    EXPECT_TRUE(active.directives.empty());
}

TEST(GridSupportState, SetCapabilityRegistersEvse) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 2;

    state.set_capability(evse_id, make_capability({gs::DirectiveType::VoltWatt}));

    const auto registered = state.registered_evses();
    ASSERT_EQ(registered.size(), 1u);
    EXPECT_EQ(registered.at(0), evse_id);
}

// unregister exists to undo a registration when the enabling write is rejected; after it the EVSE is
// gone from registered_evses and its active set is empty.
TEST(GridSupportState, UnregisterRemovesEvse) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 4;

    state.set_capability(evse_id, make_capability({gs::DirectiveType::VoltVar}));
    ASSERT_EQ(state.registered_evses().size(), 1u);

    state.unregister(evse_id);

    EXPECT_TRUE(state.registered_evses().empty());
    state.set_active_directives({make_directive("d", gs::DirectiveType::VoltVar)});
    const auto active = state.build_active_set(evse_id);
    EXPECT_TRUE(active.directives.empty());
}

TEST(GridSupportState, UnregisterUnknownEvseIsNoop) {
    module::GridSupportState state;
    state.unregister(99);
    EXPECT_TRUE(state.registered_evses().empty());
}

// Buffering does not register the EVSE; registration happens only when the buffer is later applied.
TEST(GridSupportState, BufferPendingCapabilityDoesNotRegister) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 3;

    state.buffer_pending_capability(evse_id, make_capability({gs::DirectiveType::VoltVar}));

    EXPECT_TRUE(state.registered_evses().empty());
}

// take_pending_capabilities drains the buffer, so a second drain yields nothing.
TEST(GridSupportState, TakePendingCapabilitiesDrainsBuffer) {
    module::GridSupportState state;

    state.buffer_pending_capability(1, make_capability({gs::DirectiveType::VoltVar}));
    state.buffer_pending_capability(2, make_capability({gs::DirectiveType::FreqDroop}));

    const auto drained = state.take_pending_capabilities();
    ASSERT_EQ(drained.size(), 2u);
    EXPECT_EQ(drained.at(0).first, 1);
    EXPECT_EQ(drained.at(0).second.supported_types.at(0), gs::DirectiveType::VoltVar);
    EXPECT_EQ(drained.at(1).first, 2);
    EXPECT_EQ(drained.at(1).second.supported_types.at(0), gs::DirectiveType::FreqDroop);

    EXPECT_TRUE(state.take_pending_capabilities().empty());
}

// Re-buffering the same EVSE is last-wins: the drain yields one entry carrying the latest capability.
TEST(GridSupportState, BufferPendingCapabilityIsLastWinsPerEvse) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 5;

    state.buffer_pending_capability(evse_id, make_capability({gs::DirectiveType::VoltVar}));
    state.buffer_pending_capability(evse_id, make_capability({gs::DirectiveType::FreqDroop}));

    const auto drained = state.take_pending_capabilities();
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained.at(0).first, evse_id);
    ASSERT_EQ(drained.at(0).second.supported_types.size(), 1u);
    EXPECT_EQ(drained.at(0).second.supported_types.at(0), gs::DirectiveType::FreqDroop);
}

// set_active_directives replaces the stored set wholesale: build_active_set filters the latest set, not a union.
TEST(GridSupportState, SetActiveDirectivesIsLastWins) {
    module::GridSupportState state;
    constexpr int32_t evse_id = 6;

    state.set_capability(evse_id, make_capability({gs::DirectiveType::VoltVar, gs::DirectiveType::FreqDroop}));

    state.set_active_directives({make_directive("first", gs::DirectiveType::VoltVar)});
    state.set_active_directives({make_directive("second", gs::DirectiveType::FreqDroop)});

    const auto active = state.build_active_set(evse_id);
    ASSERT_EQ(active.directives.size(), 1u);
    EXPECT_EQ(active.directives.at(0).id, "second");

    // Clearing the active set empties build_active_set output (no ghost directives linger).
    state.set_active_directives({});
    EXPECT_TRUE(state.build_active_set(evse_id).directives.empty());
}

// buffer_pending_alarm preserves arrival order; take_pending_alarms returns them and clears the buffer.
TEST(GridSupportState, BufferPendingAlarmRoundTripPreservesOrder) {
    module::GridSupportState state;

    state.buffer_pending_alarm(make_alarm(gs::GridEventFault::OverVoltage));
    state.buffer_pending_alarm(make_alarm(gs::GridEventFault::UnderFrequency, true));

    const auto drained = state.take_pending_alarms();
    ASSERT_EQ(drained.size(), 2u);
    EXPECT_EQ(drained.at(0).fault, gs::GridEventFault::OverVoltage);
    EXPECT_FALSE(drained.at(0).alarm_ended);
    EXPECT_EQ(drained.at(1).fault, gs::GridEventFault::UnderFrequency);
    EXPECT_TRUE(drained.at(1).alarm_ended);
}

// take_pending_alarms drains the buffer, so a second drain yields nothing.
TEST(GridSupportState, TakePendingAlarmsDrainsBuffer) {
    module::GridSupportState state;

    state.buffer_pending_alarm(make_alarm(gs::GridEventFault::OverVoltage));

    ASSERT_EQ(state.take_pending_alarms().size(), 1u);
    EXPECT_TRUE(state.take_pending_alarms().empty());
}

} // namespace
