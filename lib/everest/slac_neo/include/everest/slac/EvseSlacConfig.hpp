// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
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
    int set_key_timeout_ms = 500;

    // maximum amount of attempts to send CM_SET_KEY.REQ
    int set_key_max_attempts = 10;

    // timeout for CM_SLAC_PARM.REQ
    int slac_init_timeout_ms = defs::TT_EVSE_SLAC_INIT_MS;

    // Settings CM_DEVICE_RESET.REQ
    struct chip_reset_struct {
        bool enabled = false;
        int timeout_ms = 500;
        int delay_ms = 100;
    } chip_reset;

    // Settings for LINK_STATUS detection
    struct link_status_struct {
        bool do_detect = false;
        int retry_ms = 100;
        int poll_in_matched_state_ms = 1000;
        int timeout_ms = 5000;
        bool debug_simulate_failed_matching = false;
    } link_status;

    int request_info_delay_ms = 100;

    // offset for adjusting the calculated sounding attenuation
    int sounding_atten_adjustment = 0;

    bool reset_instead_of_fail{false};

    int max_matching_sessions = 4;

    bool print_state_transitions{false};
    bool provide_telemetry{false};

    bool regenerate_key_on_reset{true};
};

} // namespace everest::lib::slac::fsm::evse
