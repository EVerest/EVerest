// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <sunspec_map.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sunspec {

namespace {

// Offsets from the SunSpec model definitions, relative to the start of the
// register map at BASE_ADDRESS.
constexpr std::uint16_t MODEL_1 = 2;
constexpr std::uint16_t MODEL_1_LENGTH = 66;
constexpr std::uint16_t MODEL_701 = MODEL_1 + 2 + MODEL_1_LENGTH;
constexpr std::uint16_t MODEL_701_LENGTH = 153;
constexpr std::uint16_t END_MARKER = MODEL_701 + 2 + MODEL_701_LENGTH;
constexpr std::uint16_t MAP_SIZE = END_MARKER + 2;

namespace m1 {
constexpr std::uint16_t MN = MODEL_1 + 2;
constexpr std::uint16_t MD = MODEL_1 + 18;
constexpr std::uint16_t OPT = MODEL_1 + 34;
constexpr std::uint16_t VR = MODEL_1 + 42;
constexpr std::uint16_t SN = MODEL_1 + 50;
constexpr std::uint16_t DA = MODEL_1 + 66;
constexpr std::uint16_t PAD = MODEL_1 + 67;
} // namespace m1

namespace m701 {
constexpr std::uint16_t AC_TYPE = MODEL_701 + 2;
constexpr std::uint16_t ST = MODEL_701 + 3;
constexpr std::uint16_t INV_ST = MODEL_701 + 4;
constexpr std::uint16_t CONN_ST = MODEL_701 + 5;
constexpr std::uint16_t ALRM = MODEL_701 + 6;
constexpr std::uint16_t DER_MODE = MODEL_701 + 8;
constexpr std::uint16_t W = MODEL_701 + 10;
constexpr std::uint16_t VA = MODEL_701 + 11;
constexpr std::uint16_t VAR = MODEL_701 + 12;
constexpr std::uint16_t PF = MODEL_701 + 13;
constexpr std::uint16_t A = MODEL_701 + 14;
constexpr std::uint16_t LLV = MODEL_701 + 15;
constexpr std::uint16_t LNV = MODEL_701 + 16;
constexpr std::uint16_t HZ = MODEL_701 + 17;
constexpr std::uint16_t TOT_WH_INJ = MODEL_701 + 19;
constexpr std::uint16_t TOT_WH_ABS = MODEL_701 + 23;
constexpr std::uint16_t TOT_VARH_INJ = MODEL_701 + 27;
constexpr std::uint16_t TOT_VARH_ABS = MODEL_701 + 31;
constexpr std::uint16_t TMP_FIRST = MODEL_701 + 35;
constexpr std::uint16_t TMP_COUNT = 6;

// Each phase block holds W, VA, Var, PF, A, line-line V, line-neutral V,
// then four 64-bit energy totals: 23 registers.
constexpr std::uint16_t PHASE_FIRST = MODEL_701 + 41;
constexpr std::uint16_t PHASE_STRIDE = 23;
constexpr std::uint16_t PHASE_W = 0;
constexpr std::uint16_t PHASE_VA = 1;
constexpr std::uint16_t PHASE_VAR = 2;
constexpr std::uint16_t PHASE_PF = 3;
constexpr std::uint16_t PHASE_A = 4;
constexpr std::uint16_t PHASE_LLV = 5;
constexpr std::uint16_t PHASE_LNV = 6;
constexpr std::uint16_t PHASE_TOT_WH_INJ = 7;
constexpr std::uint16_t PHASE_TOT_WH_ABS = 11;
constexpr std::uint16_t PHASE_TOT_VARH_INJ = 15;
constexpr std::uint16_t PHASE_TOT_VARH_ABS = 19;

constexpr std::uint16_t THROT_PCT = MODEL_701 + 110;
constexpr std::uint16_t THROT_SRC = MODEL_701 + 111;
constexpr std::uint16_t A_SF = MODEL_701 + 113;
constexpr std::uint16_t V_SF = MODEL_701 + 114;
constexpr std::uint16_t HZ_SF = MODEL_701 + 115;
constexpr std::uint16_t W_SF = MODEL_701 + 116;
constexpr std::uint16_t PF_SF = MODEL_701 + 117;
constexpr std::uint16_t VA_SF = MODEL_701 + 118;
constexpr std::uint16_t VAR_SF = MODEL_701 + 119;
constexpr std::uint16_t TOT_WH_SF = MODEL_701 + 120;
constexpr std::uint16_t TOT_VARH_SF = MODEL_701 + 121;
constexpr std::uint16_t TMP_SF = MODEL_701 + 122;
constexpr std::uint16_t MN_ALRM_INFO = MODEL_701 + 123;
constexpr std::uint16_t MN_ALRM_INFO_LENGTH = 32;
} // namespace m701

constexpr std::uint16_t UNIMPL_INT16 = 0x8000;
constexpr std::uint16_t UNIMPL_UINT16 = 0xFFFF;
constexpr std::uint32_t UNIMPL_UINT32 = 0xFFFFFFFF;
constexpr std::uint64_t UNIMPL_UINT64 = 0xFFFFFFFFFFFFFFFF;

constexpr std::int16_t HZ_SCALE_FACTOR = -2;
constexpr std::int16_t ENERGY_SCALE_FACTOR = 0;
constexpr std::int16_t MIN_SCALE_FACTOR = -3;
constexpr std::int16_t MAX_SCALE_FACTOR = 4;

constexpr double INT16_MAX_COUNTS = 32767.0;

std::uint16_t phase_count(AcType ac_type) {
    switch (ac_type) {
    case AcType::SinglePhase:
        return 1;
    case AcType::SplitPhase:
        return 2;
    case AcType::ThreePhase:
        return 3;
    }
    return 1;
}

std::uint16_t phase_address(std::uint16_t phase_index, std::uint16_t field) {
    return m701::PHASE_FIRST + phase_index * m701::PHASE_STRIDE + field;
}

double counts(double value, std::int16_t scale_factor) {
    return std::round(value / std::pow(10.0, scale_factor));
}

// The lowest value of each signed type and the highest of each unsigned type
// mean "not implemented" in SunSpec, so in-range values stop one short.
std::uint16_t to_int16(double value, std::int16_t scale_factor) {
    const auto clamped = std::clamp(counts(value, scale_factor), -INT16_MAX_COUNTS, INT16_MAX_COUNTS);
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped));
}

std::uint16_t to_uint16(double value, std::int16_t scale_factor) {
    return static_cast<std::uint16_t>(std::clamp(counts(value, scale_factor), 0.0, 65534.0));
}

std::uint32_t to_uint32(double value, std::int16_t scale_factor) {
    return static_cast<std::uint32_t>(std::clamp(counts(value, scale_factor), 0.0, 4294967294.0));
}

std::uint64_t to_uint64(double value, std::int16_t scale_factor) {
    const auto c = counts(value, scale_factor);
    if (c <= 0.0) {
        return 0;
    }
    // 2^64 - 2048 is the largest double below the sentinel 2^64 - 1.
    constexpr double max_counts = 18446744073709549568.0;
    return static_cast<std::uint64_t>(std::min(c, max_counts));
}

std::optional<double> sum(const PerPhase& values) {
    std::optional<double> total;
    for (const auto& v : {values.l1, values.l2, values.l3}) {
        if (v.has_value()) {
            total = total.value_or(0.0) + *v;
        }
    }
    return total;
}

std::optional<double> average(const PerPhase& values) {
    double total = 0.0;
    int n = 0;
    for (const auto& v : {values.l1, values.l2, values.l3}) {
        if (v.has_value()) {
            total += *v;
            ++n;
        }
    }
    if (n == 0) {
        return std::nullopt;
    }
    return total / n;
}

std::optional<double> negated(const std::optional<double>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return -*value;
}

std::optional<double> phase_value(const TotalAndPerPhase& values, std::uint16_t phase_index) {
    switch (phase_index) {
    case 0:
        return values.l1;
    case 1:
        return values.l2;
    default:
        return values.l3;
    }
}

std::optional<double> phase_value(const PerPhase& values, std::uint16_t phase_index) {
    switch (phase_index) {
    case 0:
        return values.l1;
    case 1:
        return values.l2;
    default:
        return values.l3;
    }
}

class Writer {
public:
    explicit Writer(std::vector<std::uint16_t>& registers) : registers(registers) {
    }

    void u16(std::uint16_t offset, std::uint16_t value) {
        registers.at(offset) = value;
    }

    void u32(std::uint16_t offset, std::uint32_t value) {
        u16(offset, static_cast<std::uint16_t>(value >> 16));
        u16(offset + 1, static_cast<std::uint16_t>(value & 0xFFFF));
    }

    void u64(std::uint16_t offset, std::uint64_t value) {
        for (int i = 0; i < 4; ++i) {
            u16(offset + i, static_cast<std::uint16_t>(value >> (48 - 16 * i)));
        }
    }

    void string(std::uint16_t offset, std::uint16_t length, const std::string& value) {
        for (std::uint16_t i = 0; i < length; ++i) {
            const auto hi = 2u * i < value.size() ? static_cast<unsigned char>(value[2u * i]) : 0u;
            const auto lo = 2u * i + 1 < value.size() ? static_cast<unsigned char>(value[2u * i + 1]) : 0u;
            u16(offset + i, static_cast<std::uint16_t>((hi << 8) | lo));
        }
    }

    void int16(std::uint16_t offset, const std::optional<double>& value, std::int16_t scale_factor) {
        u16(offset, value.has_value() ? to_int16(*value, scale_factor) : UNIMPL_INT16);
    }

    void uint16(std::uint16_t offset, const std::optional<double>& value, std::int16_t scale_factor) {
        u16(offset, value.has_value() ? to_uint16(*value, scale_factor) : UNIMPL_UINT16);
    }

    void uint32(std::uint16_t offset, const std::optional<double>& value, std::int16_t scale_factor) {
        u32(offset, value.has_value() ? to_uint32(*value, scale_factor) : UNIMPL_UINT32);
    }

    void uint64(std::uint16_t offset, const std::optional<double>& value, std::int16_t scale_factor) {
        u64(offset, value.has_value() ? to_uint64(*value, scale_factor) : UNIMPL_UINT64);
    }

    void sunssf(std::uint16_t offset, std::int16_t scale_factor) {
        u16(offset, static_cast<std::uint16_t>(scale_factor));
    }

private:
    std::vector<std::uint16_t>& registers;
};

} // namespace

std::int16_t choose_scale_factor(double max_abs_value) {
    for (std::int16_t sf = MIN_SCALE_FACTOR; sf < MAX_SCALE_FACTOR; ++sf) {
        if (std::abs(max_abs_value) <= INT16_MAX_COUNTS * std::pow(10.0, sf)) {
            return sf;
        }
    }
    return MAX_SCALE_FACTOR;
}

RegisterMap::RegisterMap(const CommonInfo& info, AcType ac_type, const Ratings& ratings) :
    registers(MAP_SIZE, 0),
    w_sf(choose_scale_factor(ratings.max_power_W)),
    a_sf(choose_scale_factor(ratings.max_current_A * phase_count(ac_type))),
    v_sf(choose_scale_factor(ratings.max_voltage_V)) {
    Writer w(registers);

    w.u16(0, 0x5375);
    w.u16(1, 0x6E53);

    w.u16(MODEL_1, 1);
    w.u16(MODEL_1 + 1, MODEL_1_LENGTH);
    w.string(m1::MN, 16, info.manufacturer);
    w.string(m1::MD, 16, info.model);
    w.string(m1::OPT, 8, info.options);
    w.string(m1::VR, 8, info.version);
    w.string(m1::SN, 16, info.serial_number);
    w.u16(m1::DA, info.device_address);
    w.u16(m1::PAD, 0);

    w.u16(MODEL_701, 701);
    w.u16(MODEL_701 + 1, MODEL_701_LENGTH);
    w.u16(m701::AC_TYPE, static_cast<std::uint16_t>(ac_type));
    w.u16(m701::ST, UNIMPL_UINT16);
    w.u16(m701::INV_ST, UNIMPL_UINT16);
    w.u16(m701::CONN_ST, UNIMPL_UINT16);
    w.u32(m701::ALRM, UNIMPL_UINT32);
    w.u32(m701::DER_MODE, UNIMPL_UINT32);
    w.u16(m701::VA, UNIMPL_INT16);
    w.u16(m701::VAR, UNIMPL_INT16);
    w.u16(m701::PF, UNIMPL_INT16);
    w.u16(m701::LLV, UNIMPL_UINT16);
    w.u64(m701::TOT_VARH_INJ, UNIMPL_UINT64);
    w.u64(m701::TOT_VARH_ABS, UNIMPL_UINT64);
    for (std::uint16_t i = 0; i < m701::TMP_COUNT; ++i) {
        w.u16(m701::TMP_FIRST + i, UNIMPL_INT16);
    }
    for (std::uint16_t phase = 0; phase < 3; ++phase) {
        w.u16(phase_address(phase, m701::PHASE_VA), UNIMPL_INT16);
        w.u16(phase_address(phase, m701::PHASE_VAR), UNIMPL_INT16);
        w.u16(phase_address(phase, m701::PHASE_PF), UNIMPL_INT16);
        w.u16(phase_address(phase, m701::PHASE_LLV), UNIMPL_UINT16);
        w.u64(phase_address(phase, m701::PHASE_TOT_VARH_INJ), UNIMPL_UINT64);
        w.u64(phase_address(phase, m701::PHASE_TOT_VARH_ABS), UNIMPL_UINT64);
    }
    w.u16(m701::THROT_PCT, UNIMPL_UINT16);
    w.u32(m701::THROT_SRC, UNIMPL_UINT32);

    w.sunssf(m701::A_SF, a_sf);
    w.sunssf(m701::V_SF, v_sf);
    w.sunssf(m701::HZ_SF, HZ_SCALE_FACTOR);
    w.sunssf(m701::W_SF, w_sf);
    w.u16(m701::PF_SF, UNIMPL_INT16);
    w.u16(m701::VA_SF, UNIMPL_INT16);
    w.u16(m701::VAR_SF, UNIMPL_INT16);
    w.sunssf(m701::TOT_WH_SF, ENERGY_SCALE_FACTOR);
    w.u16(m701::TOT_VARH_SF, UNIMPL_INT16);
    w.u16(m701::TMP_SF, UNIMPL_INT16);
    w.string(m701::MN_ALRM_INFO, m701::MN_ALRM_INFO_LENGTH, "");

    w.u16(END_MARKER, 0xFFFF);
    w.u16(END_MARKER + 1, 0);

    update(Measurements{});
}

void RegisterMap::update(const Measurements& m) {
    Writer w(registers);
    const auto ac_type = static_cast<AcType>(registers.at(m701::AC_TYPE));

    w.int16(m701::W, negated(m.power_W.total), w_sf);
    w.int16(m701::A, sum(m.current_A), a_sf);
    w.uint16(m701::LNV, average(m.voltage_V), v_sf);
    w.uint32(m701::HZ, m.frequency_Hz, HZ_SCALE_FACTOR);
    w.uint64(m701::TOT_WH_INJ, m.energy_export_Wh.total, ENERGY_SCALE_FACTOR);
    w.uint64(m701::TOT_WH_ABS, m.energy_import_Wh.total, ENERGY_SCALE_FACTOR);

    const auto phases = phase_count(ac_type);
    for (std::uint16_t phase = 0; phase < 3; ++phase) {
        const bool present = phase < phases;
        const auto value = [present](const std::optional<double>& v) { return present ? v : std::nullopt; };
        w.int16(phase_address(phase, m701::PHASE_W), value(negated(phase_value(m.power_W, phase))), w_sf);
        w.int16(phase_address(phase, m701::PHASE_A), value(phase_value(m.current_A, phase)), a_sf);
        w.uint16(phase_address(phase, m701::PHASE_LNV), value(phase_value(m.voltage_V, phase)), v_sf);
        w.uint64(phase_address(phase, m701::PHASE_TOT_WH_INJ), value(phase_value(m.energy_export_Wh, phase)),
                 ENERGY_SCALE_FACTOR);
        w.uint64(phase_address(phase, m701::PHASE_TOT_WH_ABS), value(phase_value(m.energy_import_Wh, phase)),
                 ENERGY_SCALE_FACTOR);
    }
}

std::vector<std::uint16_t> RegisterMap::read(std::uint16_t address, std::uint16_t count) const {
    if (count == 0 || address < BASE_ADDRESS || address - BASE_ADDRESS + count > registers.size()) {
        throw std::out_of_range("SunSpec register read outside the map");
    }
    const auto first = registers.begin() + (address - BASE_ADDRESS);
    return {first, first + count};
}

std::uint16_t RegisterMap::size() const {
    return static_cast<std::uint16_t>(registers.size());
}

} // namespace sunspec
