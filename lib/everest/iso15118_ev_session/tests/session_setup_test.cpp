// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <everest/iso15118_ev_session/session_setup.hpp>

namespace {

namespace session = everest::lib::iso15118_ev;
using iso15118::ProtocolId;
using types::iso15118::EnergyTransferMode;
namespace dt = iso15118::message_20::datatypes;

TEST(SupportedProtocols, KeepTheConfiguredPriorityOrder) {
    std::string error;
    const auto protocols = session::parse_supported_protocols(" DIN70121 ,ISO15118_20", error);
    ASSERT_TRUE(protocols.has_value()) << error;
    EXPECT_EQ(*protocols, (std::vector<ProtocolId>{ProtocolId::DIN70121, ProtocolId::ISO15118_20}));
}

TEST(SupportedProtocols, RejectAnEmptyUnknownOrRepeatedList) {
    for (const auto* list : {"", " ", "ISO15118_20,", "ISO15118_3", "ISO15118_2,ISO15118_2"}) {
        std::string error;
        EXPECT_FALSE(session::parse_supported_protocols(list, error).has_value()) << list;
        EXPECT_FALSE(error.empty()) << list;
    }
}

TEST(DerIecFunctions, SetTheNamedFlagsOnly) {
    std::string error;
    const auto functions = session::parse_der_iec_functions("WattCosPhiMode, VoltVarMode", error);
    ASSERT_TRUE(functions.has_value()) << error;
    EXPECT_TRUE(functions->watt_cos_phi_mode);
    EXPECT_TRUE(functions->volt_var_mode);
    EXPECT_EQ(functions->to_bitset().count(), 2U);
}

TEST(DerIecFunctions, AnEmptyListDeclaresNone) {
    std::string error;
    const auto functions = session::parse_der_iec_functions("", error);
    ASSERT_TRUE(functions.has_value()) << error;
    EXPECT_TRUE(functions->to_bitset().none());
}

TEST(DerIecFunctions, RejectAnUnknownOrRepeatedName) {
    for (const auto* list : {"WattCosPhi", "VoltVarMode,VoltVarMode"}) {
        std::string error;
        EXPECT_FALSE(session::parse_der_iec_functions(list, error).has_value()) << list;
        EXPECT_FALSE(error.empty()) << list;
    }
}

TEST(EnergyService, MapsEachRunnableMode) {
    EXPECT_EQ(session::energy_service(EnergyTransferMode::DC), dt::ServiceCategory::DC);
    EXPECT_EQ(session::energy_service(EnergyTransferMode::AC_single_phase_core), dt::ServiceCategory::AC);
    EXPECT_EQ(session::energy_service(EnergyTransferMode::AC_BPT), dt::ServiceCategory::AC_BPT);
    EXPECT_EQ(session::energy_service(EnergyTransferMode::DC_BPT), dt::ServiceCategory::DC_BPT);
    EXPECT_EQ(session::energy_service(EnergyTransferMode::MCS_BPT), dt::ServiceCategory::MCS_BPT);
    EXPECT_EQ(session::energy_service(EnergyTransferMode::AC_DER_SAE), dt::ServiceCategory::AC_DER_SAE);
    EXPECT_FALSE(session::energy_service(EnergyTransferMode::WPT).has_value());
}

TEST(SessionRequest, CarriesTheStartInTheSessionsTerms) {
    const auto request = session::make_session_request(EnergyTransferMode::AC_BPT, {ProtocolId::ISO15118_20}, true,
                                                       false, 0, 25500.5, "test");
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->energy_service, dt::ServiceCategory::AC_BPT);
    EXPECT_EQ(request->iso2_transfer_mode, iso15118::shared_datatypes::EnergyTransferMode::AC_three_phase_core);
    EXPECT_TRUE(request->eim_requested);
    EXPECT_FALSE(request->enforce_contract);
    // Wh in, Wh on the wire.
    EXPECT_EQ(request->energy_request.energy_wh, 25500.5F);
    EXPECT_FALSE(request->energy_request.departure.has_value());
}

TEST(SessionRequest, NoneForAModeNoSessionRuns) {
    EXPECT_FALSE(
        session::make_session_request(EnergyTransferMode::WPT, {ProtocolId::ISO15118_2}, false, false, 0, 0, "test")
            .has_value());
}

TEST(SessionFiles, AResumePresentsTheContractItsPausedSessionInstalled) {
    session::VehicleConfig config;
    iso15118::ev::d2::PnCConfig material;
    material.contract = iso15118::ev::d2::ContractMaterial{};
    material.contract->emaid = "configured";
    iso15118::ev::d2::ContractMaterial installed;
    installed.emaid = "installed";
    const auto files = session::make_session_files(config, material, installed, "test");
    ASSERT_TRUE(files.pnc.contract.has_value());
    EXPECT_EQ(files.pnc.contract->emaid, "installed");
}

TEST(SessionFiles, AFreshSessionPresentsTheConfiguredContract) {
    session::VehicleConfig config;
    iso15118::ev::d2::PnCConfig material;
    material.contract = iso15118::ev::d2::ContractMaterial{};
    material.contract->emaid = "configured";
    const auto files = session::make_session_files(config, material, std::nullopt, "test");
    ASSERT_TRUE(files.pnc.contract.has_value());
    EXPECT_EQ(files.pnc.contract->emaid, "configured");
}

TEST(SeedSessionParams, SinglePhaseAsksForAThirdOfTheConfiguredPower) {
    session::VehicleConfig config;
    config.ac_phase_count = 3;
    config.ac_max_charge_power_w = 11000;
    config.ac_min_charge_power_w = 300;
    iso15118::ev::params::AcChargeParams ac;
    iso15118::ev::params::DcChargeParams dc;
    session::seed_session_params(config, EnergyTransferMode::AC_single_phase_core, dt::ServiceCategory::AC,
                                 std::nullopt, std::nullopt, ac, dc, "test");
    EXPECT_EQ(ac.phase_count, 1);
    EXPECT_FLOAT_EQ(ac.max_charge_power, 11000.0f / 3.0f);
    EXPECT_FLOAT_EQ(ac.min_charge_power, 100.0f);
}

TEST(SeedSessionParams, CommandedDischargeLimitsWinOverTheConfiguredOnes) {
    session::VehicleConfig config;
    config.dc_max_discharge_power_w = 10000;
    config.dc_min_discharge_power_w = 100;
    config.dc_max_discharge_current_a = 20;
    iso15118::ev::params::AcChargeParams ac;
    iso15118::ev::params::DcChargeParams dc;
    session::seed_session_params(config, EnergyTransferMode::DC_BPT, dt::ServiceCategory::DC_BPT, 5000.0f, std::nullopt,
                                 ac, dc, "test");
    EXPECT_FLOAT_EQ(dc.max_discharge_power, 5000.0f);
    EXPECT_FLOAT_EQ(dc.min_discharge_power, 100.0f);
    EXPECT_FLOAT_EQ(dc.max_discharge_current, 20.0f);
}

TEST(ApplyPresentValues, StoresTheCarriedFieldsAndKeepsTheRest) {
    iso15118::ev::params::AcChargeParams ac;
    iso15118::ev::params::DcChargeParams dc;
    ac.present_active_power = 7.0f;
    types::iso15118::EvPresentValues values;
    values.present_voltage = 230.5f;
    values.present_frequency = 49.9f;
    values.der_alarm_status = 5;
    session::apply_present_values(values, ac, dc, nullptr, "test");
    EXPECT_FLOAT_EQ(dc.present_voltage, 230.5f);
    ASSERT_TRUE(ac.present_voltage.has_value());
    EXPECT_FLOAT_EQ(*ac.present_voltage, 230.5f);
    ASSERT_TRUE(ac.present_frequency.has_value());
    EXPECT_FLOAT_EQ(*ac.present_frequency, 49.9f);
    EXPECT_FLOAT_EQ(ac.present_active_power, 7.0f);
    EXPECT_EQ(ac.der_alarm_status, 5U);
}

TEST(ApplyPresentValues, IgnoresANegativeAlarmStatus) {
    iso15118::ev::params::AcChargeParams ac;
    iso15118::ev::params::DcChargeParams dc;
    ac.der_alarm_status = 3;
    types::iso15118::EvPresentValues values;
    values.der_alarm_status = -1;
    session::apply_present_values(values, ac, dc, nullptr, "test");
    EXPECT_EQ(ac.der_alarm_status, 3U);
}

} // namespace
