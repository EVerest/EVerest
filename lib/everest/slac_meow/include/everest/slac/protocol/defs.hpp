// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <array>
#include <cstdint>

namespace everest::slac::defs {

// TODO (aw):
//  - is run_id 8 or 16 bytes?
//  - is nid 7 or 8 bytes?

enum class ModemVendor {
    Unknown,
    Qualcomm,
    Lumissil,
    VertexCom,
};

inline constexpr std::uint16_t ETH_P_HOMEPLUG_GREENPHY = 0x88E1;

enum class MMV : std::uint8_t {
    AV_1_0 = 0x0,
    AV_1_1 = 0x1,
    AV_2_0 = 0x2,
};

inline constexpr int MME_MIN_LENGTH = 60;

inline constexpr int STATION_ID_LEN = 17;
inline constexpr int NID_LEN = 7;
inline constexpr int NID_MOST_SIGNIFANT_BYTE_SHIFT = 4;
inline constexpr std::uint8_t NID_SECURITY_LEVEL_SIMPLE_CONNECT = 0b00;
inline constexpr int NID_SECURITY_LEVEL_OFFSET = 4;

inline constexpr std::uint8_t DAKS_HASH[] = {0x08, 0x85, 0x6d, 0xaf, 0x7c, 0xf5, 0x81, 0x85};
inline constexpr std::uint8_t NMK_HASH[] = {0x08, 0x85, 0x6d, 0xaf, 0x7c, 0xf5, 0x81, 0x86};
inline constexpr std::array<std::uint8_t, 8> NMK_HASH_ARR = {0x08, 0x85, 0x6d, 0xaf, 0x7c, 0xf5, 0x81, 0x86};

inline constexpr int NMK_LEN = 16;

inline constexpr int AAG_LIST_LEN = 58;
inline constexpr int RUN_ID_LEN = 8;

// FIXME (aw): where to put these iso15118/3 consts?
inline constexpr int C_EV_START_ATTEN_CHAR_INDS = 3;
inline constexpr int C_EV_MATCH_RETRY = 2;
// The ISO norm value for CM_SLAC_PARM.REQ retries (C_EV_MATCH_RETRY) proved insufficient in the field,
// so the legacy EV stack retried up to this many times. Kept as the default for behavioral parity.
inline constexpr int C_EV_PARM_REQ_ATTEMPTS = 100;
inline constexpr int C_EV_MATCH_MNBC = 10;
inline constexpr int TP_EV_BATCH_MSG_INTERVAL_MS =
    40; // 20ms - 50ms, interval between start_atten_char and mnbc_sound msgs
inline constexpr int TT_EV_ATTEN_RESULTS_MS = 1200; // max. 1200ms
inline constexpr int TT_EVSE_MATCH_MNBC_MS = 600;
inline constexpr int TT_MATCH_SEQUENCE_MS = 400;
inline constexpr int TT_MATCH_RESPONSE_MS = 200;
inline constexpr int TT_EVSE_MATCH_SESSION_MS = 10000;
inline constexpr int TT_EVSE_SLAC_INIT_MS = 40000; // (20s - 50s)
inline constexpr int TT_MATCH_JOIN_MS = 12000;     // max. 12s
inline constexpr int T_STEP_EF_MS = 4000;          // min. 4s

// Standard mmtypes
//
// A message is identified on the wire by its base MMTYPE ORed with the mode it is sent in, so the
// mode and category bits are declared first and each base is followed by the combinations the SLAC
// protocol actually uses. Name them here once rather than composing them at every comparison.
//
// The CM_ prefix the HomePlug specification gives these names marks the Common, station-to-station
// class - the 0x6000 category below, which every base here is in - so inside this namespace it
// would repeat on every member and is left off. The vendors' own tags go the same way: NSCM_ and
// QCA_VS_ say nothing once the vendor is in the namespace. Note qualcomm's RESET_DEVICE is 0xA01C,
// vendor specific, despite the CM_ its documentation gives it - the prefix was never a reliable
// guide to the class.
namespace mmtype {

inline constexpr std::uint16_t MODE_REQ = 0x0000;
inline constexpr std::uint16_t MODE_CNF = 0x0001;
inline constexpr std::uint16_t MODE_IND = 0x0002;
inline constexpr std::uint16_t MODE_RSP = 0x0003;
inline constexpr std::uint16_t MODE_MASK = 0x0003;

inline constexpr std::uint16_t CATEGORY_STA_CCO = 0x0000;
inline constexpr std::uint16_t CATEGORY_PROXY = 0x2000;
inline constexpr std::uint16_t CATEGORY_CCO_CCO = 0x4000;
inline constexpr std::uint16_t CATEGORY_STA_STA = 0x6000;
inline constexpr std::uint16_t CATEGORY_MANUFACTOR_SPECIFIC = 0x8000;
inline constexpr std::uint16_t CATEGORY_VENDOR_SPECIFIC = 0xA000;
inline constexpr std::uint16_t CATEGORY_MASK = 0xE000;

inline constexpr std::uint16_t SET_KEY = 0x6008;
inline constexpr std::uint16_t SET_KEY_REQ = SET_KEY | MODE_REQ;
inline constexpr std::uint16_t SET_KEY_CNF = SET_KEY | MODE_CNF;

inline constexpr std::uint16_t SLAC_PARAM = 0x6064;
inline constexpr std::uint16_t SLAC_PARAM_REQ = SLAC_PARAM | MODE_REQ;
inline constexpr std::uint16_t SLAC_PARAM_CNF = SLAC_PARAM | MODE_CNF;

inline constexpr std::uint16_t START_ATTEN_CHAR = 0x6068;
inline constexpr std::uint16_t START_ATTEN_CHAR_IND = START_ATTEN_CHAR | MODE_IND;

inline constexpr std::uint16_t ATTEN_CHAR = 0x606C;
inline constexpr std::uint16_t ATTEN_CHAR_IND = ATTEN_CHAR | MODE_IND;
inline constexpr std::uint16_t ATTEN_CHAR_RSP = ATTEN_CHAR | MODE_RSP;

inline constexpr std::uint16_t MNBC_SOUND = 0x6074;
inline constexpr std::uint16_t MNBC_SOUND_IND = MNBC_SOUND | MODE_IND;

inline constexpr std::uint16_t VALIDATE = 0x6078;
inline constexpr std::uint16_t VALIDATE_REQ = VALIDATE | MODE_REQ;
inline constexpr std::uint16_t VALIDATE_CNF = VALIDATE | MODE_CNF;

inline constexpr std::uint16_t SLAC_MATCH = 0x607C;
inline constexpr std::uint16_t SLAC_MATCH_REQ = SLAC_MATCH | MODE_REQ;
inline constexpr std::uint16_t SLAC_MATCH_CNF = SLAC_MATCH | MODE_CNF;

inline constexpr std::uint16_t ATTEN_PROFILE = 0x6084;
inline constexpr std::uint16_t ATTEN_PROFILE_IND = ATTEN_PROFILE | MODE_IND;

// CM_AMP_MAP (HomePlug GreenPHY amplitude map, AV 2.0) reduces the transmit power of selected OFDM
// carriers - ISO 15118-3 A.9.6, transmit-power limitation. Not backward compatible with AV 1.1, so
// it must be framed AV 2.0.
inline constexpr std::uint16_t AMP_MAP = 0x601C;
inline constexpr std::uint16_t AMP_MAP_REQ = AMP_MAP | MODE_REQ;
inline constexpr std::uint16_t AMP_MAP_CNF = AMP_MAP | MODE_CNF;

// Qualcomm Vendor MMEs
namespace qualcomm {
inline constexpr std::uint16_t RESET_DEVICE = 0xA01C;
inline constexpr std::uint16_t RESET_DEVICE_REQ = RESET_DEVICE | MODE_REQ;
inline constexpr std::uint16_t RESET_DEVICE_CNF = RESET_DEVICE | MODE_CNF;

inline constexpr std::uint16_t LINK_STATUS = 0xA0B8;
inline constexpr std::uint16_t LINK_STATUS_REQ = LINK_STATUS | MODE_REQ;
inline constexpr std::uint16_t LINK_STATUS_CNF = LINK_STATUS | MODE_CNF;

inline constexpr std::uint16_t OP_ATTR = 0xA068;
inline constexpr std::uint16_t OP_ATTR_REQ = OP_ATTR | MODE_REQ;
inline constexpr std::uint16_t OP_ATTR_CNF = OP_ATTR | MODE_CNF;

inline constexpr std::uint16_t NW_INFO = 0xA038;
inline constexpr std::uint16_t GET_SW = 0xA000;
inline constexpr std::uint16_t ATTENUATION_CHARACTERISTICS = 0xA14E;
} // namespace qualcomm

// Lumissil Vendor MMEs
namespace lumissil {
inline constexpr std::uint16_t RESET_DEVICE = 0xAC70;
inline constexpr std::uint16_t RESET_DEVICE_REQ = RESET_DEVICE | MODE_REQ;

inline constexpr std::uint16_t GET_VERSION = 0xAC6C;
inline constexpr std::uint16_t GET_VERSION_REQ = GET_VERSION | MODE_REQ;
inline constexpr std::uint16_t GET_VERSION_CNF = GET_VERSION | MODE_CNF;

inline constexpr std::uint16_t GET_D_LINK_STATUS = 0xAC9C;
inline constexpr std::uint16_t GET_D_LINK_STATUS_REQ = GET_D_LINK_STATUS | MODE_REQ;
inline constexpr std::uint16_t GET_D_LINK_STATUS_CNF = GET_D_LINK_STATUS | MODE_CNF;
} // namespace lumissil

} // namespace mmtype

// Common D-Link/link status values
inline constexpr std::uint8_t D_LINK_STATUS_LINKED = 0x01;

inline constexpr std::uint8_t COMMON_APPLICATION_TYPE = 0x00;
inline constexpr std::uint8_t COMMON_SECURITY_TYPE = 0x00;

// Values the fields of a particular MME carry - a Management Message Entry, HomePlug's name for
// the messages this protocol is made of. These are not the messages: the wire structs for those
// live in `messages::`, and the code identifying one on the wire is in `mmtype::` above. What is
// here is what goes *into* a field, or what a received field is compared against.
//
// Each namespace is named for the message whose field it is, which is why set_key_req and
// set_key_cnf are split by direction while validate is not: validate's SIGNAL_TYPE is used on both
// the REQ and the CNF, so splitting it would make the name lie.
namespace mme {

namespace set_key_req {
inline constexpr std::uint8_t KEY_TYPE_NMK = 0x01; // NMK (AES-128), Network Management Key
inline constexpr std::uint8_t PID_HLE = 0x04;
inline constexpr std::uint16_t PRN_UNUSED = 0x0000;
inline constexpr std::uint8_t PMN_UNUSED = 0x00;
inline constexpr std::uint8_t CCO_CAP_NONE = 0x00; // Level-0 CCo Capable, neither QoS nor TDMA
inline constexpr std::uint8_t PEKS_NMK_KNOWN_TO_STA = 0x01;
} // namespace set_key_req
namespace set_key_cnf {
inline constexpr std::uint8_t RESULT_HPGP_SUCCESS = 0x00;
inline constexpr std::uint8_t RESULT_MODEM_COMPAT_SUCCESS = 0x01;
// Compatibility alias: QCA/practical modem behavior returns 0x01 on success.
inline constexpr std::uint8_t RESULT_SUCCESS = RESULT_MODEM_COMPAT_SUCCESS;
} // namespace set_key_cnf
namespace slac_parm_cnf {
inline constexpr std::uint8_t RESP_TYPE = 0x01; // = other GP station
inline constexpr std::uint8_t NUM_SOUNDS = 10;  // typical value
inline constexpr std::uint8_t TIMEOUT = 0x06;   // 600ms
} // namespace slac_parm_cnf
// CM_VALIDATE (ISO 15118-3 9.4). The result codes travel in the CNF, not the REQ, which the old
// CM_VALIDATE_REQ_RESULT_ names claimed.
namespace atten_char_rsp {
inline constexpr std::uint8_t RESULT = 0x00; // the only value the RSP may carry
} // namespace atten_char_rsp

namespace slac_match_req {
inline constexpr std::uint16_t MVF_LENGTH = 0x3e;
} // namespace slac_match_req

namespace slac_match_cnf {
inline constexpr std::uint16_t MVF_LENGTH = 0x56;
} // namespace slac_match_cnf

namespace amp_map_cnf {
inline constexpr std::uint8_t RESULT_SUCCESS = 0x00;
} // namespace amp_map_cnf

namespace validate {
inline constexpr std::uint8_t SIGNAL_TYPE = 0x00;
inline constexpr std::uint8_t RESULT_NOT_READY = 0x00;
inline constexpr std::uint8_t RESULT_READY = 0x01;
inline constexpr std::uint8_t RESULT_SUCCESS = 0x02;
inline constexpr std::uint8_t RESULT_FAILURE = 0x03;
inline constexpr std::uint8_t RESULT_NOT_REQUIRED = 0x04;
} // namespace validate
// Vendor MMEs subdivide by vendor here exactly as they do in mmtype.
namespace qualcomm {
// Fields of OP_ATTR.CNF - messages::qualcomm::op_attr_cnf. The first two pick the zero-crossing
// sub-field out of the packed line_freq_zc byte; the rest are values it may hold.
namespace op_attr {
inline constexpr std::uint8_t LINE_FREQ_ZC_MASK = 0x03;
inline constexpr std::uint8_t ZC_SIGNAL_SHIFT = 2;
inline constexpr std::uint8_t ZC_SIGNAL_DETECTED = 0x01;
inline constexpr std::uint8_t ZC_SIGNAL_MISSING = 0x02;
inline constexpr std::uint8_t LINE_FREQUENCY_50HZ = 0x01;
inline constexpr std::uint8_t LINE_FREQUENCY_60HZ = 0x02;
} // namespace op_attr
} // namespace qualcomm

} // namespace mme

inline constexpr std::uint8_t BROADCAST_MAC_ADDRESS[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

} // namespace everest::slac::defs
