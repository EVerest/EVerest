// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <everest/slac/slac_defs.hpp>
#include <net/ethernet.h>

namespace everest::lib::slac::fsm::evse {

struct EvseSlacConfig {
    // MAC address of our (EVSE) PLC modem
    // FIXME (aw): is that used somehow?
    uint8_t plc_peer_mac[ETH_ALEN] = {0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    // FIXME (aw): we probably want to use std::array here
    void generate_nmk();
    uint8_t session_nmk[defs::NMK_LEN]{};

    // flag for using 5% PWM in AC mode
    bool ac_mode_five_percent{true};

    // timeout for CM_SET_KEY.REQ
    int set_key_timeout_ms = 500;

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

    bool print_state_transitions{false};
    bool provide_telemetry{false};
};

} // namespace everest::lib::slac::fsm::evse
