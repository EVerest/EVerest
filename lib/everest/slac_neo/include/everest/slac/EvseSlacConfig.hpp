// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>

#include <cstdint>
#include <vector>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_types.hpp>

namespace everest::lib::slac::fsm::evse {

enum class SetKeyHandlingMode {
    legacy_single_attempt,
    retry_confirmed,
};

enum class SetKeyCnfSuccessMode {
    modem_compat_0x01,
    hpgp_standard_0x00,
    accept_0x00_or_0x01,
};

enum class NmkGenerationMode {
    full_byte_range,
    legacy_printable,
};

struct EvseSlacConfig {
    // MAC address of our (EVSE) PLC modem
    // FIXME (aw): is that used somehow?
    MacAddress plc_peer_mac{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    // FIXME (aw): we probably want to use std::array here
    void generate_nmk();
    void generate_nmk(Nmk& target_nmk);
    void generate_nmk(std::uint8_t* target_nmk);
    Nmk session_nmk{};

    SetKeyHandlingMode set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;

    SetKeyCnfSuccessMode set_key_cnf_success_mode = SetKeyCnfSuccessMode::modem_compat_0x01;

    NmkGenerationMode nmk_generation_mode = NmkGenerationMode::legacy_printable;

    // flag for using 5% PWM in AC mode
    bool ac_mode_five_percent{true};

    // timeout for CM_SET_KEY.REQ
    std::chrono::milliseconds set_key_timeout{500};

    // maximum amount of attempts to send CM_SET_KEY.REQ
    int set_key_max_attempts = 10;

    // timeout for CM_SLAC_PARM.REQ
    std::chrono::milliseconds slac_init_timeout{defs::TT_EVSE_SLAC_INIT_MS};

    // Settings CM_DEVICE_RESET.REQ
    struct chip_reset_struct {
        bool enabled = false;
        std::chrono::milliseconds timeout{500};
        std::chrono::milliseconds delay{100};
    } chip_reset;

    // Settings for LINK_STATUS detection
    struct link_status_struct {
        bool do_detect = false;
        std::chrono::milliseconds retry{100};
        // Detection of a link loss takes up to debounce_count consecutive polls; keep
        // debounce_count * poll_in_matched_state well below TP_match_leave (1 s) so the SECC
        // leaves the AVLN in time on connection loss (ISO 15118-3, PLCLinkStatus_005).
        std::chrono::milliseconds poll_in_matched_state{200};
        std::chrono::milliseconds timeout{5000};
        int debounce_count = 1;
        bool debug_simulate_failed_matching = false;
    } link_status;

    std::chrono::milliseconds request_info_delay{100};

    // offset for adjusting the calculated sounding attenuation
    int sounding_atten_adjustment = 0;

    bool reset_instead_of_fail{false};

    int max_matching_sessions = 4;

    bool print_state_transitions{false};
    bool provide_telemetry{false};

    bool regenerate_key_on_reset{true};

    // CM_AMP_MAP (ISO 15118-3 A.9.6 transmit-power limitation). When
    // initiate_amp_map is true the SECC transmits a CM_AMP_MAP.REQ once the AVLN
    // is established. amp_map_len is the number of 4-bit amplitude entries and
    // amp_map_data holds them pre-packed (2 per byte); an empty map with
    // amp_map_len == 0 disables the transmit direction even if the flag is set.
    // The map is populated by the module from an operator-provided YAML file
    // (default: all carriers at maximum TX). The SECC always answers an incoming
    // CM_AMP_MAP.REQ with CM_AMP_MAP.CNF(result=0x00) regardless of this flag.
    bool initiate_amp_map{false};
    std::uint16_t amp_map_len{0};
    std::vector<std::uint8_t> amp_map_data{};
};

} // namespace everest::lib::slac::fsm::evse
