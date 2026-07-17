// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <iso15118/detail/d2/state/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC ServiceDiscovery handling") {
    const dt::SessionId id{};
    message_2::ServiceDiscoveryRequest req;

    everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6> modes;
    modes.push_back(dt::EnergyTransferMode::DC_extended);
    modes.push_back(dt::EnergyTransferMode::DC_core);

    const auto res = d2::state::handle_request(req, id, 1, modes, false);

    THEN("OK, EIM-only payment options and DC ChargeService advertised") {
        REQUIRE(res.response_code == dt::ResponseCode::OK);
        REQUIRE(res.payment_option_list.size() == 1);
        REQUIRE(res.payment_option_list[0] == dt::PaymentOption::ExternalPayment);
        REQUIRE(res.charge_service.service_id == 1);
        REQUIRE(res.charge_service.service_category == dt::ServiceCategory::EVCharging);
        const auto& adv = res.charge_service.supported_energy_transfer_mode;
        REQUIRE(std::find(adv.begin(), adv.end(), dt::EnergyTransferMode::DC_extended) != adv.end());
    }
}
