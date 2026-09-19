// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "../test_pki.hpp"
#include "helper.hpp"

#include <iso15118/d20/state/authorization.hpp>
#include <iso15118/d20/state/authorization_setup.hpp>
#include <iso15118/detail/base64.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/certificate_installation.hpp>

using namespace iso15118;
using namespace iso15118::test_pki;

namespace {

struct Recorded {
    std::vector<session::feedback::Signal> signals;
    std::optional<std::pair<std::string, std::string>> require_auth_pnc;
    std::vector<session::feedback::CertificateRequest> certificate_requests;
};

session::feedback::Callbacks make_callbacks(Recorded& recorded) {
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&recorded](session::feedback::Signal signal) { recorded.signals.push_back(signal); };
    callbacks.require_auth_pnc = [&recorded](const std::string& emaid, const std::string& chain) {
        recorded.require_auth_pnc = std::make_pair(emaid, chain);
    };
    callbacks.certificate_request = [&recorded](const session::feedback::CertificateRequest& request) {
        recorded.certificate_requests.push_back(request);
    };
    return callbacks;
}

bool eim_requested(const Recorded& recorded) {
    return std::find(recorded.signals.begin(), recorded.signals.end(), session::feedback::Signal::REQUIRE_AUTH_EIM) !=
           recorded.signals.end();
}

session::EvseSetupConfig pnc_setup(const std::string& mo_root_path, bool central_validation = false,
                                   bool cert_install = true) {
    auto setup = create_default_evse_setup();
    setup.authorization_services = {dt::Authorization::EIM, dt::Authorization::PnC};
    setup.enable_certificate_install_service = cert_install;
    setup.contract_mo_root_path = mo_root_path;
    setup.central_contract_validation_allowed = central_validation;
    return setup;
}

// Runs AuthorizationSetupReq/Res and hands back the issued challenge.
dt::GenChallenge run_authorization_setup(FsmStateHelper& helper, d20::Context& ctx, fsm::v2::FSM<d20::StateBase>& fsm) {
    message_20::AuthorizationSetupRequest req;
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411798;
    helper.handle_request(req);
    const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);
    REQUIRE(result.transitioned());
    REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);
    const auto res = ctx.get_response<message_20::AuthorizationSetupResponse>();
    REQUIRE(res.has_value());
    REQUIRE(res->response_code == dt::ResponseCode::OK);
    REQUIRE(std::holds_alternative<dt::PnC_ASResAuthorizationMode>(res->authorization_mode));
    return std::get<dt::PnC_ASResAuthorizationMode>(res->authorization_mode).gen_challenge;
}

std::vector<uint8_t> signed_authorization_req(const Pki& pki, const d20::Context& ctx,
                                              const dt::GenChallenge& challenge) {
    message_20::AuthorizationRequest req;
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411799;
    req.selected_authorization_service = dt::Authorization::PnC;
    auto& pnc = req.authorization_mode.emplace<dt::PnC_ASReqAuthorizationMode>();
    pnc.id = "id1";
    pnc.gen_challenge = challenge;
    pnc.contract_certificate_chain.certificate = pki.leaf_der();
    for (const auto& sub : pki.subs_der()) {
        pnc.contract_certificate_chain.sub_certificates.emplace_back(sub);
    }
    std::vector<uint8_t> buffer(16384);
    io::StreamOutputView out({buffer.data(), buffer.size()});
    buffer.resize(message_20::serialize(req, out));
    return crypto::sign_document(buffer, crypto::SignedElement::PnC_AReqAuthorizationMode, "id1",
                                 pki.leaf_private_key());
}

std::vector<uint8_t> signed_certificate_installation_req(const Pki& oem, const d20::Context& ctx,
                                                         const crypto::PrivateKey& key) {
    message_20::CertificateInstallationRequest req;
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411799;
    req.oem_provisioning_certificate_chain.id = "oem1";
    req.oem_provisioning_certificate_chain.certificate = oem.leaf_der();
    for (const auto& sub : oem.subs_der()) {
        req.oem_provisioning_certificate_chain.sub_certificates.emplace_back(sub);
    }
    req.list_of_root_certificate_ids.root_certificate_id.emplace_back(dt::X509IssuerSerial{"CN=V2G Root", 1});
    req.maximum_contract_certificate_chains = 2;
    std::vector<uint8_t> buffer(16384);
    io::StreamOutputView out({buffer.data(), buffer.size()});
    buffer.resize(message_20::serialize(req, out));
    return crypto::sign_document(buffer, crypto::SignedElement::CertificateInstallationReq, "oem1", key);
}

message_20::AuthorizationResponse feed_authorization(FsmStateHelper& helper, d20::Context& ctx,
                                                     fsm::v2::FSM<d20::StateBase>& fsm,
                                                     const std::vector<uint8_t>& exi) {
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, exi);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    const auto res = ctx.get_response<message_20::AuthorizationResponse>();
    REQUIRE(res.has_value());
    return *res;
}

std::vector<uint8_t> backend_certificate_installation_res(const Pki& contract, const d20::Context& ctx,
                                                          uint8_t remaining) {
    message_20::CertificateInstallationResponse res;
    res.header.session_id = ctx.session.get_id();
    res.header.timestamp = 1691411800;
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = dt::Processing::Finished;
    res.cps_certificate_chain.certificate = contract.leaf_der();
    res.signed_installation_data.id = "sid";
    res.signed_installation_data.contract_certificate_chain.certificate = contract.leaf_der();
    for (const auto& sub : contract.subs_der()) {
        res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back(sub);
    }
    res.signed_installation_data.dh_public_key = std::vector<uint8_t>(133, 0x04);
    res.signed_installation_data.secp521_encrypted_private_key = std::vector<uint8_t>(94, 0xab);
    res.remaining_contract_certificate_chains = remaining;
    std::vector<uint8_t> buffer(16384);
    io::StreamOutputView out({buffer.data(), buffer.size()});
    buffer.resize(message_20::serialize(res, out));
    return buffer;
}

} // namespace

SCENARIO("ISO 15118-20 Plug and Charge authorization") {

    const auto pki = make_pki("secp521r1");
    Recorded recorded;
    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    auto callbacks = make_callbacks(recorded);

    GIVEN("A contract certificate the SECC can validate locally") {
        auto helper = FsmStateHelper(session::SessionConfig(pnc_setup(pki.root_bundle_path)), pause_ctx, callbacks);
        auto& ctx = helper.get_context();
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        const auto challenge = run_authorization_setup(helper, ctx, fsm);
        REQUIRE_FALSE(eim_requested(recorded));
        REQUIRE(ctx.session.offered_services.cert_install_service);

        const auto signed_req = signed_authorization_req(pki, ctx, challenge);
        REQUIRE_FALSE(signed_req.empty());

        WHEN("The EV sends the signed AuthorizationReq") {
            auto res = feed_authorization(helper, ctx, fsm, signed_req);

            THEN("The chain goes to the backend and the response is Ongoing") {
                REQUIRE(res.response_code == dt::ResponseCode::OK);
                REQUIRE(res.evse_processing == dt::Processing::Ongoing);
                REQUIRE(recorded.require_auth_pnc.has_value());
                REQUIRE(recorded.require_auth_pnc->first == "DEABCC123ABC56X");
                REQUIRE(recorded.require_auth_pnc->second.find("BEGIN CERTIFICATE") != std::string::npos);
                REQUIRE_FALSE(eim_requested(recorded));
                REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

                AND_THEN("A repeated request stays Ongoing without a second forward") {
                    recorded.require_auth_pnc.reset();
                    res = feed_authorization(helper, ctx, fsm, signed_req);
                    REQUIRE(res.response_code == dt::ResponseCode::OK);
                    REQUIRE(res.evse_processing == dt::Processing::Ongoing);
                    REQUIRE_FALSE(recorded.require_auth_pnc.has_value());
                }

                AND_THEN("The backend acceptance finishes the authorization") {
                    helper.set_active_control_event(d20::AuthorizationResponse{true});
                    fsm.feed(d20::Event::CONTROL_MESSAGE);
                    res = feed_authorization(helper, ctx, fsm, signed_req);
                    REQUIRE(res.response_code == dt::ResponseCode::OK);
                    REQUIRE(res.evse_processing == dt::Processing::Finished);
                    REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);
                    REQUIRE(ctx.session.authorization.authorized);
                    REQUIRE(ctx.session.authorization.authorized_via == dt::Authorization::PnC);
                    REQUIRE(ctx.session.authorization.emaid == "DEABCC123ABC56X");
                    REQUIRE(ctx.session.authorization.contract_leaf_der == pki.leaf_der());

                    // [V2G20-1844]: the authorization survives a pause.
                    d20::PauseContext pause{};
                    pause.authorization = ctx.session.authorization;
                    const d20::Session resumed(pause);
                    REQUIRE(resumed.authorization.authorized);
                    REQUIRE(resumed.authorization.emaid == "DEABCC123ABC56X");
                }

                AND_THEN("A backend rejection with an expired certificate yields the WARNING and the EV may fall "
                         "back to EIM") {
                    helper.set_active_control_event(
                        d20::AuthorizationResponse{false, d20::CertificateStatus::CertificateExpired});
                    fsm.feed(d20::Event::CONTROL_MESSAGE);
                    res = feed_authorization(helper, ctx, fsm, signed_req);
                    REQUIRE(res.response_code == dt::ResponseCode::WARNING_CertificateExpired);
                    REQUIRE(res.evse_processing == dt::Processing::Finished);
                    REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);
                    REQUIRE_FALSE(ctx.session_stopped);

                    message_20::AuthorizationRequest eim;
                    eim.header.session_id = ctx.session.get_id();
                    eim.header.timestamp = 1691411801;
                    eim.selected_authorization_service = dt::Authorization::EIM;
                    eim.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();
                    helper.handle_request(eim);
                    fsm.feed(d20::Event::V2GTP_MESSAGE);
                    auto eim_res = ctx.get_response<message_20::AuthorizationResponse>();
                    REQUIRE(eim_res.has_value());
                    REQUIRE(eim_res->response_code == dt::ResponseCode::OK);
                    REQUIRE(eim_res->evse_processing == dt::Processing::Ongoing);
                    REQUIRE(eim_requested(recorded));

                    helper.set_active_control_event(d20::AuthorizationResponse{true});
                    fsm.feed(d20::Event::CONTROL_MESSAGE);
                    helper.handle_request(eim);
                    fsm.feed(d20::Event::V2GTP_MESSAGE);
                    eim_res = ctx.get_response<message_20::AuthorizationResponse>();
                    REQUIRE(eim_res->response_code == dt::ResponseCode::OK);
                    REQUIRE(eim_res->evse_processing == dt::Processing::Finished);
                    REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);
                    REQUIRE(ctx.session.authorization.authorized_via == dt::Authorization::EIM);
                }

                AND_THEN("An unknown token maps to WARNING_eMSPUnknown") {
                    helper.set_active_control_event(
                        d20::AuthorizationResponse{false, d20::CertificateStatus::Accepted, true});
                    fsm.feed(d20::Event::CONTROL_MESSAGE);
                    res = feed_authorization(helper, ctx, fsm, signed_req);
                    REQUIRE(res.response_code == dt::ResponseCode::WARNING_eMSPUnknown);
                    REQUIRE(res.evse_processing == dt::Processing::Finished);
                }

                AND_THEN("The ongoing timeout fails the next request") {
                    ctx.set_active_timeout(d20::TimeoutType::ONGOING);
                    fsm.feed(d20::Event::TIMEOUT);
                    res = feed_authorization(helper, ctx, fsm, signed_req);
                    REQUIRE(res.response_code == dt::ResponseCode::FAILED);
                    REQUIRE(ctx.session_stopped);
                }
            }
        }

        WHEN("The EV answers with a challenge the SECC never issued") {
            dt::GenChallenge wrong = challenge;
            wrong[0] ^= 0xff;
            const auto res = feed_authorization(helper, ctx, fsm, signed_authorization_req(pki, ctx, wrong));

            THEN("WARNING_ChallengeInvalid, Finished, nothing forwarded") {
                REQUIRE(res.response_code == dt::ResponseCode::WARNING_ChallengeInvalid);
                REQUIRE(res.evse_processing == dt::Processing::Finished);
                REQUIRE_FALSE(recorded.require_auth_pnc.has_value());
                REQUIRE_FALSE(ctx.session_stopped);
            }
        }

        WHEN("The request is signed with another key") {
            const auto other = make_pki("secp521r1");
            message_20::AuthorizationRequest req;
            req.header.session_id = ctx.session.get_id();
            req.header.timestamp = 1691411799;
            req.selected_authorization_service = dt::Authorization::PnC;
            auto& pnc = req.authorization_mode.emplace<dt::PnC_ASReqAuthorizationMode>();
            pnc.id = "id1";
            pnc.gen_challenge = challenge;
            pnc.contract_certificate_chain.certificate = pki.leaf_der();
            pnc.contract_certificate_chain.sub_certificates.emplace_back(pki.subs_der()[0]);
            std::vector<uint8_t> buffer(16384);
            io::StreamOutputView out({buffer.data(), buffer.size()});
            buffer.resize(message_20::serialize(req, out));
            const auto forged = crypto::sign_document(buffer, crypto::SignedElement::PnC_AReqAuthorizationMode, "id1",
                                                      other.leaf_private_key());
            const auto res = feed_authorization(helper, ctx, fsm, forged);

            THEN("FAILED_SignatureError ends the session") {
                REQUIRE(res.response_code == dt::ResponseCode::FAILED_SignatureError);
                REQUIRE(ctx.session_stopped);
                REQUIRE_FALSE(recorded.require_auth_pnc.has_value());
            }
        }

        WHEN("The EV selects a service that was not offered") {
            auto setup = pnc_setup(pki.root_bundle_path);
            setup.authorization_services = {dt::Authorization::PnC};
            auto pnc_only = FsmStateHelper(session::SessionConfig(setup), pause_ctx, callbacks);
            auto& pnc_ctx = pnc_only.get_context();
            fsm::v2::FSM<d20::StateBase> pnc_fsm{pnc_ctx.create_state<d20::state::AuthorizationSetup>()};
            run_authorization_setup(pnc_only, pnc_ctx, pnc_fsm);

            message_20::AuthorizationRequest eim;
            eim.header.session_id = pnc_ctx.session.get_id();
            eim.header.timestamp = 1691411801;
            eim.selected_authorization_service = dt::Authorization::EIM;
            eim.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();
            pnc_only.handle_request(eim);
            pnc_fsm.feed(d20::Event::V2GTP_MESSAGE);
            const auto res = pnc_ctx.get_response<message_20::AuthorizationResponse>();

            THEN("WARNING_AuthorizationSelectionInvalid and no EIM request") {
                REQUIRE(res->response_code == dt::ResponseCode::WARNING_AuthorizationSelectionInvalid);
                REQUIRE(res->evse_processing == dt::Processing::Finished);
                REQUIRE_FALSE(eim_requested(recorded));
            }
        }
    }

    GIVEN("A contract leaf that expires within 14 days") {
        CertSpec leaf{"DE-ABC-C123ABC56-X"};
        leaf.not_after_offset_s = 10L * 24 * 3600;
        const auto soon = make_pki("secp521r1", leaf);
        auto helper = FsmStateHelper(session::SessionConfig(pnc_setup(soon.root_bundle_path)), pause_ctx, callbacks);
        auto& ctx = helper.get_context();
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
        const auto challenge = run_authorization_setup(helper, ctx, fsm);
        const auto signed_req = signed_authorization_req(soon, ctx, challenge);

        feed_authorization(helper, ctx, fsm, signed_req);
        helper.set_active_control_event(d20::AuthorizationResponse{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        const auto res = feed_authorization(helper, ctx, fsm, signed_req);

        THEN("OK_CertificateExpiresSoon") {
            REQUIRE(res.response_code == dt::ResponseCode::OK_CertificateExpiresSoon);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);
        }
    }

    GIVEN("A contract chain from an eMSP root the SECC does not have") {
        const auto other_root = make_pki("secp521r1");

        WHEN("Central contract validation is not allowed") {
            auto helper = FsmStateHelper(session::SessionConfig(pnc_setup(other_root.root_bundle_path, false)),
                                         pause_ctx, callbacks);
            auto& ctx = helper.get_context();
            fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
            const auto challenge = run_authorization_setup(helper, ctx, fsm);
            const auto res = feed_authorization(helper, ctx, fsm, signed_authorization_req(pki, ctx, challenge));

            THEN("WARNING_eMSPUnknown, nothing forwarded") {
                REQUIRE(res.response_code == dt::ResponseCode::WARNING_eMSPUnknown);
                REQUIRE(res.evse_processing == dt::Processing::Finished);
                REQUIRE_FALSE(recorded.require_auth_pnc.has_value());
            }
        }

        WHEN("Central contract validation is allowed") {
            auto helper = FsmStateHelper(session::SessionConfig(pnc_setup(other_root.root_bundle_path, true)),
                                         pause_ctx, callbacks);
            auto& ctx = helper.get_context();
            fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
            const auto challenge = run_authorization_setup(helper, ctx, fsm);
            const auto res = feed_authorization(helper, ctx, fsm, signed_authorization_req(pki, ctx, challenge));

            THEN("The chain is forwarded to the backend") {
                REQUIRE(res.response_code == dt::ResponseCode::OK);
                REQUIRE(res.evse_processing == dt::Processing::Ongoing);
                REQUIRE(recorded.require_auth_pnc.has_value());
            }
        }
    }

    GIVEN("A prime256v1 contract chain") {
        const auto p256 = make_pki("prime256v1");
        auto helper = FsmStateHelper(session::SessionConfig(pnc_setup(p256.root_bundle_path)), pause_ctx, callbacks);
        auto& ctx = helper.get_context();
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
        const auto challenge = run_authorization_setup(helper, ctx, fsm);
        const auto res = feed_authorization(helper, ctx, fsm, signed_authorization_req(p256, ctx, challenge));

        THEN("The Annex B profile check rejects it after the signature verified") {
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_CertificateValidationError);
            REQUIRE(res.evse_processing == dt::Processing::Finished);
            REQUIRE_FALSE(ctx.session_stopped);
            REQUIRE_FALSE(recorded.require_auth_pnc.has_value());
        }
    }
}

SCENARIO("ISO 15118-20 certificate installation relay") {

    const auto contract = make_pki("secp521r1");
    const auto oem = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"}, {"OEM Sub-CA1"}, {"OEM Sub-CA2"});
    Recorded recorded;
    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    auto callbacks = make_callbacks(recorded);

    GIVEN("The certificate installation service was offered") {
        auto helper =
            FsmStateHelper(session::SessionConfig(pnc_setup(contract.root_bundle_path)), pause_ctx, callbacks);
        auto& ctx = helper.get_context();
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
        const auto challenge = run_authorization_setup(helper, ctx, fsm);
        const auto req = signed_certificate_installation_req(oem, ctx, oem.leaf_private_key());
        REQUIRE_FALSE(req.empty());

        const auto feed = [&](const std::vector<uint8_t>& exi) {
            helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, exi);
            fsm.feed(d20::Event::V2GTP_MESSAGE);
            return ctx.get_response<message_20::CertificateInstallationResponse>();
        };

        WHEN("The EV sends a signed CertificateInstallationReq") {
            auto res = feed(req);

            THEN("It is forwarded once and answered Ongoing") {
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->evse_processing == dt::Processing::Ongoing);
                REQUIRE(recorded.certificate_requests.size() == 1);
                REQUIRE(recorded.certificate_requests[0].protocol == ProtocolId::ISO15118_20);
                REQUIRE(recorded.certificate_requests[0].action ==
                        session::feedback::CertificateExchangeAction::Install);
                REQUIRE(base64_decode(recorded.certificate_requests[0].exi_request_base64) == req);

                res = feed(req);
                REQUIRE(res->evse_processing == dt::Processing::Ongoing);
                REQUIRE(recorded.certificate_requests.size() == 1);

                AND_THEN("The backend response is spliced verbatim and a second chain may follow") {
                    const auto backend = backend_certificate_installation_res(contract, ctx, 1);
                    d20::CertificateResponse response;
                    response.status_accepted = true;
                    response.exi_response_base64 = base64_encode(backend);
                    helper.set_active_control_event(response);
                    fsm.feed(d20::Event::CONTROL_MESSAGE);

                    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, req);
                    fsm.feed(d20::Event::V2GTP_MESSAGE);
                    const auto [available, size, payload_type, type] =
                        helper.get_message_exchange().check_and_clear_response();
                    REQUIRE(available);
                    REQUIRE(size == backend.size());
                    REQUIRE(payload_type == io::v2gtp::PayloadType::Part20Main);
                    REQUIRE(type == message_20::Type::CertificateInstallationRes);
                    REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

                    // Remaining == 1: another request is forwarded to the backend.
                    res = feed(req);
                    REQUIRE(res->evse_processing == dt::Processing::Ongoing);
                    REQUIRE(recorded.certificate_requests.size() == 2);

                    const auto last = backend_certificate_installation_res(contract, ctx, 0);
                    response.exi_response_base64 = base64_encode(last);
                    helper.set_active_control_event(response);
                    fsm.feed(d20::Event::CONTROL_MESSAGE);
                    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, req);
                    fsm.feed(d20::Event::V2GTP_MESSAGE);
                    helper.get_message_exchange().check_and_clear_response();

                    // Remaining == 0: only AuthorizationReq is allowed now [V2G20-1975].
                    feed(req);
                    REQUIRE(ctx.session_stopped);
                }

                AND_THEN("A backend failure yields WARNING_NoCertificateAvailable") {
                    d20::CertificateResponse response;
                    response.status_accepted = false;
                    helper.set_active_control_event(response);
                    fsm.feed(d20::Event::CONTROL_MESSAGE);
                    res = feed(req);
                    REQUIRE(res->response_code == dt::ResponseCode::WARNING_NoCertificateAvailable);
                    REQUIRE(res->evse_processing == dt::Processing::Finished);
                    REQUIRE_FALSE(ctx.session_stopped);

                    // The EV continues with the authorization.
                    const auto auth =
                        feed_authorization(helper, ctx, fsm, signed_authorization_req(contract, ctx, challenge));
                    REQUIRE(auth.response_code == dt::ResponseCode::OK);
                    REQUIRE(auth.evse_processing == dt::Processing::Ongoing);
                }

                AND_THEN("A backend timeout yields WARNING_NoCertificateAvailable") {
                    ctx.set_active_timeout(d20::TimeoutType::ONGOING);
                    fsm.feed(d20::Event::TIMEOUT);
                    res = feed(req);
                    REQUIRE(res->response_code == dt::ResponseCode::WARNING_NoCertificateAvailable);
                    REQUIRE(res->evse_processing == dt::Processing::Finished);
                    REQUIRE_FALSE(ctx.session_stopped);
                }
            }
        }

        WHEN("The request is signed with the contract key instead of the OEM provisioning key") {
            const auto res = feed(signed_certificate_installation_req(oem, ctx, contract.leaf_private_key()));

            THEN("FAILED_SignatureError ends the session") {
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SignatureError);
                REQUIRE(ctx.session_stopped);
                REQUIRE(recorded.certificate_requests.empty());
            }
        }

        WHEN("The OEM provisioning leaf is expired") {
            CertSpec leaf{"DE8PAA00003C4D58Y2"};
            leaf.not_before_offset_s = -2L * 365 * 24 * 3600;
            leaf.not_after_offset_s = -24L * 3600;
            const auto expired = make_pki("secp521r1", leaf, {"OEM Sub-CA1"}, {"OEM Sub-CA2"});
            const auto res = feed(signed_certificate_installation_req(expired, ctx, expired.leaf_private_key()));

            // [V2G20-1548] NOTE 2 and Annex B.7.3: the secondary actor validates the OEM
            // provisioning chain, so the SECC relays it and only checks the signature.
            THEN("It is relayed to the certificate provisioning service") {
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->evse_processing == dt::Processing::Ongoing);
                REQUIRE(recorded.certificate_requests.size() == 1);
                REQUIRE_FALSE(ctx.session_stopped);
            }
        }
    }

    GIVEN("The certificate installation service was not offered") {
        auto helper = FsmStateHelper(session::SessionConfig(pnc_setup(contract.root_bundle_path, false, false)),
                                     pause_ctx, callbacks);
        auto& ctx = helper.get_context();
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
        run_authorization_setup(helper, ctx, fsm);

        helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main,
                                  signed_certificate_installation_req(oem, ctx, oem.leaf_private_key()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("It is a sequence error") {
            const auto res = ctx.get_response<message_20::CertificateInstallationResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            REQUIRE(ctx.session_stopped);
        }
    }
}

TEST_CASE("ISO 15118-20: Ongoing installation response initializes encrypted key choice") {
    message_20::CertificateInstallationResponse res;
    res.header.session_id = {1, 2, 3, 4, 5, 6, 7, 8};
    res.header.timestamp = 1;
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = dt::Processing::Ongoing;
    res.signed_installation_data.id = "id1";
    res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back();
    auto doc = std::make_unique<iso20_exiDocument>();
    std::memset(doc.get(), 0xa5, sizeof(*doc));
    init_iso20_exiDocument(doc.get());
    doc->CertificateInstallationRes_isUsed = 1;
    message_20::convert(res, doc->CertificateInstallationRes);
    auto& data = doc->CertificateInstallationRes.SignedInstallationData;
    CHECK((data.SECP521_EncryptedPrivateKey_isUsed || data.X448_EncryptedPrivateKey_isUsed ||
           data.TPM_EncryptedPrivateKey_isUsed));
    std::vector<uint8_t> buffer(16384);
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), buffer.size(), 0, nullptr);
    CHECK(encode_iso20_exiDocument(&stream, doc.get()) == 0);
}

TEST_CASE("ISO 15118-20: changed contract must not consume previous backend acceptance") {
    const auto a = make_pki("secp521r1");
    const auto b = make_pki("secp521r1", {"DE-XYZ-C123ABC56-X"});
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup(a.root_bundle_path, true)), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    const auto challenge = run_authorization_setup(helper, ctx, fsm);
    const auto req_a = signed_authorization_req(a, ctx, challenge);
    const auto req_b = signed_authorization_req(b, ctx, challenge);
    REQUIRE(feed_authorization(helper, ctx, fsm, req_a).evse_processing == dt::Processing::Ongoing);
    CHECK(feed_authorization(helper, ctx, fsm, req_b).response_code == dt::ResponseCode::FAILED_SequenceError);
    CHECK(ctx.session_stopped);
    helper.set_active_control_event(d20::AuthorizationResponse{true});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    CHECK_FALSE(ctx.session.authorization.authorized);
}

TEST_CASE("ISO 15118-20: a repeated authorization still verifies its signature") {
    const auto pki = make_pki("secp521r1");
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup(pki.root_bundle_path)), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    const auto challenge = run_authorization_setup(helper, ctx, fsm);
    const auto req = signed_authorization_req(pki, ctx, challenge);
    REQUIRE(feed_authorization(helper, ctx, fsm, req).evse_processing == dt::Processing::Ongoing);
    helper.set_active_control_event(d20::AuthorizationResponse{true});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    const auto tampered = mutate_document(req, [](iso20_exiDocument& doc) {
        doc.AuthorizationReq.Header.Signature_isUsed = 0;
        doc.AuthorizationReq.PnC_AReqAuthorizationMode.ContractCertificateChain.SubCertificates.Certificate.array[0]
            .bytes[10] ^= 1;
    });
    const auto res = feed_authorization(helper, ctx, fsm, tampered);
    CHECK(res.response_code == dt::ResponseCode::FAILED_SignatureError);
    CHECK_FALSE(ctx.session.authorization.authorized);
}

TEST_CASE("ISO 15118-20: PnC warning clears timer before EIM retry") {
    const auto pki = make_pki("secp521r1");
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup(pki.root_bundle_path)), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    const auto challenge = run_authorization_setup(helper, ctx, fsm);
    const auto req = signed_authorization_req(pki, ctx, challenge);
    REQUIRE(feed_authorization(helper, ctx, fsm, req).evse_processing == dt::Processing::Ongoing);
    helper.set_active_control_event(d20::AuthorizationResponse{false});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    REQUIRE(feed_authorization(helper, ctx, fsm, req).evse_processing == dt::Processing::Finished);
    ctx.set_active_timeout(d20::TimeoutType::ONGOING);
    fsm.feed(d20::Event::TIMEOUT);
    message_20::AuthorizationRequest eim;
    eim.header.session_id = ctx.session.get_id();
    eim.header.timestamp = 1;
    eim.selected_authorization_service = dt::Authorization::EIM;
    eim.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();
    helper.handle_request(eim);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    CHECK(ctx.get_response<message_20::AuthorizationResponse>()->response_code == dt::ResponseCode::OK);
}

TEST_CASE("ISO 15118-20: expiring certificate permits certificate installation") {
    CertSpec leaf{"DE-ABC-C123ABC56-X"};
    leaf.not_after_offset_s = 7L * 24 * 3600;
    const auto pki = make_pki("secp521r1", leaf);
    const auto oem = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"});
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup(pki.root_bundle_path)), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    const auto challenge = run_authorization_setup(helper, ctx, fsm);
    const auto req = signed_authorization_req(pki, ctx, challenge);
    REQUIRE(feed_authorization(helper, ctx, fsm, req).evse_processing == dt::Processing::Ongoing);
    helper.set_active_control_event(d20::AuthorizationResponse{true});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    REQUIRE(feed_authorization(helper, ctx, fsm, req).response_code == dt::ResponseCode::OK_CertificateExpiresSoon);
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main,
                              signed_certificate_installation_req(oem, ctx, oem.leaf_private_key()));
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    CHECK_FALSE(ctx.session_stopped);
    CHECK(recorded.certificate_requests.size() == 1);
    const auto renewed = make_pki("secp521r1");
    helper.set_active_control_event(
        d20::CertificateResponse{true, base64_encode(backend_certificate_installation_res(renewed, ctx, 0))});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main,
                              signed_certificate_installation_req(oem, ctx, oem.leaf_private_key()));
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    CHECK(ctx.session.authorization.contract_leaf_der == pki.leaf_der());
    CHECK(feed_authorization(helper, ctx, fsm, req).response_code == dt::ResponseCode::OK);
    CHECK(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);
    CHECK(ctx.session.authorization.contract_leaf_der == pki.leaf_der());
}

TEST_CASE("ISO 15118-20: eMSPUnknown installation warning permits retry without priorities") {
    const auto pki = make_pki("secp521r1");
    const auto oem = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"});
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup(pki.root_bundle_path)), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    run_authorization_setup(helper, ctx, fsm);
    const auto req = signed_certificate_installation_req(oem, ctx, oem.leaf_private_key());
    const auto prioritized_unsigned = mutate_document(req, [](iso20_exiDocument& doc) {
        auto& r = doc.CertificateInstallationReq;
        r.PrioritizedEMAIDs_isUsed = 1;
        r.PrioritizedEMAIDs.EMAID.arrayLen = 1;
        const std::string id = "DE-XYZ-C123ABC56-X";
        std::memcpy(r.PrioritizedEMAIDs.EMAID.array[0].characters, id.data(), id.size());
        r.PrioritizedEMAIDs.EMAID.array[0].charactersLen = id.size();
    });
    const auto prioritized = crypto::sign_document(
        prioritized_unsigned, crypto::SignedElement::CertificateInstallationReq, "oem1", oem.leaf_private_key());
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, prioritized);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    const auto warning = mutate_document(backend_certificate_installation_res(pki, ctx, 0), [](iso20_exiDocument& doc) {
        doc.CertificateInstallationRes.ResponseCode = iso20_responseCodeType_WARNING_eMSPUnknown;
    });
    helper.set_active_control_event(d20::CertificateResponse{true, base64_encode(warning)});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, prioritized);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, req);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    CHECK_FALSE(ctx.session_stopped);
    CHECK(recorded.certificate_requests.size() == 2);
}

TEST_CASE("ISO 15118-20: large schema-sized installation response fits production buffer") {
    message_20::CertificateInstallationResponse res;
    res.header.session_id = {1, 2, 3, 4, 5, 6, 7, 8};
    res.header.timestamp = 1;
    res.response_code = dt::ResponseCode::OK;
    res.cps_certificate_chain.certificate.assign(1500, 0x30);
    res.cps_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>(1500, 0x30));
    res.cps_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>(1500, 0x30));
    res.signed_installation_data.id = "id1";
    res.signed_installation_data.contract_certificate_chain.certificate.assign(1500, 0x30);
    res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back(
        std::vector<uint8_t>(1500, 0x30));
    res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back(
        std::vector<uint8_t>(1500, 0x30));
    res.signed_installation_data.dh_public_key.assign(133, 0x04);
    res.signed_installation_data.secp521_encrypted_private_key = std::vector<uint8_t>(94, 0xab);
    std::vector<uint8_t> encoded(16384);
    encoded.resize(message_20::serialize(res, {encoded.data(), encoded.size()}));
    REQUIRE(encoded.size() > 8192);
    REQUIRE(base64_encode(encoded).size() < 17000);
    std::array<uint8_t, io::MAX_V2G_PACKET_SIZE> wire_buffer{};
    d20::MessageExchange exchange({wire_buffer.data(), wire_buffer.size()});
    exchange.set_raw_response(encoded.data(), encoded.size(), message_20::Type::CertificateInstallationRes);
    CHECK(exchange.has_response());
}

TEST_CASE("ISO 15118-20: an Ed448 signed installation request reaches the CPS") {
    const auto oem = make_pki("ED448", {"DE8PAA00003C4D58Y2"});
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup("")), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    run_authorization_setup(helper, ctx, fsm);
    const auto req = signed_certificate_installation_req(oem, ctx, oem.leaf_private_key());
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, req);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    REQUIRE(recorded.certificate_requests.size() == 1);
    CHECK(base64_decode(recorded.certificate_requests.front().exi_request_base64) == req);
    CHECK_FALSE(ctx.session_stopped);
}

TEST_CASE("ISO 15118-20: invalid certificate relay produces a Finished warning") {
    const auto oem = make_pki("secp521r1", {"DE8PAA00003C4D58Y2"});
    Recorded recorded;
    std::optional<d20::PauseContext> pause;
    FsmStateHelper helper(session::SessionConfig(pnc_setup("")), pause, make_callbacks(recorded));
    auto& ctx = helper.get_context();
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
    run_authorization_setup(helper, ctx, fsm);
    const auto req = signed_certificate_installation_req(oem, ctx, oem.leaf_private_key());
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, req);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    helper.set_active_control_event(d20::CertificateResponse{true, base64_encode({0, 1, 2, 3})});
    fsm.feed(d20::Event::CONTROL_MESSAGE);
    helper.handle_raw_request(io::v2gtp::PayloadType::Part20Main, req);
    fsm.feed(d20::Event::V2GTP_MESSAGE);
    const auto response = ctx.get_response<message_20::CertificateInstallationResponse>();
    REQUIRE(response);
    CHECK(response->response_code == dt::ResponseCode::WARNING_NoCertificateAvailable);
    CHECK(response->evse_processing == dt::Processing::Finished);
    ctx.set_active_timeout(d20::TimeoutType::ONGOING);
    fsm.feed(d20::Event::TIMEOUT);
    CHECK_FALSE(ctx.session_stopped);
}
