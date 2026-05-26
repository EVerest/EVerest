// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/session.hpp>

#include <algorithm>
#include <random>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d2 {

namespace dt = d2::msg::data_types;

Session::Session() {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<uint8_t> distribution(0x00, 0xff);

    for (auto& item : id) {
        item = distribution(generator);
    }
}

dt::ServiceParameterList Session::get_service_parameter_list(dt::ServiceID service_id) {
    dt::ServiceParameterList ret;

    auto category = dt::convert_service_id_to_service_category(service_id);
    if (category == dt::ServiceCategory::ContractCertificate) {
        for (const auto& entry : offered_services.certificate_service_parameter_list) {
            // ISO 15118-2 Table 106
            if (entry == dt::CertificateService::Installation) {
                dt::ParameterSet set{1, {{"Service", "Installation"}}};
                ret.push_back(set);
            } else if (entry == dt::CertificateService::Update) {
                dt::ParameterSet set{2, {{"Service", "update"}}};
                ret.push_back(set);
            }
        }
    } else if (category == dt::ServiceCategory::Internet) {
        // TODO(kd): ISO 15118-2 Table 107
        for (const auto& [id, parameters] : offered_services.internet_access_parameter_list) {
            dt::ParameterSet set;
            std::string protocol;
            switch (parameters.protocol) {
            case dt::Protocol::Ftp:
                protocol = "ftp";
                break;
            case dt::Protocol::Http:
                protocol = "http";
                break;
            case dt::Protocol::Https:
                protocol = "https";
                break;
            default:
                break;
            }

            set.parameter_set_id = id;
            set.parameter.emplace_back("Protocol", protocol);
            set.parameter.emplace_back("Port", parameters.port);
            ret.push_back(set);
        }
    }
    return ret;
}

void Session::select_service(dt::SelectedService service) {
    select_service(service.service_id, service.parameter_set_id);
}

void Session::select_service(dt::ServiceID service_id, std::optional<dt::ParameterSetID> parameter_set_id) {
    const auto category = dt::convert_service_id_to_service_category(service_id);
    if (category == dt::ServiceCategory::EvCharging) {
        charge_service_selected = true;
    } else if (category == dt::ServiceCategory::ContractCertificate) {
        if (not parameter_set_id.has_value()) {
            return;
        }
        // ISO 15118-2 Table 106
        if (parameter_set_id.value() == 1) {
            selected_vas_parameter.certificate_service = dt::CertificateService::Installation;
        } else if (parameter_set_id.value() == 2) {
            selected_vas_parameter.certificate_service = dt::CertificateService::Update;
        }
    } else if (category == dt::ServiceCategory::Internet) {
        if (not parameter_set_id.has_value()) {
            return;
        }
        auto parameters =
            std::find_if(offered_services.internet_access_parameter_list.begin(),
                         offered_services.internet_access_parameter_list.end(),
                         [&](const auto& parameters) { return parameters.first == parameter_set_id.value(); });
        if (parameters == offered_services.internet_access_parameter_list.end()) {
            return;
        }
        selected_vas_parameter.internet_access = parameters->second;
    } else if (category == dt::ServiceCategory::OtherCustom) {
        // TODO(kd): We need a way to handle custom VAS
    }
}

Session::~Session() = default;

} // namespace iso15118::d2
