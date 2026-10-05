// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <vector>

#include <generated/types/evse_board_support.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>

#include "energy_transfer_modes.hpp"

using EnergyTransferMode = types::iso15118::EnergyTransferMode;

namespace {

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

TEST(EnergyTransferModesTest, single_phase_only) {
    const auto caps = make_caps(1, 1, 0, 0);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/false);

    EXPECT_EQ(result, std::vector<EnergyTransferMode>{EnergyTransferMode::AC_single_phase_core});
}

TEST(EnergyTransferModesTest, three_phase_range) {
    const auto caps = make_caps(1, 3, 0, 0);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/false);

    const std::vector<EnergyTransferMode> expected{
        EnergyTransferMode::AC_single_phase_core,
        EnergyTransferMode::AC_two_phase,
        EnergyTransferMode::AC_three_phase_core,
    };
    EXPECT_EQ(result, expected);
}

TEST(EnergyTransferModesTest, phase_inversion_clamps_to_three_phase) {
    // min_phases = clamp(3, 1, 3) = 3; max_phases = clamp(1, 3, 3) = 3 -> only count == 3 qualifies.
    const auto caps = make_caps(3, 1, 0, 0);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/false);

    EXPECT_EQ(result, std::vector<EnergyTransferMode>{EnergyTransferMode::AC_three_phase_core});
}

TEST(EnergyTransferModesTest, lower_bound_clamp_to_single_phase) {
    // both clamp to the [1,3] floor of 1 -> only count == 1 qualifies.
    const auto caps = make_caps(0, 0, 0, 0);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/false);

    EXPECT_EQ(result, std::vector<EnergyTransferMode>{EnergyTransferMode::AC_single_phase_core});
}

TEST(EnergyTransferModesTest, ac_bpt_added_when_export_capable) {
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/true,
                                                                  /*der_available=*/false);

    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_BPT));
}

TEST(EnergyTransferModesTest, ac_der_iec_added_when_der_available_and_export_capable) {
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/true);

    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(EnergyTransferModesTest, ac_bpt_and_ac_der_iec_coexist) {
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/true,
                                                                  /*der_available=*/true);

    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_BPT));
    EXPECT_TRUE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(EnergyTransferModesTest, ac_der_iec_not_added_when_not_export_capable) {
    const auto caps = make_caps(1, 3, 0, 3);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/true);

    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(EnergyTransferModesTest, ac_der_iec_not_added_when_der_unavailable) {
    const auto caps = make_caps(1, 3, 16, 3);

    const auto result = module::get_supported_ac_energy_transfers(caps, /*supported_iso_ac_bpt=*/false,
                                                                  /*der_available=*/false);

    EXPECT_FALSE(contains(result, EnergyTransferMode::AC_DER_IEC));
}

TEST(EnergyTransferModesTest, filter_allowed_empty_input_is_empty) {
    const auto result = module::filter_allowed_energy_transfers({}, types::evse_manager::ConnectorTypeEnum::cMCS);

    EXPECT_TRUE(result.empty());
}

TEST(EnergyTransferModesTest, filter_allowed_without_connector_type_is_unchanged) {
    const std::vector<EnergyTransferMode> allowed{
        EnergyTransferMode::DC_BPT,
        EnergyTransferMode::AC_single_phase_core,
        EnergyTransferMode::DC,
    };

    const auto result = module::filter_allowed_energy_transfers(allowed, std::nullopt);

    EXPECT_EQ(result, allowed);
}

TEST(EnergyTransferModesTest, filter_allowed_non_mcs_connector_is_unchanged) {
    const std::vector<EnergyTransferMode> allowed{
        EnergyTransferMode::DC,
        EnergyTransferMode::DC_BPT,
        EnergyTransferMode::AC_three_phase_core,
    };

    const auto result = module::filter_allowed_energy_transfers(allowed, types::evse_manager::ConnectorTypeEnum::cCCS2);

    EXPECT_EQ(result, allowed);
}

TEST(EnergyTransferModesTest, filter_allowed_mcs_replaces_dc_modes) {
    const std::vector<EnergyTransferMode> allowed{
        EnergyTransferMode::AC_single_phase_core,
        EnergyTransferMode::DC,
        EnergyTransferMode::MCS,
        EnergyTransferMode::DC_BPT,
    };

    const auto result = module::filter_allowed_energy_transfers(allowed, types::evse_manager::ConnectorTypeEnum::cMCS);

    const std::vector<EnergyTransferMode> expected{
        EnergyTransferMode::AC_single_phase_core,
        EnergyTransferMode::MCS,
        EnergyTransferMode::MCS,
        EnergyTransferMode::MCS_BPT,
    };
    EXPECT_EQ(result, expected);
}
