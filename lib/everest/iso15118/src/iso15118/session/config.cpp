// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/session/config.hpp>

#include <algorithm>
#include <bitset>
#include <utility>

#include <iso15118/detail/helper.hpp>

namespace iso15118::session {

EvSessionConfig::EvSessionConfig(EvSetupConfig config) :
    evcc_id(std::move(config.evcc_id)),
    supported_energy_services(std::move(config.supported_energy_services)),
    preferred_control_mode(config.preferred_control_mode),
    supported_auth_options(std::move(config.supported_auth_options)),
    custom_protocol(std::move(config.custom_protocol)),
    supported_protocols(std::move(config.supported_protocols)),
    evcc_mac(config.evcc_mac),
    iso2_energy_transfer_mode(config.iso2_energy_transfer_mode),
    iso2_ac_e_amount(config.iso2_ac_e_amount),
    iso2_ac_ev_max_voltage(config.iso2_ac_ev_max_voltage),
    iso2_ac_ev_max_current(config.iso2_ac_ev_max_current),
    iso2_ac_ev_min_current(config.iso2_ac_ev_min_current),
    resumed_session_id(config.resumed_session_id),
    dc_charge_parameters(std::move(config.dc_charge_parameters)),
    dc_bpt_charge_parameters(std::move(config.dc_bpt_charge_parameters)),
    ac_charge_parameters(std::move(config.ac_charge_parameters)),
    iso2_pnc(std::move(config.iso2_pnc)),
    has_cp_state_feedback(config.has_cp_state_feedback) {

    if (supported_auth_options.empty()) {
        supported_auth_options = {dt::Authorization::EIM};
    }
}

namespace {

auto get_mobility_needs_mode(const d20::ControlMobilityNeedsModes& mode) {
    using namespace dt;

    if (mode.control_mode == ControlMode::Scheduled and mode.mobility_mode == MobilityNeedsMode::ProvidedBySecc) {
        logf_info("Setting the mobility needs mode to ProvidedByEvcc. In scheduled mode only ProvidedByEvcc is "
                  "supported.");
        return MobilityNeedsMode::ProvidedByEvcc;
    }

    return mode.mobility_mode;
}

auto get_default_ac_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes,
                                   const d20::AcSetupConfig& ac_setup_config) {
    using namespace dt;

    std::vector<AcParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        for (const auto& connector : ac_setup_config.connectors) {
            param_list.push_back({
                connector,
                mode.control_mode,
                get_mobility_needs_mode(mode),
                ac_setup_config.voltage,
                Pricing::NoPricing,
            });
        }
    }

    return param_list;
}

auto get_default_ac_bpt_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes,
                                       const d20::AcSetupConfig& ac_setup_config,
                                       const d20::BptSetupConfig& bpt_setup_config) {
    using namespace dt;

    std::vector<AcBptParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        for (const auto& connector : ac_setup_config.connectors) {
            param_list.push_back(
                {{
                     connector,
                     mode.control_mode,
                     get_mobility_needs_mode(mode),
                     ac_setup_config.voltage,
                     Pricing::NoPricing,
                 },
                 bpt_setup_config.bpt_channel,
                 bpt_setup_config.generator_mode,
                 bpt_setup_config.grid_code_detection_method.value_or(dt::GridCodeIslandingDetectionMethod::Passive)});
        }
    }

    return param_list;
}

auto get_default_ac_der_iec_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes,
                                           const d20::AcSetupConfig& ac_setup_config,
                                           const d20::DerSetupConfig& der_setup_config) {
    using namespace dt;

    std::vector<AcDerParameterList> param_list;

    constexpr auto MAX_IEC_CONTROL_FUNCTIONS = 12;
    std::bitset<MAX_IEC_CONTROL_FUNCTIONS> control_functions{};

    static_assert(MAX_IEC_CONTROL_FUNCTIONS ==
                      message_20::to_underlying_value(iec::DERControlName::UnderVoltageFaultRideThroughMode) + 1,
                  "MAX_IEC_CONTROL_FUNCTIONS should be in sync with the DERControlName enum definition");

    for (const auto& function : der_setup_config.supported_der_control_functions) {
        control_functions.set(static_cast<size_t>(function.first), true);
    }

    for (const auto& mode : control_mobility_modes) {
        for (const auto& connector : ac_setup_config.connectors) {
            param_list.push_back({{
                                      connector,
                                      mode.control_mode,
                                      get_mobility_needs_mode(mode),
                                      ac_setup_config.voltage,
                                      Pricing::NoPricing,
                                  },
                                  control_functions});
        }
    }

    return param_list;
}

auto get_default_dc_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector

    std::vector<DcParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({
            DcConnector::Extended,
            mode.control_mode,
            get_mobility_needs_mode(mode),
            Pricing::NoPricing,
        });
    }

    return param_list;
}

auto get_default_dc_bpt_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes,
                                       const d20::BptSetupConfig& bpt_setup_config) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector

    std::vector<DcBptParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({{
                                  DcConnector::Extended,
                                  mode.control_mode,
                                  get_mobility_needs_mode(mode),
                                  Pricing::NoPricing,
                              },
                              bpt_setup_config.bpt_channel,
                              bpt_setup_config.generator_mode});
    }

    return param_list;
}

auto get_default_mcs_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector
    std::vector<McsParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({
            McsConnector::Mcs,
            mode.control_mode,
            get_mobility_needs_mode(mode),
            Pricing::NoPricing,
        });
    }

    return param_list;
}

auto get_default_mcs_bpt_parameter_list(const std::vector<d20::ControlMobilityNeedsModes>& control_mobility_modes,
                                        const d20::BptSetupConfig& bpt_setup_config) {
    using namespace dt;

    // TODO(sl): Add check if a control mode is more than one in that vector
    std::vector<McsBptParameterList> param_list;

    for (const auto& mode : control_mobility_modes) {
        param_list.push_back({{
                                  McsConnector::Mcs,
                                  mode.control_mode,
                                  get_mobility_needs_mode(mode),
                                  Pricing::NoPricing,
                              },
                              bpt_setup_config.bpt_channel,
                              bpt_setup_config.generator_mode});
    }

    return param_list;
}

} // namespace

SessionConfig::SessionConfig(EvseSetupConfig config) :
    evse_id(std::move(config.evse_id)),
    cert_install_service(config.enable_certificate_install_service),
    authorization_services(std::move(config.authorization_services)),
    supported_energy_transfer_services(std::move(config.supported_energy_services)),
    supported_vas_services(std::move(config.supported_vas_services)),
    dc_limits(std::move(config.dc_limits)),
    ac_limits(std::move(config.ac_limits)),
    der_limits(std::move(config.der_limits)),
    powersupply_limits(std::move(config.powersupply_limits)),
    supported_control_mobility_modes(std::move(config.control_mobility_modes)),
    custom_protocol(std::move(config.custom_protocol)),
    selecting_sap_based_on_energy_service(config.selecting_sap_based_on_energy_service),
    supported_protocols(std::move(config.supported_protocols)),
    iso2_pnc_enabled(config.iso2_pnc_enabled),
    iso2_receipt_required(config.iso2_receipt_required),
    contract_mo_root_path(std::move(config.contract_mo_root_path)),
    contract_v2g_root_path(std::move(config.contract_v2g_root_path)),
    central_contract_validation_allowed(config.central_contract_validation_allowed) {

    // TODO(SL): How to handle this probaly
    const auto is_dc_bpt_service = [](dt::ServiceCategory service) {
        return service == dt::ServiceCategory::DC_BPT or service == dt::ServiceCategory::MCS_BPT;
    };
    const auto dc_bpt_found = std::any_of(supported_energy_transfer_services.begin(),
                                          supported_energy_transfer_services.end(), is_dc_bpt_service);

    if (dc_bpt_found and not dc_limits.discharge_limits.has_value()) {
        logf_warning("The supported energy services contain DC_BPT or MCS_BPT, but dc limits does not contain BPT "
                     "limits. This can lead to session shutdowns.");
    }

    const auto is_ac_bpt_service = [](dt::ServiceCategory service) { return service == dt::ServiceCategory::AC_BPT; };
    const auto ac_bpt_found = std::any_of(supported_energy_transfer_services.begin(),
                                          supported_energy_transfer_services.end(), is_ac_bpt_service);

    if (ac_bpt_found and not ac_limits.discharge_power.has_value()) {
        logf_warning("The supported energy services contain AC_BPT, but ac limits does not contain BPT limits. This "
                     "can lead to session shutdowns.");
    }

    const auto is_ac_der_iec_service = [](dt::ServiceCategory service) {
        return service == dt::ServiceCategory::AC_DER_IEC;
    };
    const auto ac_der_iec_found = std::any_of(supported_energy_transfer_services.begin(),
                                              supported_energy_transfer_services.end(), is_ac_der_iec_service);
    if (ac_der_iec_found and not der_limits.has_value()) {
        logf_warning("The supported energy services contain AC_DER_IEC, but there is no der limits defined. This "
                     "can lead to session shutdowns.");
    }

    if (supported_control_mobility_modes.empty()) {
        logf_warning("No control modes were provided, set to scheduled mode");
        supported_control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    }

    const auto ac_setup_config =
        config.ac_setup_config.value_or(d20::AcSetupConfig({230, {dt::AcConnector::SinglePhase}}));
    const auto ac_bpt_setup_config = config.bpt_setup_config.value_or(d20::BptSetupConfig(
        {dt::BptChannel::Unified, dt::GeneratorMode::GridFollowing, dt::GridCodeIslandingDetectionMethod::Passive}));
    const auto dc_bpt_setup_config = config.bpt_setup_config.value_or(
        d20::BptSetupConfig({dt::BptChannel::Unified, dt::GeneratorMode::GridFollowing, std::nullopt}));
    der_setup_config = config.der_setup_config.value_or(
        d20::DerSetupConfig({{}, iec::OperatingMode::GridFollowing, iec::GridConnectionMode::GridConnected}));

    ac_parameter_list = get_default_ac_parameter_list(supported_control_mobility_modes, ac_setup_config);
    ac_bpt_parameter_list =
        get_default_ac_bpt_parameter_list(supported_control_mobility_modes, ac_setup_config, ac_bpt_setup_config);
    ac_der_iec_parameter_list =
        get_default_ac_der_iec_parameter_list(supported_control_mobility_modes, ac_setup_config, der_setup_config);

    dc_parameter_list = get_default_dc_parameter_list(supported_control_mobility_modes);
    dc_bpt_parameter_list = get_default_dc_bpt_parameter_list(supported_control_mobility_modes, dc_bpt_setup_config);

    mcs_parameter_list = get_default_mcs_parameter_list(supported_control_mobility_modes);
    mcs_bpt_parameter_list = get_default_mcs_bpt_parameter_list(supported_control_mobility_modes, dc_bpt_setup_config);
}

} // namespace iso15118::session
