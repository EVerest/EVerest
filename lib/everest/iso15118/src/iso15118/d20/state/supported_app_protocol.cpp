// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/supported_app_protocol.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>

#include <iso15118/d20/state/session_setup.hpp>

#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

constexpr auto ISO20_DC_NAMESPACE = "urn:iso:std:iso:15118:-20:DC";
constexpr auto ISO20_AC_NAMESPACE = "urn:iso:std:iso:15118:-20:AC";
constexpr auto ISO20_DER_IEC_NAMESPACE = "urn:iso:std:iso:15118:-20:AC-DER-IEC";
constexpr auto ISO20_DER_SAE_NAMESPACE = "urn:iso:std:iso:15118:-20:AC-DER-SAE";

constexpr std::array AcNamespaces = {ISO20_AC_NAMESPACE, ISO20_DER_IEC_NAMESPACE, ISO20_DER_SAE_NAMESPACE};

using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

struct SupportedEnergyModes {
    bool ac{false};
    bool dc{false};
    bool acdp{false};
    bool wpt{false};
};

namespace {

message_20::SupportedAppProtocolResponse handle_request(const message_20::SupportedAppProtocolRequest& req,
                                                        const SupportedEnergyModes& modes,
                                                        const std::optional<std::string>& custom_protocol_namespace) {
    message_20::SupportedAppProtocolResponse res;

    std::map<uint8_t, uint8_t> ev_supported_protocols{}; // key: priority, value: schema_id

    for (const auto& protocol : req.app_protocol) {
        const auto is_dc = protocol.protocol_namespace == ISO20_DC_NAMESPACE and modes.dc;
        const auto is_ac =
            (std::find(AcNamespaces.begin(), AcNamespaces.end(), protocol.protocol_namespace) != AcNamespaces.end()) and
            modes.ac;
        const auto is_custom = custom_protocol_namespace.has_value()
                                   ? protocol.protocol_namespace == custom_protocol_namespace.value()
                                   : false;

        if (is_dc or is_ac or is_custom) {
            ev_supported_protocols[protocol.priority] = protocol.schema_id;
        }
    }

    if (ev_supported_protocols.empty()) {
        return response_with_code(res, ResponseCode::Failed_NoNegotiation);
    }

    res.schema_id = ev_supported_protocols.begin()->second; // [V2G20-167] Highest Prio: 1, Lowest Prio: 20
    return response_with_code(res, ResponseCode::OK_SuccessfulNegotiation);
}

} // namespace

void SupportedAppProtocol::enter() {
    m_ctx.log.enter_state("SupportedAppProtocol");
}

Result SupportedAppProtocol::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::SupportedAppProtocolRequest>()) {

        auto energy_modes = SupportedEnergyModes{false, false, false, false};
        const auto supported_energy_services = m_ctx.session_config.supported_energy_transfer_services;

        if (m_ctx.session_config.selecting_sap_based_on_energy_service) {
            logf_info("Selecting supported app protocol namespace based on the supported energy services");
            for (const auto& service : supported_energy_services) {
                if (service == dt::ServiceCategory::AC or service == dt::ServiceCategory::AC_BPT or
                    service == dt::ServiceCategory::AC_DER_IEC or service == dt::ServiceCategory::AC_DER_SAE) {
                    energy_modes.ac = true;
                } else if (service == dt::ServiceCategory::DC or service == dt::ServiceCategory::DC_BPT or
                           service == dt::ServiceCategory::MCS or service == dt::ServiceCategory::MCS_BPT) {
                    energy_modes.dc = true;
                } else if (service == dt::ServiceCategory::DC_ACDP or service == dt::ServiceCategory::DC_ACDP_BPT) {
                    energy_modes.acdp = true;
                } else if (service == dt::ServiceCategory::WPT) {
                    energy_modes.wpt = true;
                }
            }
        } else {
            energy_modes = SupportedEnergyModes{true, true, true, true};
        }

        const auto res = handle_request(*req, energy_modes, m_ctx.session_config.custom_protocol);
        m_ctx.respond(res);

        m_ctx.ev_info.ev_supported_app_protocols = req->app_protocol;

        if (res.response_code == ResponseCode::Failed_NoNegotiation) {
            std::string ev_supported_namespaces{};
            for (const auto& protocol : req->app_protocol) {
                ev_supported_namespaces.append(protocol.protocol_namespace + ";");
            }
            logf_error("Selecting a protocol namespace failed. Ev offered: %s", ev_supported_namespaces.c_str());
            return {};
        }

        for (const auto& protocol : req->app_protocol) {
            if (protocol.schema_id == res.schema_id) {
                m_ctx.ev_info.selected_app_protocol = protocol;

                if (protocol.protocol_namespace == ISO20_DC_NAMESPACE) {
                    m_ctx.feedback.selected_protocol("ISO15118-20:DC");
                } else if (std::find(AcNamespaces.begin(), AcNamespaces.end(), protocol.protocol_namespace) !=
                           AcNamespaces.end()) {
                    m_ctx.feedback.selected_protocol("ISO15118-20:AC and similar");
                } else if (m_ctx.session_config.custom_protocol.has_value() and
                           m_ctx.session_config.custom_protocol.value() == protocol.protocol_namespace) {
                    m_ctx.feedback.selected_protocol(m_ctx.session_config.custom_protocol.value());
                    logf_warning(
                        "EV and EVSE have agreed on a custom protocol namespace. Problems or aborts can occur in the "
                        "following states!");
                }
            }
        }
        return m_ctx.create_state<SessionSetup>();
    }
    m_ctx.log("expected SupportedAppProtocolReq! But code type id: %d", variant->get_type());

    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::state
