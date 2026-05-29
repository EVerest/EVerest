// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

// TestFixture for FsmContext-based tests: wires PublisherSink + TimerSink +
// MockPeerActions into a freshly constructed FsmContext per Catch2 SECTION.
// Mocks record peer calls; the fixture exposes them by reference so tests can
// assert the recorded interactions.

#include "../main/FsmContext.hpp"
#include "PeerMocks.hpp"
#include "PublisherSink.hpp"
#include "TimerSink.hpp"

#include <everest_api_types/utilities/Topics.hpp>

#include <memory>
#include <string>

namespace module::test {

// Aggregates the recorders FsmContext drives through PeerActions. Tests
// inspect `bsp.records`, `iso.records`, `slac.records`, `kvs.records` and
// the typed last-value fields under each mock.
struct ActionMocks {
    MockBoardSupport bsp;
    MockIso15118Ev iso;
    MockEvSlac slac;
    MockKvs kvs;

    // KVS load returns this raw JSON string (empty == missing/first boot).
    std::string next_kvs_load_value{};
    // Internal-side publish records (mirrors EvAPI subscriber traffic).
    int internal_bsp_event_count{0};
    int internal_ev_info_count{0};
};

// Build a PeerActions pack wired through the supplied ActionMocks recorder.
inline PeerActions make_peer_actions(ActionMocks& mocks) {
    PeerActions actions;

    actions.bsp_set_cp = [&mocks](::types::ev_board_support::EvCpState s) { mocks.bsp.call_set_cp_state(s); };
    actions.bsp_allow_power_on = [&mocks](bool v) { mocks.bsp.call_allow_power_on(v); };
    actions.bsp_set_ac_max_current = [&mocks](float a) { mocks.bsp.call_set_ac_max_current(static_cast<double>(a)); };
    actions.bsp_set_three_phases = [&mocks](bool v) { mocks.bsp.call_set_three_phases(v); };
    actions.bsp_diode_fail = [&mocks](bool v) { mocks.bsp.call_diode_fail(v); };
    actions.bsp_set_rcd_error = [&mocks](float v) { mocks.bsp.call_set_rcd_error(static_cast<double>(v)); };

    actions.iso_start_charging = [&mocks](::types::iso15118::EnergyTransferMode mode,
                                          ::types::iso15118::PaymentOption payment, int32_t departure, int32_t e_amount,
                                          bool /*force_payment_option*/) -> bool {
        ::types::iso15118::SelectedPaymentOption sel;
        sel.payment_option = payment;
        return mocks.iso.call_start_charging(mode, sel, static_cast<double>(departure), static_cast<double>(e_amount));
    };
    actions.iso_stop_charging = [&mocks]() { mocks.iso.call_stop_charging(); };
    actions.iso_pause_charging = [&mocks]() { mocks.iso.call_pause_charging(); };
    actions.iso_update_soc = [&mocks](float pct) { mocks.iso.call_update_soc(static_cast<double>(pct)); };
    actions.iso_enable_sae_j2847_v2g_v2h = [&mocks]() { mocks.iso.call_enable_sae_j2847_v2g_v2h(); };
    actions.iso_set_bpt_dc_params = [&mocks](const ::types::iso15118::DcEvBPTParameters& params) {
        mocks.iso.call_set_bpt_dc_params(params);
    };

    actions.slac_trigger_matching = [&mocks]() -> bool { return mocks.slac.call_trigger_matching(); };

    actions.kvs_store = [&mocks](const std::string& key, const std::string& json) {
        // Record both the key and the json payload for assertion convenience.
        mocks.kvs.records.emplace_back("store(key=" + key + ",json=" + json + ")");
    };
    actions.kvs_load_raw = [&mocks](const std::string& key) -> std::string {
        mocks.kvs.records.emplace_back("load(key=" + key + ")");
        return mocks.next_kvs_load_value;
    };

    actions.publish_internal_bsp_event = [&mocks](const ::types::board_support_common::BspEvent&) {
        mocks.internal_bsp_event_count++;
    };
    actions.publish_internal_ev_info = [&mocks](const ::types::evse_manager::EVInfo&) {
        mocks.internal_ev_info_count++;
    };

    return actions;
}

struct TestFixture {
    Conf cfg{};
    ev_API::Topics topics;
    PeerHandles peers{};
    ActionMocks mocks;
    PublisherSink sink;
    TimerSink timer;

    TestFixture() {
        // Sensible defaults for the AC IEC pathway most helper tests use.
        cfg.connector_id = 1;
        cfg.ac_nominal_voltage = 230.0;
        cfg.max_current_a = 16.0;
        cfg.three_phases = true;
        cfg.dc_max_current_limit = 0;
        cfg.dc_max_power_limit = 0;
        cfg.dc_max_voltage_limit = 0;
        cfg.dc_energy_capacity = 60000;
        cfg.dc_target_current = 0;
        cfg.dc_target_voltage = 0;
        cfg.soc_initial_pct = 30;
        cfg.departure_time_s = 86400;
        cfg.e_amount_wh = 0;
        cfg.force_payment_option = false;
        cfg.keep_cross_boot_plugin_state = false;
        cfg.publish_bsp_measurements = false;
        cfg.tick_interval_ms = 250;
        cfg.cfg_communication_check_to_s = 0;
        cfg.cfg_heartbeat_interval_ms = 0;

        topics.setup("test_module", "ev_simulator", 1);
    }

    std::unique_ptr<FsmContext> make_ctx() {
        return std::make_unique<FsmContext>(
            peers, make_peer_actions(mocks),
            [this](const std::string& topic, const std::string& payload) { sink(topic, payload); },
            [this](std::chrono::milliseconds ms) { timer.arm(ms); }, [this]() { timer.cancel(); },
            [this](int ms) { timer.arm_tick(ms); }, [this]() { timer.disarm_tick(); },
            [this](Event ev) { timer.record_enqueue(std::move(ev)); },
            [this](std::chrono::seconds s) { timer.arm_scenario(s); }, cfg, topics);
    }
};

} // namespace module::test
