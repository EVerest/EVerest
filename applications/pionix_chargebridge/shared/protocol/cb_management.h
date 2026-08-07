// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/**
 * \file Common structs used between linux and the STM32 dev board. Data
 * will be sent raw over the UDP socket, with the sender using
 * \ref send(struct, sizeof(CbStruct))
 * and the receiver using
 * \ref receive(raw_bytes, sizeof(CbStruct))
 * CbStruct *struct = reinterpret_cast<CbStruct *>(raw_bytes);
 *
 * Notes:
 * 1) After V1 structs will not be able to remove fields, only add fields
 *    after the existing fields
 * 2) There can be problems with variable length structs, for example
 *    CbFirmwarePacket that can have a payload with lengths 0-1024
 *
 * Test files are added at the end that check the sizes of the structs
 * at compile time, to determine the fact that they are consistent
 * across platforms.
 */
#pragma once

#include <stdint.h>

#include "cb_config.h"

#ifndef __cplusplus
#error "This header is C++ only"
#endif

enum class AppUDPResponse : uint32_t {
	AUR_Ok = 0x500D500D, AUR_Bad = 0xBADBAD00,
};

enum class CbType : uint8_t {
	CCS_EVSE = 0, CCS_EV = 1,
};

/*
 * What type of message is on the socket
 */
enum class CbStructType : uint16_t {

	// track IP with timeout and port
	// Housekeepig, heartbeat/config (safety_config, serial port(fixed)/CAN bitrate, gpio config, mdns module name), fw version, softreset,
	CST_HostToCb_Heartbeat = 1,
	CST_CbToHost_Heartbeat = 2,

	// track IP with timeout and port
	// GPIO client (host -> MCU: write GPIO outputs)
	CST_HostToCb_Gpio = 3,

	// Combined I/O report (MCU -> host): GPIO inputs + calibrated ADC values in
	// one packet. Pushed on GPIO change / significant ADC change / periodically,
	// and as a reply to each host CST_HostToCb_Gpio poll. Replaces the former
	// separate CST_CbToHost_Gpio (4) and CST_CbToHost_Adc (6).
	CST_CbToHost_Io = 4,

	// WS2812/NeoPixel pixel data (host -> MCU): RGB frame for a WS28_LED strip.
	CST_HostToCb_Ws28 = 5,

	// WS2812/NeoPixel animation command (host -> MCU): selects an autonomous animation style the
	// MCU renders on its own (no per-frame pixel streaming). See CbWs28AnimPacket.
	CST_HostToCb_Ws28Anim = 6,

	// Debug-UART line forwarding (MCU -> host): one text line of the MCU's printf/debug output,
	// sent on the IO connection when CbConfig.debug_uart_udp_enabled is set. See CbDebugUartLinePacket.
	CST_CbToHost_DebugUart = 7,

	// FW update
	CST_CbFirmwareReply = 0xFFF9,
	CST_CbFirmwareStart = 0xFFFA,
	CST_CbFirmwarePacket = 0xFFFB,
	CST_CbFirmwareFinish = 0xFFFC,
	CST_CbFirmwareUpdateCancel = 0xFFFD,
	CST_CbFirmwarePing = 0xFFFE,
	CST_CbFirmwareGetVersion = 0xFFFF,
};


/*
 This container message is used for all generic module management packets
 */


template<typename T>
struct CB_COMPILER_ATTR_PACK CbManagementPacket {
	CbStructType type;
	T data;
};

template<> struct CB_COMPILER_ATTR_PACK CbManagementPacket<void> {
	CbStructType type;
};


// Host -> MCU: GPIO output write (and connection keepalive/poll).
struct CB_COMPILER_ATTR_PACK CbGpioPacket {
	uint8_t number_of_gpios; // Just to check compatibility
	uint16_t gpio_values[CB_NUMBER_OF_GPIOS]; // Actual value, 0: low, 1: high, or duty cycle for PWM
};

// Host -> MCU: WS2812/NeoPixel pixel frame for a WS28_LED strip. Fixed-size like CbGpioPacket
// (the whole struct is sent); only the first led_count pixels of rgb[] are meaningful. Pixels are
// R,G,B per LED in LED order -- the firmware reorders to the WS2812 GRB wire order.
struct CB_COMPILER_ATTR_PACK CbWs28Packet {
	uint8_t gpio_index;                 // target GPIO (must be a WS28_LED pin; only 8 today)
	uint8_t reserved;                   // 0; reserved for future flags (e.g. RGBW)
	uint16_t led_count;                 // number of valid LEDs in rgb[]
	uint8_t rgb[CB_WS28_MAX_LEDS * 3];  // R,G,B per LED, in LED order
};

// Host -> MCU: WS2812/NeoPixel animation command. Selects one of the autonomous animation styles the
// MCU renders locally (the strip keeps animating with no further host traffic) until a new command or
// a static pixel frame (CbWs28Packet) arrives. style values match Ws28AnimStyle in ws28_led.hpp.
// style 0 (STATIC) shows the last host-pushed pixel frame; the other styles are colour-generated.
struct CB_COMPILER_ATTR_PACK CbWs28AnimPacket {
	uint8_t gpio_index;  // target GPIO (must be a WS28_LED pin; only 8 today)
	uint8_t style;       // Ws28AnimStyle: 0 STATIC,1 BLINK,2 BREATHE,3 WIPE,4 THEATER,5 SCANNER,
	                     // 6 COMET,7 RAINBOW,8 RAINBOW_CHASE,9 SPARKLE,10 GRADIENT,11 FIRE
	uint8_t speed;       // 0..255 animation rate (style-relative; higher = faster)
	uint8_t brightness;  // 0..255 master brightness scale applied to the whole strip
	uint8_t r1, g1, b1;  // colour1 (primary)
	uint8_t r2, g2, b2;  // colour2 (background / gradient end; unused by some styles)
	uint8_t param;       // style-specific (tail length / chase gap / sparkle density / fire cooling)
	uint8_t flags;       // bit0 = reverse direction, bit1 = scroll (gradient); rest reserved 0
};

// MCU -> host: a chunk of the MCU's debug-UART (printf) byte stream, forwarded over UDP on the IO
// connection when CbConfig.debug_uart_udp_enabled is set. The chunk is raw bytes and may contain
// several '\n'-separated lines (including a partial trailing line); the host reassembles the stream
// and splits on '\n'. VARIABLE LENGTH ON THE WIRE: only the first `length` bytes of text[] are
// transmitted, so the sent payload is sizeof(uint16_t) + length bytes (no NUL terminator). The MCU
// buffers printf bytes in a ring, dropping the oldest byte on overflow, so output produced before
// the host connects is retained and flushed once forwarding is enabled.
#define CB_DEBUG_UART_LINE_MAX 128
struct CB_COMPILER_ATTR_PACK CbDebugUartLinePacket {
	uint16_t length;                      // bytes of text[] actually used (1..CB_DEBUG_UART_LINE_MAX)
	char     text[CB_DEBUG_UART_LINE_MAX]; // raw byte chunk (may contain newlines), not NUL-terminated
};

// Generic, unstructured telemetry carried inside the combined IO packet (see CbIoPacket).
// Each entry is just a name and a raw integer value; the MCU owns the set of names and what
// they mean. The host does NOT interpret telemetry - it simply republishes every entry as
// telemetry/<name> = <value> to MQTT for other apps to consume. Add new telemetry by emitting
// more entries on the MCU; no protocol/host change is needed as long as the entry count stays
// within CB_TELEMETRY_MAX_ENTRIES.
#define CB_TELEMETRY_NAME_LEN 12   // max name length including the NUL terminator (<=11 chars)
#define CB_TELEMETRY_MAX_ENTRIES 32 // max entries carried in one IO packet

struct CB_COMPILER_ATTR_PACK CbTelemetryEntry {
	char name[CB_TELEMETRY_NAME_LEN]; // NUL-terminated ASCII; unused tail bytes are 0
	int32_t value;                    // raw value, published verbatim
};

struct CB_COMPILER_ATTR_PACK CbTelemetry {
	uint8_t number_of_entries; // count of valid entries[] (0..CB_TELEMETRY_MAX_ENTRIES)
	CbTelemetryEntry entries[CB_TELEMETRY_MAX_ENTRIES];
};

// MCU -> host: combined I/O report (CST_CbToHost_Io). GPIO inputs, the calibrated generic ADC
// values and the unstructured telemetry snapshot are reported together in a single packet. The
// telemetry block rides along whenever this packet is transmitted (GPIO change / significant
// ADC change / periodic / poll reply); it never triggers a transmission on its own.
//
// VARIABLE LENGTH ON THE WIRE: only telemetry.number_of_entries entries are transmitted, so the
// sent length is (sizeof(CbIoPacket) - sizeof(telemetry.entries)) + number_of_entries *
// sizeof(CbTelemetryEntry), i.e. the fixed GPIO/ADC prefix + the telemetry count byte + that many
// entries. The struct size below is the MAXIMUM (full entries[] capacity); the receiver must
// accept any length in [fixed_prefix, sizeof(CbIoPacket)] and validate it against number_of_entries.
struct CB_COMPILER_ATTR_PACK CbIoPacket {
	uint8_t number_of_gpios; // Just to check compatibility
	uint16_t gpio_values[CB_NUMBER_OF_GPIOS]; // Actual value, 0: low, 1: high, or duty cycle for PWM
	uint8_t number_of_adcs; // Just to check compatibility
	int32_t adc_values[CB_NUMBER_OF_ADCS]; // Calibrated values: raw mV run through the per-channel transfer function; unit depends on the channel mode (mV, m degC, ...). Signed so temperature channels can report negative values.
	CbTelemetry telemetry; // Generic unstructured telemetry; variable length, rides along, never triggers sends
};

struct CB_COMPILER_ATTR_PACK CbHeartbeatPacket {
    CbConfig module_config;
};

struct CB_COMPILER_ATTR_PACK CbHeartbeatReplyPacket {
	int32_t cp_hi_mV;
	int32_t cp_lo_mV;
	int32_t vdd_core;
	int32_t vdd_3v3;
	int32_t vdd_refint;
	int32_t vdd_12V;
	int32_t vdd_N12V;
	int32_t pp_mOhm;
	int32_t pp_voltage_mV;
	uint8_t relay_state_feedback[3];
	int16_t temperature_mcu_C;
	int16_t temperature_pcb_C;
	int16_t temperature_modem_C;
	int16_t temperature_PT1000_C[2];
	int32_t uptime_ms;
};

struct CB_COMPILER_ATTR_PACK CbFirmwareStart {
	uint8_t is_secure_fw :1;
	uint8_t requires_crc_verification :1;
	uint8_t requires_sha256_verification :1;
	uint8_t requires_signature_verification :1;
	uint8_t requires_decryption :1;

	// IV in case we require decryption
	uint8_t iv[16];
};

struct CB_COMPILER_ATTR_PACK CbFirmwarePacket {
	// If it is the last packet sent, used when we need to take care of
	// the padding bytes in case of decryption or other operations
	uint8_t last_packet :1;

	uint16_t sector;
	uint16_t data_len;
	uint8_t data[1024];
};

struct CB_COMPILER_ATTR_PACK CbFirmwareEnd {
	uint32_t firmware_len;

	// Signature for the firmware in the faw format
	uint8_t fw_signature[128];
	uint8_t fw_signature_len;
	uint8_t watermark_secure_end;
};

struct CB_COMPILER_ATTR_PACK CbFirmwareUpdateCancel {
	uint8_t dummy;
};

struct CB_COMPILER_ATTR_PACK CbFirmwarePing {
	uint8_t dummy;
};

struct CB_COMPILER_ATTR_PACK CbFirmwareGetVersion {
	uint8_t dummy;
};

#include "test/cb_management_test.h"
