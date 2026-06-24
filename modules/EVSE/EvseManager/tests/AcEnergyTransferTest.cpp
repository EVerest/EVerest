// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <EvseManager.hpp>
#include <generated/types/evse_board_support.hpp>
#include <generated/types/iso15118.hpp>

#include "ac_energy_transfer.hpp"

using namespace module;
using EnergyTransferMode = types::iso15118::EnergyTransferMode;

namespace {

Conf make_conf(bool supported_iso_ac_bpt) {
    Conf config{};
    config.supported_iso_ac_bpt = supported_iso_ac_bpt;
    return config;
}

types::evse_board_support::HardwareCapabilities make_caps(int min_phase_count_import, int max_phase_count_import,
                                                          float max_current_A_export, int max_phase_count_export) {
    types::evse_board_support::HardwareCapabilities caps{};
    caps.min_phase_count_import = min_phase_count_import;
    caps.max_phase_count_import = max_phase_count_import;
    caps.max_current_A_export = max_current_A_export;
    caps.max_phase_count_export = max_phase_count_export;
    return caps;
}

bool contains(const std::vector<EnergyTransferMode>& modes, EnergyTransferMode mode) {
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

} // namespace

TEST(AcEnergyTransferTest, single_phase_only) {
    const auto config = make_conf(false);
    const auto caps = make_caps(1, 1, 0, 0);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps);

    EXPECT_EQ(result, std::vector<EnergyTransferMode>{EnergyTransferMode::AC_single_phase_core});
}

TEST(AcEnergyTransferTest, three_phase_range) {
    const auto config = make_conf(false);
    const auto caps = make_caps(1, 3, 0, 0);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps);

    const std::vector<EnergyTransferMode> expected{
        EnergyTransferMode::AC_single_phase_core,
        EnergyTransferMode::AC_two_phase,
        EnergyTransferMode::AC_three_phase_core,
    };
    EXPECT_EQ(result, expected);
}

TEST(AcEnergyTransferTest, phase_inversion_clamps_to_three_phase) {
    const auto config = make_conf(false);
    // min_phases = clamp(3, 1, 3) = 3; max_phases = clamp(1, 3, 3) = 3 -> only count == 3 qualifies.
    const auto caps = make_caps(3, 1, 0, 0);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps);

    EXPECT_EQ(result, std::vector<EnergyTransferMode>{EnergyTransferMode::AC_three_phase_core});
}

TEST(AcEnergyTransferTest, lower_bound_clamp_to_single_phase) {
    const auto config = make_conf(false);
    // both clamp to the [1,3] floor of 1 -> only count == 1 qualifies.
    const auto caps = make_caps(0, 0, 0, 0);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps);

    EXPECT_EQ(result, std::vector<EnergyTransferMode>{EnergyTransferMode::AC_single_phase_core});
}

TEST(AcEnergyTransferTest, ac_der_iec_added_when_export_caps_present) {
    const auto config = make_conf(false);
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps, /*offer_ac_der_iec=*/true);

    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(AcEnergyTransferTest, ac_der_iec_not_added_when_disabled) {
    const auto config = make_conf(false);
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps);

    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(AcEnergyTransferTest, ac_der_iec_not_added_when_no_export_current) {
    const auto config = make_conf(false);
    const auto caps = make_caps(1, 3, 0, 3);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps, /*offer_ac_der_iec=*/true);

    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(AcEnergyTransferTest, ac_der_iec_not_added_when_no_export_phases) {
    const auto config = make_conf(false);
    const auto caps = make_caps(1, 3, 16, 0);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps, /*offer_ac_der_iec=*/true);

    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(AcEnergyTransferTest, ac_bpt_and_ac_der_iec_coexist) {
    const auto config = make_conf(true);
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = detail::get_supported_ac_energy_transfers(config, caps, /*offer_ac_der_iec=*/true);

    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_BPT));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_DER_IEC));
}
