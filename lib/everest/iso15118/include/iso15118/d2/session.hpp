// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>

#include <iso15118/message/d2/msg_data_types.hpp>

namespace dt = iso15118::d2::msg::data_types;

namespace iso15118::d2 {

// TODO(kd): Move to msg_data_types.hpp?
namespace msg::data_types {
enum class CertificateService {
    Installation,
    Update,
};

enum class Protocol {
    Ftp,
    Http,
    Https,
};

typedef uint16_t Port;
} // namespace msg::data_types

struct InternetAccessParameter {
    dt::Protocol protocol;
    dt::Port port;
};

struct OfferedServices {
    std::vector<dt::CertificateService> certificate_service_parameter_list;
    // Parameter set IDs should comply with ISO 15118-2:2014 Table 107
    std::map<dt::ParameterSetID, InternetAccessParameter> internet_access_parameter_list;
};

struct SelectedVasParameter {
    std::optional<dt::CertificateService> certificate_service;
    std::optional<InternetAccessParameter> internet_access;
};

class Session {

    // TODO(sl): move to a common defs file
    static constexpr auto ID_LENGTH = 8;

public:
    Session();

    std::array<uint8_t, ID_LENGTH> get_id() const {
        return id;
    }

    // bool is_ac_charger() const {
    //     return selected_services.selected_energy_service == dt::ServiceCategory::AC or
    //            selected_services.selected_energy_service == dt::ServiceCategory::AC_BPT;
    // }

    // bool is_dc_charger() const {
    //     return selected_services.selected_energy_service == dt::ServiceCategory::DC or
    //            selected_services.selected_energy_service == dt::ServiceCategory::DC_BPT or
    //            selected_services.selected_energy_service == dt::ServiceCategory::MCS or
    //            selected_services.selected_energy_service == dt::ServiceCategory::MCS_BPT;
    // }

    void set_selected_payment_option(const dt::PaymentOption& option) {
        selected_payment_option = option;
    };
    std::optional<dt::PaymentOption> get_selected_payment_option() const {
        return selected_payment_option;
    };

    dt::ServiceParameterList get_service_parameter_list(dt::ServiceID service_id);

    void select_service(dt::SelectedService service);
    void select_service(dt::ServiceID service_id, std::optional<dt::ParameterSetID> parameter_set_id);

    ~Session();

private:
    // NOTE (aw): could be const
    std::array<uint8_t, ID_LENGTH> id{};

    bool service_renegotiation_supported{false};

    OfferedServices offered_services;
    bool charge_service_selected{false};
    std::optional<dt::PaymentOption> selected_payment_option{std::nullopt};
    SelectedVasParameter selected_vas_parameter{};
};

} // namespace iso15118::d2
