// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <everest/slac/EvseSlacConfig.hpp>
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/slac_types.hpp>

namespace everest::lib::slac {

std::string format_nmk(Nmk const& nmk);
std::string format_nmk(const uint8_t* nmk);

std::string format_mac_addr(MacAddress const& mac);
std::string format_mac_addr(const uint8_t* mac);

std::optional<MacAddress> parse_mac_addr(std::string_view text);
bool parse_mac_addr(const std::string& mac_str, uint8_t* mac, size_t length);

std::string format_run_id(RunId const& run_id);
std::string format_run_id(const uint8_t* run_id);

std::string format_mmtype(const uint16_t mmtype);

// Whether a CM_SET_KEY.CNF result byte counts as success under the configured acceptance mode.
bool accepts_set_key_cnf_success_result(fsm::evse::SetKeyCnfSuccessMode mode, uint8_t result);

// NMK as plain hex without separators, as printed in the "Using SLAC session NMK" log line.
std::string format_session_nmk_for_log(Nmk const& nmk);

// Log prefix identifying a matching session, same style as EvseSlac's session_log().
std::string session_log_prefix(fsm::evse::MatchingSessionData const& session_data);

} // namespace everest::lib::slac
