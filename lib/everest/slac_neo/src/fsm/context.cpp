// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include <everest/slac/fsm/context.hpp>

#include <algorithm>
#include <random>

#include "misc.hpp"

namespace everest::lib::slac::fsm::evse {

void Context::signal_cm_slac_parm_req(const uint8_t* mac) {
    if (callbacks.signal_ev_mac_address_parm_req) {
        const auto mac_string = format_mac_addr(mac);
        callbacks.signal_ev_mac_address_parm_req(mac_string);
    }
}

void Context::signal_cm_slac_match_cnf(const uint8_t* mac) {
    if (callbacks.signal_ev_mac_address_match_cnf) {
        const auto mac_string = format_mac_addr(mac);
        callbacks.signal_ev_mac_address_match_cnf(mac_string);
    }
}

void Context::signal_dlink_ready(bool value) {
    if (callbacks.signal_dlink_ready) {
        callbacks.signal_dlink_ready(value);
    }
}

void Context::signal_error_routine_request() {
    if (callbacks.signal_error_routine_request) {
        callbacks.signal_error_routine_request();
    }
}

void Context::signal_state(const std::string& state) {
    if (callbacks.signal_state) {
        callbacks.signal_state(state);
    }
}

void Context::clear_match_confirm_cache() {
    match_confirm_cache = MatchConfirmCache{};
}

void Context::cache_match_confirm_message(messages::cm_slac_match_cnf const& match_confirm_message, uint8_t const* ev_mac,
                                          uint8_t const* evse_mac, uint8_t const* run_id) {
    cache_match_confirm_message(match_confirm_message, byte_array_from_wire<MacAddress>(ev_mac),
                                byte_array_from_wire<MacAddress>(evse_mac), byte_array_from_wire<RunId>(run_id));
}

void Context::cache_match_confirm_message(messages::cm_slac_match_cnf const& match_confirm_message, MacAddress const& ev_mac,
                                          MacAddress const& evse_mac, RunId const& run_id) {
    match_confirm_cache.valid = true;
    match_confirm_cache.message = match_confirm_message;
    match_confirm_cache.ev_mac = ev_mac;
    match_confirm_cache.evse_mac = evse_mac;
    match_confirm_cache.run_id = run_id;
}

void Context::log_debug(const std::string& text) {
    if (callbacks.log_debug) {
        callbacks.log_debug(text);
    }
}

void Context::log_info(const std::string& text) {
    if (callbacks.log_info) {
        callbacks.log_info(text);
    }
}

void Context::log_warn(const std::string& text) {
    if (callbacks.log_warn) {
        callbacks.log_warn(text);
    }
}

void Context::log_error(const std::string& text) {
    if (callbacks.log_error) {
        callbacks.log_error(text);
    }
}

void Context::telemetry(const std::string& block, const std::string& key, const std::string& value) {
    if (callbacks.pub_telemetry) {
        callbacks.pub_telemetry(block, key, value);
    }
}

} // namespace everest::lib::slac::fsm::evse
