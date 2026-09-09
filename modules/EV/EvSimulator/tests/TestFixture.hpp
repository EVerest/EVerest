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

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace module::test {

// Shared record/sink inspection helpers used across the FsmContext test TUs.

inline bool contains_substr(const std::vector<std::string>& records, const std::string& needle) {
    return std::any_of(records.begin(), records.end(),
                       [&](const std::string& r) { return r.find(needle) != std::string::npos; });
}

// First index whose record contains `needle`, or -1 if absent. Used to assert
// ordering between recorded peer calls.
inline int index_of_substr(const std::vector<std::string>& records, const std::string& needle) {
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].find(needle) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline bool topic_recorded(const PublisherSink& sink, const std::string& topic) {
    return std::any_of(sink.records.begin(), sink.records.end(), [&](const auto& kv) { return kv.first == topic; });
}

inline std::string payload_for(const PublisherSink& sink, const std::string& topic) {
    auto it = std::find_if(sink.records.begin(), sink.records.end(), [&](const auto& kv) { return kv.first == topic; });
    return it == sink.records.end() ? std::string{} : it->second;
}

// Aggregates the recorders FsmContext drives through PeerActions. Tests
// inspect `bsp.records`, `iso.records`, `slac.records`, `kvs.records` and
// the typed last-value fields under each mock.
struct ActionMocks {
    MockBoardSupport bsp;
    MockIso15118Ev iso;
    MockEvSlac slac;
    MockKvs kvs;

    // KVS load returns this optional raw JSON payload. nullopt == key missing
    // (first boot); optional<string>{""} == persisted empty payload (suspicious);
    // optional<string>{"..."} == persisted blob to parse.
    std::optional<std::string> next_kvs_load_value{};
    // Internal-side publish records (mirrors EvAPI subscriber traffic).
    int internal_bsp_event_count{0};
    int internal_ev_info_count{0};

    // Out-of-band error raise/clear records (mirrors p_ev_manager error
    // interaction, driven from the loop thread via the ErrorSink seam).
    std::vector<std::string> error_raises;
    std::vector<std::string> error_clears;
    // Errors the framework reports as currently active. is_active() consults
    // this set so a clear of an inactive error is a no-op (publishes a
    // Rejected ack instead of silently doing nothing).
    std::vector<std::pair<std::string, std::string>> active_errors;
};

// Build a PeerActions pack wired through the supplied ActionMocks recorder.
inline PeerActions make_peer_actions(ActionMocks& mocks) {
    PeerActions actions;

    actions.bsp.set_cp = [&mocks](::types::ev_board_support::EvCpState s) { mocks.bsp.call_set_cp_state(s); };
    actions.bsp.allow_power_on = [&mocks](bool v) { mocks.bsp.call_allow_power_on(v); };
    actions.bsp.set_ac_max_current = [&mocks](float a) { mocks.bsp.call_set_ac_max_current(static_cast<double>(a)); };
    actions.bsp.set_three_phases = [&mocks](bool v) { mocks.bsp.call_set_three_phases(v); };
    actions.bsp.diode_fail = [&mocks](bool v) { mocks.bsp.call_diode_fail(v); };
    actions.bsp.set_rcd_error = [&mocks](float v) { mocks.bsp.call_set_rcd_error(static_cast<double>(v)); };
    actions.bsp.enable = [&mocks](bool v) { mocks.bsp.call_enable(v); };
    actions.bsp.present = true;

    actions.iso.start_charging = [&mocks](::types::iso15118::EnergyTransferMode mode,
                                          ::types::iso15118::PaymentOption payment, int32_t departure, int32_t e_amount,
                                          bool /*force_payment_option*/) -> bool {
        ::types::iso15118::SelectedPaymentOption sel;
        sel.payment_option = payment;
        return mocks.iso.call_start_charging(mode, sel, static_cast<double>(departure), static_cast<double>(e_amount));
    };
    actions.iso.stop_charging = [&mocks]() { mocks.iso.call_stop_charging(); };
    actions.iso.pause_charging = [&mocks]() { mocks.iso.call_pause_charging(); };
    actions.iso.update_soc = [&mocks](float pct) { mocks.iso.call_update_soc(static_cast<double>(pct)); };
    actions.iso.enable_sae_j2847_v2g_v2h = [&mocks]() { mocks.iso.call_enable_sae_j2847_v2g_v2h(); };
    actions.iso.set_bpt_dc_params = [&mocks](const ::types::iso15118::DcEvBPTParameters& params) {
        mocks.iso.call_set_bpt_dc_params(params);
    };
    actions.iso.set_dc_params = [&mocks](const ::types::iso15118::DcEvParameters& params) {
        mocks.iso.call_set_dc_params(params);
    };
    actions.iso.present = true;

    actions.slac.trigger_matching = [&mocks]() -> bool { return mocks.slac.call_trigger_matching(); };
    actions.slac.present = true;

    actions.kvs.store = [&mocks](const std::string& key, const std::string& json) {
        // Record both the key and the json payload for assertion convenience.
        mocks.kvs.records.emplace_back("store(key=" + key + ",json=" + json + ")");
    };
    actions.kvs.load_raw = [&mocks](const std::string& key) -> std::optional<std::string> {
        mocks.kvs.records.emplace_back("load(key=" + key + ")");
        return mocks.next_kvs_load_value;
    };
    actions.kvs.present = true;

    actions.publisher.bsp_event = [&mocks](const ::types::board_support_common::BspEvent&) {
        mocks.internal_bsp_event_count++;
    };
    actions.publisher.ev_info = [&mocks](const ::types::evse_manager::EVInfo&) { mocks.internal_ev_info_count++; };
    actions.publisher.present = true;

    actions.error.raise = [&mocks](const std::string& type, const std::string& sub_type, const std::string& message,
                                   Everest::error::Severity) {
        mocks.error_raises.emplace_back("raise(type=" + type + ",sub=" + sub_type + ",msg=" + message + ")");
    };
    actions.error.clear = [&mocks](const std::string& type, const std::optional<std::string>& sub_type) {
        mocks.error_clears.emplace_back("clear(type=" + type + ",sub=" + sub_type.value_or("") + ")");
    };
    actions.error.is_active = [&mocks](const std::string& type, const std::string& sub_type) -> bool {
        for (const auto& [t, s] : mocks.active_errors) {
            if (t == type && s == sub_type) {
                return true;
            }
        }
        return false;
    };
    actions.error.present = true;

    return actions;
}

struct TestFixture {
    Conf cfg{};
    ev_API::Topics topics;
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
        cfg.on_battery_full = "clamp";
        cfg.battery_full_threshold_pct = 100.0;
        cfg.cfg_communication_check_to_s = 0;
        cfg.cfg_heartbeat_interval_ms = 0;
        cfg.enabled_at_startup = false;

        topics.setup("test_module", "ev_simulator", 1);
    }

    std::unique_ptr<FsmContext> make_ctx() {
        return std::make_unique<FsmContext>(
            make_peer_actions(mocks),
            [this](const std::string& topic, const std::string& payload) { sink(topic, payload); },
            [this](std::chrono::milliseconds ms) { timer.arm(ms); }, [this]() { timer.cancel(); },
            [this](int ms) { timer.arm_tick(ms); }, [this]() { timer.disarm_tick(); },
            [this](Event ev) { timer.record_enqueue(std::move(ev)); },
            [this](std::chrono::milliseconds ms) { timer.arm_scenario(ms); }, cfg, topics);
    }
};

// Session-state test helpers. SimVars no longer exposes independent
// charge_mode / bpt / mcs_enabled / payment fields; they live in an
// optional<Session>. These helpers keep test bodies terse and mechanical:
// `set_mode(*ctx, AcIec)` starts (or retargets) a session of that mode while
// preserving any payment/bpt/mcs/curve already staged.
inline Session& ensure_session(FsmContext& ctx) {
    if (!ctx.vars.session) {
        ctx.vars.session.emplace();
    }
    return *ctx.vars.session;
}

inline void set_mode(FsmContext& ctx, API_types::ev_simulator::ChargeMode mode) {
    ensure_session(ctx).mode = mode;
}

inline void clear_session(FsmContext& ctx) {
    ctx.vars.session.reset();
}

} // namespace module::test
