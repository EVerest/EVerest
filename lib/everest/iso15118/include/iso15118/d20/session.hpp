// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/io/sha_hash.hpp>
#include <iso15118/message/common_types.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d20 {

namespace dt = message_20::datatypes;

// Key: service IDs, value: vector with the parameter set ids
using CustomVasList = std::map<std::uint16_t, std::vector<uint16_t>>;

struct OfferedServices {

    everest::lib::util::fixed_vector<dt::Authorization, 2> auth_services;
    std::vector<dt::ServiceCategory> energy_services;
    std::vector<uint16_t> vas_services;

    std::map<uint8_t, dt::AcParameterList> ac_parameter_list;
    std::map<uint8_t, dt::AcBptParameterList> ac_bpt_parameter_list;
    std::map<uint8_t, dt::AcDerParameterList> ac_der_iec_parameter_list;
    std::map<uint8_t, dt::DcParameterList> dc_parameter_list;
    std::map<uint8_t, dt::DcBptParameterList> dc_bpt_parameter_list;
    std::map<uint8_t, dt::McsParameterList> mcs_parameter_list;
    std::map<uint8_t, dt::McsBptParameterList> mcs_bpt_parameter_list;
    std::map<uint8_t, dt::InternetParameterList> internet_parameter_list;
    std::map<uint8_t, dt::ParkingParameterList> parking_parameter_list;
    CustomVasList custom_vas_list;
};

struct SelectedServiceParameters {

    dt::ServiceCategory selected_energy_service;

    std::variant<dt::AcConnector, dt::DcConnector, dt::McsConnector> selected_connector;
    dt::ControlMode selected_control_mode;
    dt::MobilityNeedsMode selected_mobility_needs_mode;
    dt::Pricing selected_pricing;

    // BPT
    std::optional<dt::BptChannel> selected_bpt_channel;
    std::optional<dt::GeneratorMode> selected_generator_mode;

    // AC specific
    std::optional<float> evse_nominal_voltage;
    std::optional<dt::GridCodeIslandingDetectionMethod> selected_grid_code_method;

    std::bitset<12> selected_der_control_functions;

    SelectedServiceParameters() = default;
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::DcConnector dc_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_);
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::DcConnector dc_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_,
                              dt::BptChannel channel_, dt::GeneratorMode generator_);
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::McsConnector mcs_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_);
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::McsConnector mcs_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_,
                              dt::BptChannel channel_, dt::GeneratorMode generator_);
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::AcConnector ac_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_,
                              float nominal_voltage_);
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::AcConnector ac_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_,
                              dt::BptChannel channel_, dt::GeneratorMode generator_, float nominal_voltage_,
                              dt::GridCodeIslandingDetectionMethod grid_code_method_);
    SelectedServiceParameters(dt::ServiceCategory energy_service_, dt::AcConnector ac_connector_,
                              dt::ControlMode control_mode_, dt::MobilityNeedsMode mobility_, dt::Pricing pricing_,
                              float nominal_voltage_, std::bitset<12> der_control_functions_);
};

// Todo(sl): missing services
// WPT -> ControlMode, Pricing
// DC_ACDP -> ControlMode, MobilityNeedsMode
// DC_ACDP_BPT -> ControlMode, MobilityNeedsMode, BPTChannel

struct SelectedVasParameter {
    std::vector<dt::ServiceCategory> vas_services;

    dt::Protocol internet_protocol;
    dt::Port internet_port;

    dt::IntendedService parking_intended_service;
    dt::ParkingStatus parking_status;
};

// TODO(SL): How to handle d2 pause? Move Struct to a seperate header file?
// TODO(SL): Missing handling scheduletuple in schedule mode [V2G20-1058]
struct PauseContext {
    io::sha512_hash_t vehicle_cert_session_id_hash{};
    std::array<uint8_t, 8> old_session_id{};
    SelectedServiceParameters selected_service_parameters{};
};

class Session {

    // TODO(sl): move to a common defs file
    static constexpr auto ID_LENGTH = 8;

public:
    Session();
    Session(const PauseContext& pause_ctx);
    Session(SelectedServiceParameters);
    Session(OfferedServices);

    std::array<uint8_t, ID_LENGTH> get_id() const {
        return id;
    }

    bool find_energy_parameter_set_id(const dt::ServiceCategory service, int16_t id);
    bool find_vas_parameter_set_id(const uint16_t vas_service, int16_t id);

    void selected_service_parameters(const dt::ServiceCategory service, const uint16_t id);
    void selected_service_parameters(const uint16_t vas_service, const uint16_t id);

    [[nodiscard]] auto get_selected_services() const& {
        return selected_services;
    }

    [[nodiscard]] bool is_ac_charger() const {
        return selected_services.selected_energy_service == dt::ServiceCategory::AC or
               selected_services.selected_energy_service == dt::ServiceCategory::AC_BPT;
    }

    [[nodiscard]] bool is_ac_der_iec_charger() const {
        return selected_services.selected_energy_service == dt::ServiceCategory::AC_DER_IEC;
    }

    [[nodiscard]] bool is_ac_der_sae_charger() const {
        return selected_services.selected_energy_service == dt::ServiceCategory::AC_DER_SAE;
    }

    [[nodiscard]] bool is_dc_charger() const {
        return selected_services.selected_energy_service == dt::ServiceCategory::DC or
               selected_services.selected_energy_service == dt::ServiceCategory::DC_BPT or
               selected_services.selected_energy_service == dt::ServiceCategory::MCS or
               selected_services.selected_energy_service == dt::ServiceCategory::MCS_BPT;
    }

    void set_ev_supported_sae_functions(std::uint32_t bitmap) {
        ev_supported_sae_functions.emplace(bitmap);
    }

    [[nodiscard]] std::optional<std::uint32_t> get_ev_supported_sae_functions() const {
        return ev_supported_sae_functions;
    }

    void set_enabled_der_control_modes(std::uint32_t bitmap) {
        enabled_der_control_modes = bitmap;
    }

    [[nodiscard]] std::uint32_t get_enabled_der_control_modes() const {
        return enabled_der_control_modes;
    }

    void record_der_control_sent(std::uint32_t revision) {
        der_control_sent_revision.emplace(revision);
    }

    [[nodiscard]] bool der_control_changed_since_cpd(std::uint32_t config_revision) const {
        return der_control_sent_revision != config_revision;
    }

    void record_der_modes_sent(sae::RequiredDEROperatingMode operating_mode, sae::GridConnectionMode connection_mode) {
        sent_required_der_operating_mode.emplace(operating_mode);
        sent_grid_connection_mode.emplace(connection_mode);
    }

    [[nodiscard]] std::optional<sae::RequiredDEROperatingMode> get_sent_required_der_operating_mode() const {
        return sent_required_der_operating_mode;
    }

    [[nodiscard]] std::optional<sae::GridConnectionMode> get_sent_grid_connection_mode() const {
        return sent_grid_connection_mode;
    }

    ~Session();

    OfferedServices offered_services;

    bool service_renegotiation_supported{false};

private:
    // NOTE (aw): could be const
    std::array<uint8_t, ID_LENGTH> id{};

    SelectedServiceParameters selected_services{};
    SelectedVasParameter selected_vas_services{};

    // The EV's masked SupportedModes declaration, empty until the SAE CPD has run.
    std::optional<std::uint32_t> ev_supported_sae_functions{};

    // The SAE bits the SECC actually enabled in the CPD response, which is what the EV's EnabledModes echo is
    // compared against. Zero until the SAE CPD has run.
    std::uint32_t enabled_der_control_modes{0};

    // The DerSaeSetupConfig::revision last sent to the EV, to be compared against the configured one. Empty
    // until the SAE CPD has run.
    std::optional<std::uint32_t> der_control_sent_revision{};

    // Last-sent Annex M mode enums, for [V2G20-3358]-[V2G20-3361] per-field resend (ADR-0027).
    std::optional<sae::RequiredDEROperatingMode> sent_required_der_operating_mode{};
    std::optional<sae::GridConnectionMode> sent_grid_connection_mode{};
};

} // namespace iso15118::d20
