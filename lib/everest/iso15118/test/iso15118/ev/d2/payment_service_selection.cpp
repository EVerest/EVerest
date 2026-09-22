// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "../../test_pki.hpp"
#include "helper.hpp"

#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/ev/d2/state/service_discovery.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::PaymentServiceSelectionResponse make_response(const message_2::Header& header, dt::ResponseCode code) {
    message_2::PaymentServiceSelectionResponse res;
    res.header = header;
    res.response_code = code;
    return res;
}

// A minimal contract so PnCConfig::needs_cert_install() is false.
ev::params::EvSessionParams params_with_contract() {
    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.prefer_contract = true;
    params.pnc.contract = ev::d2::ContractMaterial{{0x30, 0x01}, {}, "key", std::nullopt, "DEPNXCONTRACT1"};
    return params;
}

ev::params::EvSessionParams params_prefer_contract() {
    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.prefer_contract = true;
    return params;
}

message_2::ServiceDiscoveryResponse service_discovery_response(bool contract, bool certificate_service,
                                                               bool eim = true) {
    message_2::ServiceDiscoveryResponse res;
    res.header = d2_header();
    res.response_code = dt::ResponseCode::OK;
    if (eim) {
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    }
    if (contract) {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
    }
    res.charge_service.service_id = dt::CHARGE_SERVICE_ID;
    res.charge_service.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
    if (certificate_service) {
        dt::Service service;
        service.service_id = dt::CERTIFICATE_SERVICE_ID;
        service.service_category = dt::ServiceCategory::ContractCertificate;
        service.free_service = true;
        res.service_list.emplace().push_back(service);
    }
    return res;
}

// PaymentServiceSelection entered the way a session reaches it, from the ServiceDiscovery response.
auto by_service_discovery(bool contract, bool certificate_service) {
    return d2_entered_by([contract, certificate_service](D2StateHelper& helper) {
        return d2_successor_of<ev::d2::state::ServiceDiscovery>(
            helper, service_discovery_response(contract, certificate_service));
    });
}

const auto offering_eim = by_service_discovery(false, false);
const auto offering_contract = by_service_discovery(true, true);
const auto offering_installation = d2_entered_by([](D2StateHelper& helper) {
    auto state = d2_successor_of<ev::d2::state::ServiceDiscovery>(helper, service_discovery_response(true, true));
    REQUIRE(take_all_requests(helper.get_message_exchange()).types() ==
            std::vector<message_2::Type>{message_2::Type::ServiceDetailReq});
    auto res = d2_ok<message_2::ServiceDetailResponse>();
    res.service_id = dt::CERTIFICATE_SERVICE_ID;
    dt::ParameterSet set{};
    set.parameter_set_id = 1;
    res.service_parameter_list.emplace().push_back(set);
    helper.handle_response(res);
    return std::move(state->feed(ev::d2::Event::V2GTP_MESSAGE).new_state);
});
const auto offering_contract_only = by_service_discovery(true, false);

// Captures the error lines logged during its lifetime; the destructor restores a no-op callback, so a
// failed REQUIRE leaves no callback into freed storage.
struct ErrorLog {
    ErrorLog() {
        io::set_logging_callback([this](LogLevel level, std::string message) {
            if (level == LogLevel::Error) {
                lines.push_back(std::move(message));
            }
        });
    }
    ~ErrorLog() {
        io::set_logging_callback([](LogLevel, const std::string&) {});
    }
    ErrorLog(const ErrorLog&) = delete;
    ErrorLog& operator=(const ErrorLog&) = delete;

    bool contains(const std::string& text) const {
        return std::any_of(lines.begin(), lines.end(),
                           [&text](const std::string& line) { return line.find(text) != std::string::npos; });
    }

    std::vector<std::string> lines;
};

} // namespace

SCENARIO("ISO15118-2 EV PaymentServiceSelection selects ExternalPayment by default") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, d2_no_seed, offering_eim};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
    REQUIRE(request->selected_service_list.size() == 1);
    REQUIRE(request->selected_service_list.front().service_id == dt::CHARGE_SERVICE_ID);
    REQUIRE(primed.ctx.pnc.contract_selected == false);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection adds the Certificate service for an installation") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_prefer_contract(), false, d2_no_seed,
                                                               offering_installation};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
    REQUIRE(request->selected_service_list.size() == 2);
    REQUIRE(request->selected_service_list.at(1).service_id == dt::CERTIFICATE_SERVICE_ID);
    REQUIRE(request->selected_service_list.at(1).parameter_set_id == 1);
    REQUIRE(primed.ctx.pnc.contract_selected == true);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection routes EIM to Authorization") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, d2_no_seed, offering_eim};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection routes a missing contract to CertificateInstallation") {
    const ev::feedback::Callbacks callbacks{};
    const auto pki = test_pki::make_pki("prime256v1");
    auto params = params_prefer_contract();
    params.v2g_root_cert_path = pki.root_bundle_path;
    params.pnc.oem_prov_cert_der = pki.leaf_der();
    params.pnc.oem_prov_key_pem = test_pki::key_to_pem(pki.leaf_key.get());
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, std::move(params), false, d2_no_seed,
                                                               offering_installation};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CertificateInstallation);
    const auto requests = primed.take_requests();
    REQUIRE(requests.types() == std::vector<message_2::Type>{message_2::Type::CertificateInstallationReq});
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection routes a preloaded contract to PaymentDetails") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_with_contract(), false, d2_no_seed,
                                                               offering_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PaymentDetails);
    REQUIRE(primed.ctx.pnc.contract.has_value());
    REQUIRE(primed.ctx.pnc.contract->emaid == "DEPNXCONTRACT1");
    REQUIRE(primed.ctx.pnc.contract->cert_der.size() == 2);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection selects Contract when enforced without an offer") {
    const ev::feedback::Callbacks callbacks{};
    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.enforce_contract = true;
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params, false, d2_no_seed, offering_eim};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
    // The Certificate service was not offered, so it is not selected.
    REQUIRE(request->selected_service_list.size() == 1);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection falls back to EIM without usable contract material") {
    const ev::feedback::Callbacks callbacks{};
    // Contract offered and preferred, but the SECC offers no Certificate service and the EV carries no
    // pre-installed contract: nothing to authenticate with.
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params_prefer_contract(), false, d2_no_seed,
                                                               offering_contract_only};

    const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
    REQUIRE(request->selected_service_list.size() == 1);
    REQUIRE(primed.ctx.pnc.contract_selected == false);

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::Authorization);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection keeps Contract without material when enforced") {
    const ev::feedback::Callbacks callbacks{};
    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.enforce_contract = true;
    PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, params, false, d2_no_seed, offering_eim};

    REQUIRE(primed.ctx.pnc.contract_selected == true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PaymentDetails);
}

SCENARIO("ISO15118-2 EV PaymentServiceSelection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::PaymentServiceSelection>()};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    const message_2::AuthorizationResponse wrong{d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished};
    check_rejection_paths(callbacks, ev::d2::StateID::PaymentServiceSelection, make_fsm, make_ok, wrong);
}

// Plug & Charge on a plaintext link would put the contract chain and the eMAID on the wire in
// clear. The vehicle charges by EIM instead, which is the correction this repository's own charger
// already makes from the other end, unless the deviation is accepted deliberately.
SCENARIO("ISO15118-2 EV PaymentServiceSelection refuses Contract on a plaintext session") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("a charger offering Contract and an EV preferring it, over a session with no TLS") {
        auto params = params_with_contract();
        params.secure_transport = false;
        PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, std::move(params), false, d2_no_seed,
                                                                   offering_contract};

        THEN("ExternalPayment is selected") {
            const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
            REQUIRE(primed.ctx.pnc.contract_selected == false);
        }
    }

    GIVEN("the same session with enforce_contract set") {
        auto params = params_with_contract();
        params.secure_transport = false;
        params.pnc.enforce_contract = true;
        PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, std::move(params), false, d2_no_seed,
                                                                   offering_contract};

        THEN("ExternalPayment is still selected: the conformance override does not defeat the transport rule") {
            const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE(request->selected_payment_option == dt::PaymentOption::ExternalPayment);
            REQUIRE(primed.ctx.pnc.contract_selected == false);
        }
    }

    GIVEN("the same session with allow_insecure_pnc set") {
        auto params = params_with_contract();
        params.secure_transport = false;
        params.pnc.allow_insecure_pnc = true;
        PrimedState<ev::d2::state::PaymentServiceSelection> primed{callbacks, std::move(params), false, d2_no_seed,
                                                                   offering_contract};

        THEN("Contract is selected: the deviation was accepted deliberately") {
            const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
            REQUIRE(primed.ctx.pnc.contract_selected == true);
        }
    }
}

// [V2G2-465]: the EV selects only a payment option the SECC offered, so where it would fall back to
// ExternalPayment and the SECC offered only Contract, it stops instead.
SCENARIO("ISO15118-2 EV PaymentServiceSelection never selects an unoffered ExternalPayment") {
    const ev::feedback::Callbacks callbacks{};
    const auto stops_after_discovery = [&callbacks](ev::params::EvSessionParams params) {
        PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, std::move(params), false, d2_no_seed};
        primed.handle_response(service_discovery_response(true, false, false));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
        REQUIRE(primed.take_requests().types() == std::vector<message_2::Type>{message_2::Type::SessionStopReq});
    };

    GIVEN("Contract preferred without contract material and no Certificate service offered") {
        const ErrorLog errors;
        stops_after_discovery(params_prefer_contract());
        REQUIRE(errors.contains("No usable contract certificate and no ExternalPayment offer"));
    }

    GIVEN("Contract preferred without contract material on a plaintext session that allows insecure PnC") {
        auto params = params_prefer_contract();
        params.secure_transport = false;
        params.pnc.allow_insecure_pnc = true;
        const ErrorLog errors;
        stops_after_discovery(std::move(params));
        REQUIRE(errors.contains("No usable contract certificate and no ExternalPayment offer"));
    }

    GIVEN("Contract preferred with contract material on a plaintext session") {
        auto params = params_with_contract();
        params.secure_transport = false;
        const ErrorLog errors;
        stops_after_discovery(std::move(params));
        REQUIRE(errors.contains("the session is plaintext and allow_insecure_pnc is off"));
    }

    GIVEN("Contract enforced on a plaintext session") {
        auto params = params_with_contract();
        params.secure_transport = false;
        params.pnc.enforce_contract = true;
        const ErrorLog errors;
        stops_after_discovery(std::move(params));
        REQUIRE(errors.contains("the session is plaintext and allow_insecure_pnc is off"));
    }

    GIVEN("no Plug and Charge preference") {
        // ServiceDiscovery stops on this offer itself; the selection guards it as well.
        D2StateHelper helper{callbacks};
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        ctx.evse_info.selected_charge_service_id = dt::CHARGE_SERVICE_ID;
        ctx.evse_info.contract_offered = true;
        ctx.evse_info.eim_offered = false;
        const ErrorLog errors;
        const auto state = ev::d2::state::to_payment_service_selection(ctx);
        REQUIRE(state->get_id() == ev::d2::StateID::SessionStop);
        REQUIRE(errors.contains("Plug and Charge is not enabled"));
        REQUIRE(take_all_requests(helper.get_message_exchange()).types() ==
                std::vector<message_2::Type>{message_2::Type::SessionStopReq});
    }

    GIVEN("Contract enforced without contract material on a secure session") {
        ev::params::EvSessionParams params;
        params.secure_transport = true;
        params.pnc.enforce_contract = true;
        PrimedState<ev::d2::state::ServiceDiscovery> primed{callbacks, std::move(params), false, d2_no_seed};
        primed.handle_response(service_discovery_response(true, false, false));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned());

        THEN("Contract is still selected") {
            const auto request = primed.take_requests().get<message_2::PaymentServiceSelectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE(request->selected_payment_option == dt::PaymentOption::Contract);
        }
    }
}
