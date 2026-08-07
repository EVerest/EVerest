// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#ifndef EASTRON_SDM630EV_REGISTERS_HPP
#define EASTRON_SDM630EV_REGISTERS_HPP

#include <cstddef>
#include <cstdint>

// Raw Modbus protocol addresses are used throughout. The Eastron doc mixes
// 5-digit (46001 -> 0x1770) and 6-digit (461439 -> 0xEFFE) register
// numbering, so a single base-address subtraction cannot work.
namespace sdm630ev::registers {

// The device rejects transactions above 40 parameters / 80 registers.
constexpr std::uint16_t MAX_REGISTERS_PER_READ = 80;

// --- Input registers (FC04), IEEE754 float32, MS register first ---

// 30001..30072: live measurements read as one block.
constexpr std::int32_t LIVE_BLOCK_ADDRESS = 0x0000;
constexpr std::uint16_t LIVE_BLOCK_REGISTER_COUNT = 72;

// Byte offsets into the live block: (register_number - 30001) * 2.
namespace live_offsets {
constexpr std::size_t V_L1 = 0;        // 30001
constexpr std::size_t V_L2 = 4;        // 30003
constexpr std::size_t V_L3 = 8;        // 30005
constexpr std::size_t A_L1 = 12;       // 30007
constexpr std::size_t A_L2 = 16;       // 30009
constexpr std::size_t A_L3 = 20;       // 30011
constexpr std::size_t W_L1 = 24;       // 30013
constexpr std::size_t W_L2 = 28;       // 30015
constexpr std::size_t W_L3 = 32;       // 30017
constexpr std::size_t VAR_L1 = 48;     // 30025
constexpr std::size_t VAR_L2 = 52;     // 30027
constexpr std::size_t VAR_L3 = 56;     // 30029
constexpr std::size_t W_TOTAL = 104;   // 30053
constexpr std::size_t VAR_TOTAL = 120; // 30061
constexpr std::size_t FREQUENCY = 140; // 30071
} // namespace live_offsets

// 30073..30080: energy totals (kWh / kVArh).
constexpr std::int32_t ENERGY_BLOCK_ADDRESS = 0x0048;
constexpr std::uint16_t ENERGY_BLOCK_REGISTER_COUNT = 8;

namespace energy_offsets {
constexpr std::size_t IMPORT_KWH = 0; // 30073
constexpr std::size_t EXPORT_KWH = 4; // 30075
} // namespace energy_offsets

// 30347..30358: per-phase import/export energy (kWh).
constexpr std::int32_t PHASE_ENERGY_BLOCK_ADDRESS = 0x015A;
constexpr std::uint16_t PHASE_ENERGY_BLOCK_REGISTER_COUNT = 12;

namespace phase_energy_offsets {
constexpr std::size_t IMPORT_L1 = 0;  // 30347
constexpr std::size_t IMPORT_L2 = 4;  // 30349
constexpr std::size_t IMPORT_L3 = 8;  // 30351
constexpr std::size_t EXPORT_L1 = 12; // 30353
constexpr std::size_t EXPORT_L2 = 16; // 30355
constexpr std::size_t EXPORT_L3 = 20; // 30357
} // namespace phase_energy_offsets

// --- Holding registers (FC03 read / FC16 write) ---

constexpr std::int32_t CHARGING_DURATION_ADDRESS = 0x1770; // 46001, UINT32 s
constexpr std::int32_t CHARGING_ENERGY_ADDRESS = 0x1772;   // 46003, UINT32 Wh
constexpr std::int32_t START_TIMESTAMP_ADDRESS = 0x1774;   // 46005, UINT32 unix
constexpr std::int32_t STOP_TIMESTAMP_ADDRESS = 0x1776;    // 46007, UINT32 unix

constexpr std::int32_t CHARGING_STATUS_ADDRESS = 0x17A1; // 46050, ro
constexpr std::uint16_t CHARGING_STATUS_IDLE = 0;
constexpr std::uint16_t CHARGING_STATUS_IN_PROGRESS = 1;
constexpr std::uint16_t CHARGING_STATUS_POWER_LOSS = 2;
constexpr std::uint16_t CHARGING_STATUS_RESET = 3;

constexpr std::int32_t CHARGE_CONTROL_ADDRESS = 0x17A2; // 46051, r/w
constexpr std::uint16_t CHARGE_CONTROL_BEGIN = 0x0001;  // 'B'
constexpr std::uint16_t CHARGE_CONTROL_END = 0x0002;    // 'E'

constexpr std::int32_t SIGNATURE_STATUS_ADDRESS = 0x17AC; // 46061, ro
constexpr std::uint16_t SIGNATURE_STATUS_NOT_INITIALISED = 0x00;
constexpr std::uint16_t SIGNATURE_STATUS_IDLE = 0x01;
constexpr std::uint16_t SIGNATURE_STATUS_IN_PROGRESS = 0x02;
constexpr std::uint16_t SIGNATURE_STATUS_OK = 0x03;

constexpr std::int32_t SIGNATURE_FORMAT_ADDRESS = 0x17B0; // 46065, r/w
constexpr std::uint16_t SIGNATURE_FORMAT_HEX_ASN1 = 0;

constexpr std::int32_t SIGNATURE_ALGORITHM_ADDRESS = 0x17B1; // 46066, r/w
constexpr std::uint16_t SIGNATURE_ALGORITHM_NONE = 0;
constexpr std::uint16_t SIGNATURE_ALGORITHM_ECDSA_SECP256R1 = 1;

// OCMF identification dataset. Each field must be written with its own
// FC16 request: the device accepts only one parameter per write message.
constexpr std::int32_t OCMF_IS_ADDRESS = 0x1800; // 46145: 0=true, 1=false
constexpr std::int32_t OCMF_IF_ADDRESS = 0x1802; // 46147, UINT32 bitfield
constexpr std::int32_t OCMF_IT_ADDRESS = 0x1804; // 46149
constexpr std::int32_t OCMF_ID_ADDRESS = 0x1805; // 46150, CHAR[40]
constexpr std::int32_t OCMF_CT_ADDRESS = 0x1819; // 46170: 1=EVSEID, 2=CBIDC
constexpr std::int32_t OCMF_CI_ADDRESS = 0x181A; // 46171, CHAR[40]
constexpr std::uint16_t OCMF_TEXT_FIELD_WORD_COUNT = 20;

constexpr std::int32_t SIGNATURE_LENGTH_ADDRESS = 0x1D00; // 47425, bytes
constexpr std::int32_t SIGNATURE_DATA_ADDRESS = 0x1D01;   // 47426
constexpr std::int32_t OCMF_JSON_LENGTH_ADDRESS = 0x2100; // 48449, bytes
constexpr std::int32_t OCMF_JSON_DATA_ADDRESS = 0x2101;   // 48450

constexpr std::int32_t PUBLIC_KEY_ADDRESS = 0x2300; // 48961, 64 bytes raw
constexpr std::uint16_t PUBLIC_KEY_WORD_COUNT = 32;

constexpr std::int32_t TIMEZONE_ADDRESS = 0xEFFE; // 461439, float hours
constexpr std::int32_t TIME_BCD_ADDRESS = 0xF000; // 461441, 8 byte BCD
constexpr std::uint16_t TIME_BCD_WORD_COUNT = 4;

} // namespace sdm630ev::registers

#endif // EASTRON_SDM630EV_REGISTERS_HPP
