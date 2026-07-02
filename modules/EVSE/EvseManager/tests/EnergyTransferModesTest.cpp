// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include <generated/types/evse_board_support.hpp>
#include <generated/types/iso15118.hpp>

#include "../energy_transfer_modes.hpp"

namespace module {
namespace {

using EnergyTransferMode = types::iso15118::EnergyTransferMode;
using HardwareCapabilities = types::evse_board_support::HardwareCapabilities;

bool contains(const std::vector<EnergyTransferMode>& modes, EnergyTransferMode mode) {
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

// Caps that are BPT-capable (export current > 0 and at least one export phase).
HardwareCapabilities make_caps(int min_phase_import = 1, int max_phase_import = 3, float max_current_export = 32.0f,
                               int max_phase_export = 3) {
    HardwareCapabilities caps;
    caps.max_current_A_import = 32.0f;
    caps.min_current_A_import = 6.0f;
    caps.max_phase_count_import = max_phase_import;
    caps.min_phase_count_import = min_phase_import;
    caps.max_current_A_export = max_current_export;
    caps.min_current_A_export = 0.0f;
    caps.max_phase_count_export = max_phase_export;
    caps.min_phase_count_export = 0;
    caps.supports_changing_phases_during_charging = false;
    caps.supports_cp_state_E = false;
    caps.connector_type = types::evse_board_support::Connector_type::IEC62196Type2Cable;
    return caps;
}

// (a) DER not available: no DER modes, but BPT and phase modes still hold.
TEST(EnergyTransferModesTest, DerUnavailableYieldsNoDerModes) {
    const auto caps = make_caps();
    const auto result = get_supported_ac_energy_transfers(caps, /*bpt*/ true, /*der_available*/ false);
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_BPT_DER));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_BPT));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_single_phase_core));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_three_phase_core));
}

// (b) DER available + BPT-capable: AC_BPT is upgraded to AC_BPT_DER.
TEST(EnergyTransferModesTest, DerAvailableAndBptCapableYieldsBptDer) {
    const auto caps = make_caps();
    const auto result = get_supported_ac_energy_transfers(caps, /*bpt*/ true, /*der_available*/ true);
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_BPT_DER));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_BPT));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER));
}

// (c) DER available but BPT disabled: plain AC_DER, no AC_BPT.
TEST(EnergyTransferModesTest, DerAvailableAndNotBptCapableYieldsAcDer) {
    const auto caps = make_caps();
    const auto result = get_supported_ac_energy_transfers(caps, /*bpt*/ false, /*der_available*/ true);
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_DER));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_BPT));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_BPT_DER));
}

// (c') Lack of BPT capability from zero export current still yields plain AC_DER.
TEST(EnergyTransferModesTest, DerAvailableAndZeroExportCurrentYieldsAcDer) {
    const auto caps = make_caps(/*min_phase_import*/ 1, /*max_phase_import*/ 3, /*max_current_export*/ 0.0f);
    const auto result = get_supported_ac_energy_transfers(caps, /*bpt*/ true, /*der_available*/ true);
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_DER));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_BPT));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_BPT_DER));
}

// (c'') Zero export phase count is not BPT-capable, for both DER states.
TEST(EnergyTransferModesTest, ZeroExportPhaseCountIsNotBptCapable) {
    const auto caps = make_caps(/*min_phase_import*/ 1, /*max_phase_import*/ 3, /*max_current_export*/ 16.0f,
                                /*max_phase_export*/ 0);
    const auto no_der = get_supported_ac_energy_transfers(caps, /*bpt*/ true, /*der_available*/ false);
    EXPECT_FALSE(contains(no_der, EnergyTransferMode::AC_BPT));
    EXPECT_FALSE(contains(no_der, EnergyTransferMode::AC_BPT_DER));
    EXPECT_FALSE(contains(no_der, EnergyTransferMode::AC_DER));
    EXPECT_TRUE(contains(no_der, EnergyTransferMode::AC_single_phase_core));

    const auto with_der = get_supported_ac_energy_transfers(caps, /*bpt*/ true, /*der_available*/ true);
    EXPECT_TRUE(contains(with_der, EnergyTransferMode::AC_DER));
    EXPECT_FALSE(contains(with_der, EnergyTransferMode::AC_BPT));
    EXPECT_FALSE(contains(with_der, EnergyTransferMode::AC_BPT_DER));
}

// (d) Phase modes follow min/max phase count import (existing behavior preserved).
TEST(EnergyTransferModesTest, PhaseModesSpanMinToMaxPhaseCount) {
    const auto caps = make_caps(/*min_phase_import*/ 1, /*max_phase_import*/ 3);
    const auto result = get_supported_ac_energy_transfers(caps, /*bpt*/ false, /*der_available*/ false);
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_single_phase_core));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_two_phase));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_three_phase_core));
}

TEST(EnergyTransferModesTest, PhaseModesThreePhaseOnly) {
    const auto caps = make_caps(/*min_phase_import*/ 3, /*max_phase_import*/ 3);
    const auto result = get_supported_ac_energy_transfers(caps, /*bpt*/ false, /*der_available*/ false);
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_single_phase_core));
    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_two_phase));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_three_phase_core));
}

} // namespace
} // namespace module
