// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/service_discovery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

namespace {
din::SessionConfig make_config() {
    din::SessionConfig config;
    config.charge_service_id = 1;
    config.free_service = true;
    config.energy_transfer_mode = dt::SupportedEnergyTransferMode::DC_extended;
    return config;
}
} // namespace

SCENARIO("DIN SECC ServiceDiscovery state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A matching session id") {
        message_din::ServiceDiscoveryRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, make_config(), session);

        THEN("Only ExternalPayment and the DC charge service are offered") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.payment_options.size() == 1);
            REQUIRE(res.payment_options.front() == dt::PaymentOption::ExternalPayment);
            REQUIRE(res.charge_service.service_tag.service_id == 1);
            REQUIRE(res.charge_service.service_tag.service_category == dt::ServiceCategory::EVCharging);
            REQUIRE(res.charge_service.energy_transfer_type == dt::SupportedEnergyTransferMode::DC_extended);
        }
    }

    GIVEN("A mismatching session id") {
        message_din::ServiceDiscoveryRequest req;
        req.header.session_id = dt::SessionId{}; // all zero

        const auto res = din::state::handle_request(req, make_config(), session);

        THEN("ResponseCode is FAILED_UnknownSession") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }
}
