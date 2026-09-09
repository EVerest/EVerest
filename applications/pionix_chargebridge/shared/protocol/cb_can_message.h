// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "cb_platform.h"
#include <stdint.h>

/* Enum definitions */
typedef enum _CanErrorState {
    CanErrorState_Error_Active = 0,
    CanErrorState_Error_Passive = 1,
    CanErrorState_ForceSize = 0xFFFFFFFF,
} CanErrorState;

typedef enum _CanBitrate {
    CanBitrate_125kbps = 0,
    CanBitrate_250kbps = 1,
    CanBitrate_500kbps = 2,
    CanBitrate_1000kbps = 3,
    CanBitrate_ForceSize = 0xFFFFFFFF,
} CanBitrate;

typedef enum _CanFDBitrate {
    CanFDBitrate_1MBps = 0,
    CanFDBitrate_2MBps = 1,
    CanFDBitrate_3MBps = 2,
    CanFDBitrate_4MBps = 3,
    CanFDBitrate_5MBps = 4,
    CanFDBitrate_6MBps = 5,
    CanFDBitrate_7MBps = 6,
    CanFDBitrate_8MBps = 7,
    CanFDBitrate_ForceSize = 0xFFFFFFFF,
} CanFDBitrate;

typedef enum _CanFlags {
    CanFlags_EFF = 1,
    CanFlags_RTR = 1 << 1,
    CanFlags_ERR = 1 << 2,
} CanFlags;

typedef struct CB_COMPILER_ATTR_PACK _CanStatistics {
    // tx: direction is from host to bus
    // rx: direction is from bus to host
    uint32_t frames_tx;
    uint32_t frames_rx;
    uint32_t event_rx_buf_full;
    uint32_t event_tx_buf_full;
} CanStatistics;

typedef enum _CanPacketType : uint8_t{
    CanPacketType_Regular = 0,
    CanPacketType_Keep_Alive = 1,
} CanPacketType;

/* Since 2026-09 a CAN UDP datagram carries 1..N of these records back to
 * back (N bounded by the datagram size; records are always full-size, so the
 * count is byte_count / sizeof). Both senders batch with a ~3 ms hold-off
 * ("mini Nagle": a frame following another within 3 ms waits for followers,
 * up to 12 per datagram; a frame after a quiet gap leaves immediately) and
 * receivers must iterate. NOTE: this is not gated by a
 * version (CB_CONFIG_VERSION only versions the config struct, which is
 * unchanged): an old receiver silently reads only the first record of a
 * batch, so deploy firmware and tool together. */
struct CB_COMPILER_ATTR_PACK cb_can_message {
    uint8_t version;
    CanPacketType packet_type; // 0: regular CAN packet, 1: dummy keep-alive packet
    CanErrorState error_state;
    CanStatistics statistics;
    CanBitrate bitrate;
    CanFDBitrate fd_bitrate; /* integer in MBit (1-8) */
    uint8_t can_flags;   // EFF, RTR, ERR
    uint32_t can_id;

    /* dlc 0..8: standard CAN frame with up to 8 bytes
     * FDCAN dlc:
     *  9: 12 bytes
     *  10: 16 bytes
     *  11: 20 bytes
     *  12: 24 bytes
     *  13: 32 bytes
     *  14: 48 bytes
     *  15: 64 bytes
     */
    uint8_t dlc;

    // Note: in UDP transmission, data bytes at the end may be omitted in the message.
    // Always check dlc first before accessing the data
    uint8_t data[64];
};

#define cb_can_message_set_zero                                                                                        \
    {0, CanPacketType_Regular, CanErrorState_Error_Active, {0, 0, 0, 0}, CanBitrate_125kbps, CanFDBitrate_1MBps, 0, 0, \
     0, {0, 0, 0, 0, 0, 0, 0, 0}};

#include "test/cb_can_message_test.h"
