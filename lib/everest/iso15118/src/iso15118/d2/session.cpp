// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/session.hpp>

#include <random>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d2 {

Session::Session() {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<uint8_t> distribution(0x00, 0xff);

    for (auto& item : id) {
        item = distribution(generator);
    }
}

void Session::select_service(dt::SelectedService service) {
    for (const auto& selected_service : selected_services) {
        if (service.service_id == selected_service.service_id) {
            // TODO(kd) raise an exception? Overwrite the service?
            return;
        }
    }
    selected_services.push_back(service);
}

void Session::select_service(dt::ServiceID service_id, std::optional<dt::ParameterSetID> parameter_set_id) {
    return select_service(dt::SelectedService{service_id, parameter_set_id});
}

Session::~Session() = default;

} // namespace iso15118::d2
