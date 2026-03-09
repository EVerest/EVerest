// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <array>
#include <cstdint>

namespace everest::lib::slac::defs {

// TODO (aw):
//  - is run_id 8 or 16 bytes?
//  - is nid 7 or 8 bytes?

enum class ModemVendor {
    Unknown,
    Qualcomm,
    Lumissil,
    VertexCom,
};

const std::uint16_t ETH_P_HOMEPLUG_GREENPHY = 0x88E1;

enum class MMV : std::uint8_t {
    AV_1_0 = 0x0,
    AV_1_1 = 0x1,
    AV_2_0 = 0x2,
};

const int MME_MIN_LENGTH = 60;

const int STATION_ID_LEN = 17;
const int NID_LEN = 7;
const int NID_MOST_SIGNIFANT_BYTE_SHIFT = 4;
const std::uint8_t NID_SECURITY_LEVEL_SIMPLE_CONNECT = 0b00;
const int NID_SECURITY_LEVEL_OFFSET = 4;

const std::uint8_t DAKS_HASH[] = {0x08, 0x85, 0x6d, 0xaf, 0x7c, 0xf5, 0x81, 0x85};
const std::uint8_t NMK_HASH[] = {0x08, 0x85, 0x6d, 0xaf, 0x7c, 0xf5, 0x81, 0x86};
const std::array<std::uint8_t, 8> NMK_HASH_ARR = {0x08, 0x85, 0x6d, 0xaf, 0x7c, 0xf5, 0x81, 0x86};

const int NMK_LEN = 16;

const int AAG_LIST_LEN = 58;
const int RUN_ID_LEN = 8;

// FIXME (aw): where to put these iso15118/3 consts?
const int C_EV_START_ATTEN_CHAR_INDS = 3;
const int C_EV_MATCH_RETRY = 2;
const int C_EV_MATCH_MNBC = 10;
const int TP_EV_BATCH_MSG_INTERVAL_MS = 40; // 20ms - 50ms, interval between start_atten_char and mnbc_sound msgs
const int TT_EV_ATTEN_RESULTS_MS = 1200;    // max. 1200ms
const int TT_EVSE_MATCH_MNBC_MS = 600;
const int TT_MATCH_SEQUENCE_MS = 400;
const int TT_MATCH_RESPONSE_MS = 200;
const int TT_EVSE_MATCH_SESSION_MS = 10000;
const int TT_EVSE_SLAC_INIT_MS = 40000; // (20s - 50s)
const int TT_MATCH_JOIN_MS = 12000;     // max. 12s
const int T_STEP_EF_MS = 4000;          // min. 4s

const std::uint16_t MMTYPE_CM_SET_KEY = 0x6008;
const std::uint16_t MMTYPE_CM_SLAC_PARAM = 0x6064;
const std::uint16_t MMTYPE_CM_START_ATTEN_CHAR = 0x6068;
const std::uint16_t MMTYPE_CM_ATTEN_CHAR = 0x606C;
const std::uint16_t MMTYPE_CM_MNBC_SOUND = 0x6074;
const std::uint16_t MMTYPE_CM_VALIDATE = 0x6078;
const std::uint16_t MMTYPE_CM_SLAC_MATCH = 0x607C;
const std::uint16_t MMTYPE_CM_ATTEN_PROFILE = 0x6084;

// Qualcomm Vendor MMEs
namespace qualcomm {
const std::uint16_t MMTYPE_CM_RESET_DEVICE = 0xA01C;
const std::uint16_t MMTYPE_LINK_STATUS = 0xA0B8;
const std::uint16_t MMTYPE_OP_ATTR = 0xA068;
const std::uint16_t MMTYPE_NW_INFO = 0xA038;
const std::uint16_t MMTYPE_GET_SW = 0xA000;
} // namespace qualcomm

// Lumissil Vendor MMEs
namespace lumissil {
const std::uint16_t MMTYPE_NSCM_RESET_DEVICE = 0xAC70;
const std::uint16_t MMTYPE_NSCM_GET_VERSION = 0xAC6C;
const std::uint16_t MMTYPE_NSCM_GET_D_LINK_STATUS = 0xAC9C;
} // namespace lumissil

// Standard mmtypes
const std::uint16_t MMTYPE_MODE_REQ = 0x0000;
const std::uint16_t MMTYPE_MODE_CNF = 0x0001;
const std::uint16_t MMTYPE_MODE_IND = 0x0002;
const std::uint16_t MMTYPE_MODE_RSP = 0x0003;
const std::uint16_t MMTYPE_MODE_MASK = 0x0003;

const std::uint16_t MMTYPE_CATEGORY_STA_CCO = 0x0000;
const std::uint16_t MMTYPE_CATEGORY_PROXY = 0x2000;
const std::uint16_t MMTYPE_CATEGORY_CCO_CCO = 0x4000;
const std::uint16_t MMTYPE_CATEGORY_STA_STA = 0x6000;
const std::uint16_t MMTYPE_CATEGORY_MANUFACTOR_SPECIFIC = 0x8000;
const std::uint16_t MMTYPE_CATEGORY_VENDOR_SPECIFIC = 0xA000;
const std::uint16_t MMTYPE_CATEGORY_MASK = 0xE000;

const std::uint8_t COMMON_APPLICATION_TYPE = 0x00;
const std::uint8_t COMMON_SECURITY_TYPE = 0x00;

const std::uint8_t CM_VALIDATE_REQ_SIGNAL_TYPE = 0x00;
const std::uint8_t CM_VALIDATE_REQ_RESULT_READY = 0x01;
const std::uint8_t CM_VALIDATE_REQ_RESULT_FAILURE = 0x03;

const std::uint16_t CM_SLAC_MATCH_REQ_MVF_LENGTH = 0x3e;

const std::uint16_t CM_SLAC_MATCH_CNF_MVF_LENGTH = 0x56;

const std::uint8_t CM_SLAC_PARM_CNF_RESP_TYPE = 0x01; // = other GP station
const std::uint8_t CM_SLAC_PARM_CNF_NUM_SOUNDS = 10;  // typical value
const std::uint8_t CM_SLAC_PARM_CNF_TIMEOUT = 0x06;   // 600ms

const std::uint8_t CM_SET_KEY_REQ_KEY_TYPE_NMK = 0x01; // NMK (AES-128), Network Management Key
const std::uint8_t CM_SET_KEY_REQ_PID_HLE = 0x04;
const std::uint16_t CM_SET_KEY_REQ_PRN_UNUSED = 0x0000;
const std::uint8_t CM_SET_KEY_REQ_PMN_UNUSED = 0x00;
const std::uint8_t CM_SET_KEY_REQ_CCO_CAP_NONE = 0x00; // Level-0 CCo Capable, neither QoS nor TDMA
const std::uint8_t CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA = 0x01;

const std::uint8_t CM_SET_KEY_CNF_RESULT_SUCCESS = 0x0;

const std::uint8_t CM_ATTEN_CHAR_RSP_RESULT = 0x00;

const std::uint8_t BROADCAST_MAC_ADDRESS[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

} // namespace everest::lib::slac::defs
