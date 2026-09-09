// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

// Mock peer recorders for EvSimulator FSM tests.
//
// The ev-cli generated `ev_board_supportIntf`, `ISO15118_evIntf`, `ev_slacIntf`,
// and `kvsIntf` classes have non-virtual `call_*` methods and require a real
// `Everest::ModuleAdapter*` for construction, so they cannot be subclassed to
// intercept calls. This header provides hand-rolled recorder classes that mirror
// the subset of `call_*` methods FsmContext invokes; tests use these recorders
// directly to assert expected calls.
//
// FsmContext shortcuts (set_cp, allow_power_on, iso_*, slac_trigger_matching,
// kvs_*) route through these recorders via PeerActions function-pointer
// injection rather than raw `*Intf*` peers.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <utils/types.hpp> // Array, Object

#include <generated/types/board_support_common.hpp>
#include <generated/types/ev_board_support.hpp>
#include <generated/types/iso15118.hpp>

namespace module::test {

// Generic recorder: every call is stringified into `records` for substring
// assertions; consumers may also inspect typed last_* fields when convenient.

class MockBoardSupport {
public:
    void call_enable(bool value);
    void call_set_cp_state(::types::ev_board_support::EvCpState cp_state);
    void call_allow_power_on(bool value);
    void call_diode_fail(bool value);
    void call_set_ac_max_current(double current);
    void call_set_three_phases(bool three_phases);
    void call_set_rcd_error(double rcd_current_mA);

    std::vector<std::string> records;
    void clear() {
        records.clear();
    }
};

class MockIso15118Ev {
public:
    // Returns `next_start_charging_result` (default true).
    bool call_start_charging(::types::iso15118::EnergyTransferMode mode,
                             ::types::iso15118::SelectedPaymentOption payment, double departure_time, double e_amount);
    void call_stop_charging();
    void call_pause_charging();
    void call_set_fault();
    void call_set_dc_params(const ::types::iso15118::DcEvParameters& params);
    void call_set_bpt_dc_params(const ::types::iso15118::DcEvBPTParameters& params);
    void call_enable_sae_j2847_v2g_v2h();
    void call_update_soc(double soc);

    bool next_start_charging_result{true};
    std::vector<std::string> records;
    void clear() {
        records.clear();
    }
};

class MockEvSlac {
public:
    void call_reset();
    // Returns `next_trigger_matching_result` (default true).
    bool call_trigger_matching();

    bool next_trigger_matching_result{true};
    std::vector<std::string> records;
    void clear() {
        records.clear();
    }
};

class MockKvs {
public:
    using Value = std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string>;

    void call_store(const std::string& key, Value value);
    // Returns `next_load_value` (default `nullptr_t{}` modelling first boot).
    Value call_load(const std::string& key);
    void call_delete(const std::string& key);
    // Returns `next_exists_result` (default false).
    bool call_exists(const std::string& key);

    Value next_load_value{nullptr};
    bool next_exists_result{false};
    std::vector<std::string> records;
    void clear() {
        records.clear();
    }
};

} // namespace module::test
