// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <sunspec_map.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using sunspec::AcType;
using sunspec::BASE_ADDRESS;
using sunspec::CommonInfo;
using sunspec::Measurements;
using sunspec::Ratings;
using sunspec::RegisterMap;

// Addresses from the SunSpec model definitions (model_1.json, model_701.json).
constexpr std::uint16_t MODEL_1 = BASE_ADDRESS + 2;
constexpr std::uint16_t MODEL_701 = MODEL_1 + 68;
constexpr std::uint16_t END_MARKER = MODEL_701 + 155;

constexpr std::uint16_t MN = MODEL_1 + 2;
constexpr std::uint16_t OPT = MODEL_1 + 34;
constexpr std::uint16_t SN = MODEL_1 + 50;
constexpr std::uint16_t DA = MODEL_1 + 66;

constexpr std::uint16_t AC_TYPE = MODEL_701 + 2;
constexpr std::uint16_t W = MODEL_701 + 10;
constexpr std::uint16_t VAR = MODEL_701 + 12;
constexpr std::uint16_t A = MODEL_701 + 14;
constexpr std::uint16_t LLV = MODEL_701 + 15;
constexpr std::uint16_t LNV = MODEL_701 + 16;
constexpr std::uint16_t HZ = MODEL_701 + 17;
constexpr std::uint16_t TOT_WH_INJ = MODEL_701 + 19;
constexpr std::uint16_t TOT_WH_ABS = MODEL_701 + 23;
constexpr std::uint16_t W_L1 = MODEL_701 + 41;
constexpr std::uint16_t A_L1 = MODEL_701 + 45;
constexpr std::uint16_t V_L1 = MODEL_701 + 47;
constexpr std::uint16_t TOT_WH_ABS_L1 = MODEL_701 + 52;
constexpr std::uint16_t W_L2 = MODEL_701 + 64;
constexpr std::uint16_t W_L3 = MODEL_701 + 87;
constexpr std::uint16_t TOT_WH_ABS_L3 = MODEL_701 + 98;
constexpr std::uint16_t A_SF = MODEL_701 + 113;
constexpr std::uint16_t V_SF = MODEL_701 + 114;
constexpr std::uint16_t HZ_SF = MODEL_701 + 115;
constexpr std::uint16_t W_SF = MODEL_701 + 116;
constexpr std::uint16_t PF_SF = MODEL_701 + 117;
constexpr std::uint16_t TOT_WH_SF = MODEL_701 + 120;

constexpr std::uint16_t UNIMPL_INT16 = 0x8000;
constexpr std::uint16_t UNIMPL_UINT16 = 0xFFFF;

CommonInfo common_info() {
    CommonInfo info;
    info.manufacturer = "Example Inc";
    info.model = "EVSE-1";
    info.options = "opt";
    info.version = "1.0.0";
    info.serial_number = "SN-0001";
    info.device_address = 7;
    return info;
}

Ratings ratings_22kw() {
    return Ratings{22000.0, 32.0, 480.0};
}

std::uint16_t reg(const RegisterMap& map, std::uint16_t address) {
    return map.read(address, 1).at(0);
}

std::int16_t sreg(const RegisterMap& map, std::uint16_t address) {
    return static_cast<std::int16_t>(reg(map, address));
}

std::uint32_t reg32(const RegisterMap& map, std::uint16_t address) {
    const auto r = map.read(address, 2);
    return (static_cast<std::uint32_t>(r.at(0)) << 16) | r.at(1);
}

std::uint64_t reg64(const RegisterMap& map, std::uint16_t address) {
    const auto r = map.read(address, 4);
    std::uint64_t value = 0;
    for (const auto word : r) {
        value = (value << 16) | word;
    }
    return value;
}

std::string read_string(const RegisterMap& map, std::uint16_t address, std::uint16_t length) {
    std::string out;
    for (const auto word : map.read(address, length)) {
        out.push_back(static_cast<char>(word >> 8));
        out.push_back(static_cast<char>(word & 0xFF));
    }
    return out;
}

TEST(SunSpecScaleFactor, ChoosesSmallestExponentThatFitsInt16) {
    EXPECT_EQ(sunspec::choose_scale_factor(3000.0), -1);
    EXPECT_EQ(sunspec::choose_scale_factor(32767.0), 0);
    EXPECT_EQ(sunspec::choose_scale_factor(22000.0), 0);
    EXPECT_EQ(sunspec::choose_scale_factor(350000.0), 2);
    EXPECT_EQ(sunspec::choose_scale_factor(32.0), -3);
    EXPECT_EQ(sunspec::choose_scale_factor(480.0), -1);
}

TEST(SunSpecScaleFactor, ClampsToSupportedRange) {
    EXPECT_EQ(sunspec::choose_scale_factor(0.0), -3);
    EXPECT_EQ(sunspec::choose_scale_factor(1.0e12), 4);
}

TEST(SunSpecRegisterMap, LayoutHasMarkerModelsAndEndMarker) {
    const RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());

    EXPECT_EQ(map.size(), 227);
    EXPECT_EQ(map.read(BASE_ADDRESS, 2), (std::vector<std::uint16_t>{0x5375, 0x6E53}));
    EXPECT_EQ(map.read(MODEL_1, 2), (std::vector<std::uint16_t>{1, 66}));
    EXPECT_EQ(map.read(MODEL_701, 2), (std::vector<std::uint16_t>{701, 153}));
    EXPECT_EQ(map.read(END_MARKER, 2), (std::vector<std::uint16_t>{0xFFFF, 0}));
}

TEST(SunSpecRegisterMap, CommonModelCarriesDeviceIdentity) {
    const RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());

    EXPECT_EQ(read_string(map, MN, 16), std::string("Example Inc") + std::string(21, '\0'));
    EXPECT_EQ(read_string(map, SN, 16), std::string("SN-0001") + std::string(25, '\0'));
    EXPECT_EQ(reg(map, DA), 7);
}

TEST(SunSpecRegisterMap, CommonModelTruncatesLongStrings) {
    auto info = common_info();
    info.options = std::string(40, 'x');
    const RegisterMap map(info, AcType::SinglePhase, ratings_22kw());

    EXPECT_EQ(read_string(map, OPT, 8), std::string(16, 'x'));
}

TEST(SunSpecRegisterMap, AcTypeAndScaleFactorsAreSetAtConstruction) {
    const RegisterMap map(common_info(), AcType::ThreePhase, ratings_22kw());

    EXPECT_EQ(reg(map, AC_TYPE), 2);
    EXPECT_EQ(sreg(map, W_SF), 0);
    // The total current shares A_SF with the phases, so it covers 3 x 32 A.
    EXPECT_EQ(sreg(map, A_SF), -2);
    EXPECT_EQ(sreg(map, V_SF), -1);
    EXPECT_EQ(sreg(map, HZ_SF), -2);
    EXPECT_EQ(sreg(map, TOT_WH_SF), 0);
    EXPECT_EQ(reg(map, PF_SF), UNIMPL_INT16);
}

TEST(SunSpecRegisterMap, SinglePhaseCurrentScaleFactorCoversOnePhase) {
    const RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());

    EXPECT_EQ(sreg(map, A_SF), -3);
}

TEST(SunSpecRegisterMap, MeasurementsAreUnimplementedBeforeFirstUpdate) {
    const RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());

    EXPECT_EQ(reg(map, W), UNIMPL_INT16);
    EXPECT_EQ(reg(map, A), UNIMPL_INT16);
    EXPECT_EQ(reg(map, LNV), UNIMPL_UINT16);
    EXPECT_EQ(reg32(map, HZ), 0xFFFFFFFFu);
    EXPECT_EQ(reg64(map, TOT_WH_ABS), 0xFFFFFFFFFFFFFFFFull);
}

TEST(SunSpecRegisterMap, ImportIsNegativePowerAndAbsorbedEnergy) {
    RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());
    Measurements m;
    m.power_W.total = 7200.0;
    m.power_W.l1 = 7200.0;
    m.energy_import_Wh.total = 123456789.0;
    m.energy_export_Wh.total = 0.0;

    map.update(m);

    EXPECT_EQ(sreg(map, W), -7200);
    EXPECT_EQ(sreg(map, W_L1), -7200);
    EXPECT_EQ(reg64(map, TOT_WH_ABS), 123456789u);
    EXPECT_EQ(reg64(map, TOT_WH_INJ), 0u);
}

TEST(SunSpecRegisterMap, ExportIsPositivePower) {
    RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());
    Measurements m;
    m.power_W.total = -3000.0;

    map.update(m);

    EXPECT_EQ(sreg(map, W), 3000);
}

TEST(SunSpecRegisterMap, PowerIsScaledByScaleFactor) {
    RegisterMap map(common_info(), AcType::ThreePhase, Ratings{350000.0, 500.0, 1000.0});
    Measurements m;
    m.power_W.total = 150049.0;

    map.update(m);

    EXPECT_EQ(sreg(map, W_SF), 2);
    EXPECT_EQ(sreg(map, W), -1500);
}

TEST(SunSpecRegisterMap, ValuesOutsideRangeSaturateWithoutHittingSentinel) {
    RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());
    Measurements m;
    m.power_W.total = 100000.0;
    m.energy_import_Wh.total = -5.0;

    map.update(m);

    EXPECT_EQ(sreg(map, W), -32767);
    EXPECT_EQ(reg64(map, TOT_WH_ABS), 0u);
}

TEST(SunSpecRegisterMap, CurrentTotalIsSumOfPhasesAndVoltageIsAverage) {
    RegisterMap map(common_info(), AcType::ThreePhase, ratings_22kw());
    Measurements m;
    m.current_A = {10.0, 11.0, 12.0};
    m.voltage_V = {230.0, 231.0, 232.0};
    m.frequency_Hz = 50.02;

    map.update(m);

    EXPECT_EQ(sreg(map, A), 3300);
    EXPECT_EQ(sreg(map, A_L1), 1000);
    EXPECT_EQ(reg(map, LNV), 2310);
    EXPECT_EQ(reg(map, V_L1), 2300);
    EXPECT_EQ(reg32(map, HZ), 5002u);
}

TEST(SunSpecRegisterMap, PointsWithoutSourceStayUnimplemented) {
    RegisterMap map(common_info(), AcType::ThreePhase, ratings_22kw());
    Measurements m;
    m.power_W.total = 1000.0;
    m.voltage_V = {230.0, 230.0, 230.0};

    map.update(m);

    EXPECT_EQ(reg(map, VAR), UNIMPL_INT16);
    EXPECT_EQ(reg(map, LLV), UNIMPL_UINT16);
}

TEST(SunSpecRegisterMap, SinglePhaseLeavesOtherPhasesUnimplemented) {
    RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());
    Measurements m;
    m.power_W = {1000.0, 1000.0, 500.0, 500.0};
    m.energy_import_Wh = {10.0, 10.0, 5.0, 5.0};

    map.update(m);

    EXPECT_EQ(sreg(map, W_L1), -1000);
    EXPECT_EQ(reg(map, W_L2), UNIMPL_INT16);
    EXPECT_EQ(reg(map, W_L3), UNIMPL_INT16);
    EXPECT_EQ(reg64(map, TOT_WH_ABS_L3), 0xFFFFFFFFFFFFFFFFull);
}

TEST(SunSpecRegisterMap, ThreePhaseWritesAllPhases) {
    RegisterMap map(common_info(), AcType::ThreePhase, ratings_22kw());
    Measurements m;
    m.power_W = {3000.0, 1000.0, 1000.0, 1000.0};
    m.energy_import_Wh = {30.0, 10.0, 10.0, 10.0};

    map.update(m);

    EXPECT_EQ(sreg(map, W_L2), -1000);
    EXPECT_EQ(sreg(map, W_L3), -1000);
    EXPECT_EQ(reg64(map, TOT_WH_ABS_L1), 10u);
    EXPECT_EQ(reg64(map, TOT_WH_ABS_L3), 10u);
}

TEST(SunSpecRegisterMap, MissingValueResetsPointToUnimplemented) {
    RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());
    Measurements with_power;
    with_power.power_W.total = 1000.0;
    map.update(with_power);

    map.update(Measurements{});

    EXPECT_EQ(reg(map, W), UNIMPL_INT16);
}

TEST(SunSpecRegisterMap, ReadReturnsRequestedSlice) {
    const RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());

    const auto words = map.read(MODEL_701, 3);

    EXPECT_EQ(words, (std::vector<std::uint16_t>{701, 153, 0}));
}

TEST(SunSpecRegisterMap, ReadOutsideMapThrows) {
    const RegisterMap map(common_info(), AcType::SinglePhase, ratings_22kw());

    EXPECT_THROW(map.read(BASE_ADDRESS - 1, 1), std::out_of_range);
    EXPECT_THROW(map.read(END_MARKER + 1, 2), std::out_of_range);
    EXPECT_THROW(map.read(0, 0), std::out_of_range);
}

} // namespace
