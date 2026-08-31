// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/**
 * \file Test utilities used to determine that the used types
 * have the same sizes independent of the platforms that we
 * are using. Added to make sure that reinterpret_cast or other
 * types of cast will yield the same types across platform
 */
#pragma once

CB_STATIC_ASSERT(sizeof(AppUDPResponse) == 4, "Wrong AppUDPReponse type size!");
CB_STATIC_ASSERT(sizeof(CbType) == 1, "Wrong CB type size!");
CB_STATIC_ASSERT(sizeof(CbStructType) == 2, "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbFirmwareStart) == 16 + 1 && sizeof(CbFirmwareStart) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbFirmwarePacket) == 1 + 2 + 2 + 1024 && sizeof(CbFirmwarePacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbFirmwareEnd) == 4 + 1 + (128 + 1) && sizeof(CbFirmwareEnd) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
// The trailing +1 is CbConfig.cb_type (charge_bridge.type), CB_CONFIG_VERSION 6.
CB_STATIC_ASSERT((sizeof(CbHeartbeatPacket) == 225 + 1 && sizeof(CbHeartbeatPacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT(sizeof(CbConfig) == sizeof(CbHeartbeatPacket), "CbHeartbeatPacket is just a CbConfig!");
CB_STATIC_ASSERT(sizeof(CbLinkTechnology) == 1, "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbLinkStatusPacket) == 8 && sizeof(CbLinkStatusPacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
// 53 bytes of measurements + the embedded 8 byte CbLinkStatusPacket + 1 byte latched_cb_type
CB_STATIC_ASSERT((sizeof(CbHeartbeatReplyPacket) == 53 + sizeof(CbLinkStatusPacket) + 1 &&
                  sizeof(CbHeartbeatReplyPacket) == 62 &&
                  sizeof(CbHeartbeatReplyPacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbDebugUartLinePacket) == 2 + CB_DEBUG_UART_LINE_MAX &&
                  sizeof(CbDebugUartLinePacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT(sizeof(CbTelemetryEntry) == CB_TELEMETRY_NAME_LEN + 4, "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbTelemetry) == 1 + CB_TELEMETRY_MAX_ENTRIES * (CB_TELEMETRY_NAME_LEN + 4) &&
                  sizeof(CbTelemetry) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbIoPacket) ==
                      1 + (CB_NUMBER_OF_GPIOS * 2) + 1 + (CB_NUMBER_OF_ADCS * 4) + sizeof(CbTelemetry) &&
                  sizeof(CbIoPacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbWs28AnimPacket) == 12 && sizeof(CbWs28AnimPacket) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
CB_STATIC_ASSERT((sizeof(CbWs28Packet) == 4 + (CB_WS28_MAX_LEDS * 3) &&
                  sizeof(CbWs28Packet) <= CB_MAX_CB_STRUCT_SIZE),
                 "Wrong CB type size!");
