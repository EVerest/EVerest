// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>

namespace iso15118::d2 {

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

    ~Session();

private:
    // NOTE (aw): could be const
    std::array<uint8_t, ID_LENGTH> id{};

    bool service_renegotiation_supported{false};
};

} // namespace iso15118::d2
