// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/types.hpp>

namespace everest::slac::evse {

enum class SetKeyHandlingMode {
    /// One CM_SET_KEY.REQ; only a successful confirmation is accepted, anything else fails.
    legacy_single_attempt,
    /// Retry on timeout and on a failure result; promote the new NMK only once confirmed.
    retry_confirmed,
};

/// Which CM_SET_KEY.CNF result byte counts as success. Modems disagree: HomePlug Green PHY says
/// 0x00, most QCA parts answer 0x01.
enum class SetKeyCnfSuccessMode {
    modem_compat_0x01,
    hpgp_standard_0x00,
    accept_0x00_or_0x01,
};

enum class NmkGenerationMode {
    /// Printable characters only (0-9A-Z), for compatibility with modems that expect them.
    legacy_printable,
    /// The full byte range, which reduces collision probability.
    full_byte_range,
};

struct Config {
    /// MAC address of our own PLC modem.
    MacAddress plc_peer_mac{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    Nmk session_nmk{};
    void generate_nmk();
    void generate_nmk(Nmk& target_nmk);

    SetKeyHandlingMode set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    SetKeyCnfSuccessMode set_key_cnf_success_mode = SetKeyCnfSuccessMode::modem_compat_0x01;
    NmkGenerationMode nmk_generation_mode = NmkGenerationMode::legacy_printable;

    /// 5% PWM in AC mode, per ISO 15118-3.
    bool ac_mode_five_percent{true};

    int set_key_timeout_ms = 500;
    int set_key_max_attempts = 10;

    /// TT_EVSE_SLAC_init, ISO 15118-3 Table A.1.
    int slac_init_timeout_ms = defs::TT_EVSE_SLAC_INIT_MS;

    struct ChipReset {
        bool enabled = false;
        int delay_ms = 100;
    } chip_reset;

    struct LinkStatus {
        bool do_detect = false;
        int retry_ms = 100;
        // debounce_count * poll_in_matched_state_ms must stay well below TP_match_leave (1 s):
        // PLCLinkStatus_005.
        int poll_in_matched_state_ms = 200;
        int timeout_ms = 5000;
        /// Consecutive negative LINK_STATUS answers needed to declare the link lost. Clamped to 1.
        int debounce_count = 1;
        bool debug_simulate_failed_matching = false;
    } link_status;

    /// Between the modem discovery probes.
    int request_info_delay_ms = 100;

    /// Offset added to the calculated sounding attenuation, in dB.
    int sounding_atten_adjustment = 0;

    bool reset_instead_of_fail{false};

    /// Concurrent CM_SLAC_PARM sessions the EVSE will keep. Clamped to at least 1.
    int max_matching_sessions = 4;

    bool print_state_transitions{false};
    bool provide_telemetry{false};

    bool regenerate_key_on_reset{true};

    /// CM_AMP_MAP transmit-power limitation, ISO 15118-3 A.9.6. amp_map_data holds amp_map_len
    /// 4-bit entries, two per byte; an empty map disables the transmit direction whatever the flag
    /// says. Answering an incoming CM_AMP_MAP.REQ does not depend on any of this.
    bool initiate_amp_map{false};
    std::uint16_t amp_map_len{0};
    std::vector<std::uint8_t> amp_map_data{};
};

bool accepts_set_key_cnf_success_result(SetKeyCnfSuccessMode mode, std::uint8_t result);

} // namespace everest::slac::evse
