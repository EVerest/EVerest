// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ISO15118_chargerImpl.hpp"
#include <fmt/core.h>
#include <fmt/ranges.h>

#include "der_setup.hpp"
#include "evse_security_chain_mapper.hpp"
#include "grid_event.hpp"
#include "utils.hpp"

#include <algorithm>

#include <utils/date.hpp>

#include <everest/util/misc/container.hpp>

#include <iso15118/config.hpp>

#include <iso15118/io/logging.hpp>

namespace module {
namespace charger {

static constexpr auto WAIT_FOR_SETUP_DONE_MS{std::chrono::milliseconds(200)};

std::mutex GEL; // Global EVerest Lock

namespace dt = iso15118::message_20::datatypes;

namespace {

iso15118::config::TlsNegotiationStrategy convert_tls_negotiation_strategy(const std::string& strategy) {
    using Strategy = iso15118::config::TlsNegotiationStrategy;
    if (strategy == "ACCEPT_CLIENT_OFFER") {
        return Strategy::ACCEPT_CLIENT_OFFER;
    }
    if (strategy == "ENFORCE_TLS") {
        return Strategy::ENFORCE_TLS;
    }
    if (strategy == "ENFORCE_NO_TLS") {
        return Strategy::ENFORCE_NO_TLS;
    }
    EVLOG_AND_THROW(Everest::EverestConfigError("Invalid choice for tls_negotiation_strategy: " + strategy));
    // better safe than sorry
}

types::iso15118::DisplayParameters convert_display_parameters(const dt::DisplayParameters& in) {
    return {in.present_soc,
            in.min_soc,
            in.target_soc,
            in.max_soc,
            in.remaining_time_to_min_soc,
            in.remaining_time_to_target_soc,
            in.remaining_time_to_max_soc,
            in.charging_complete,
            convert_from_optional(in.battery_energy_capacity),
            in.inlet_hot};
}

// DIN SPEC 70121 / ISO 15118-2 DC_EVStatus (both protocols share iso15118::shared_datatypes::DcEvErrorCode).
// The wire enumerators happen to match the EVerest type one for one, but the mapping is spelled out
// rather than static_cast so an added or reordered enumerator surfaces as a -Wswitch warning here.
types::iso15118::DcEvStatus convert_dc_ev_status(const iso15118::session::feedback::DcEvStatus& in) {
    using InCode = iso15118::shared_datatypes::DcEvErrorCode;
    using OutCode = types::iso15118::DcEvErrorCode;

    const auto error_code = [](InCode code) {
        switch (code) {
        case InCode::NO_ERROR:
            return OutCode::NO_ERROR;
        case InCode::FAILED_RESSTemperatureInhibit:
            return OutCode::FAILED_RESSTemperatureInhibit;
        case InCode::FAILED_EVShiftPosition:
            return OutCode::FAILED_EVShiftPosition;
        case InCode::FAILED_ChargerConnectorLockFault:
            return OutCode::FAILED_ChargerConnectorLockFault;
        case InCode::FAILED_EVRESSMalfunction:
            return OutCode::FAILED_EVRESSMalfunction;
        case InCode::FAILED_ChargingCurrentdifferential:
            return OutCode::FAILED_ChargingCurrentdifferential;
        case InCode::FAILED_ChargingVoltageOutOfRange:
            return OutCode::FAILED_ChargingVoltageOutOfRange;
        case InCode::Reserved_A:
            return OutCode::Reserved_A;
        case InCode::Reserved_B:
            return OutCode::Reserved_B;
        case InCode::Reserved_C:
            return OutCode::Reserved_C;
        case InCode::FAILED_ChargingSystemIncompatibility:
            return OutCode::FAILED_ChargingSystemIncompatibility;
        case InCode::NoData:
            return OutCode::NoData;
        }
        return OutCode::NoData;
    }(in.error_code);

    types::iso15118::DcEvStatus out;
    out.dc_ev_ready = in.ready;
    out.dc_ev_error_code = error_code;
    out.dc_ev_ress_soc = static_cast<float>(in.ress_soc);
    out.dc_ev_cabin_conditioning = in.cabin_conditioning;
    out.dc_ev_ress_conditioning = in.ress_conditioning;
    return out;
}

// DIN SPEC 70121 / ISO 15118-2 RequestedEnergyTransferMode (both message layers use
// shared_datatypes::EnergyTransferMode). Spelled out rather than static_cast so an added or reordered
// enumerator surfaces as a -Wswitch warning here.
types::iso15118::EnergyTransferMode convert_energy_transfer_mode(iso15118::shared_datatypes::EnergyTransferMode in) {
    using In = iso15118::shared_datatypes::EnergyTransferMode;
    using Out = types::iso15118::EnergyTransferMode;
    switch (in) {
    case In::AC_single_phase_core:
        return Out::AC_single_phase_core;
    case In::AC_three_phase_core:
        return Out::AC_three_phase_core;
    case In::DC_core:
        return Out::DC_core;
    case In::DC_extended:
        return Out::DC_extended;
    case In::DC_combo_core:
        return Out::DC_combo_core;
    case In::DC_unique:
        return Out::DC_unique;
    }
    return Out::DC_extended;
}

// The EV reports remaining times and its departure time as seconds from now; the EVerest vars carry
// absolute RFC3339 UTC timestamps (EvseV2G publish_dc_ev_remaining_time / publish_departure_time).
std::string seconds_from_now_to_rfc3339(double seconds) {
    const auto offset = std::chrono::duration_cast<date::utc_clock::duration>(std::chrono::duration<double>(seconds));
    return Everest::Date::to_rfc3339(date::utc_clock::now() + offset);
}

types::iso15118::DCChargingParameters
convert_dc_charging_parameters(const iso15118::session::feedback::DcEvChargeParameters& in) {
    types::iso15118::DCChargingParameters out;
    out.ev_max_current = in.max_current;
    out.ev_max_voltage = in.max_voltage;
    out.ev_max_power = in.max_power;
    out.ev_energy_capacity = in.energy_capacity;
    out.energy_amount = in.energy_request;
    out.state_of_charge = static_cast<int32_t>(in.ress_soc);
    if (in.full_soc.has_value()) {
        out.full_soc = static_cast<int32_t>(in.full_soc.value());
    }
    if (in.bulk_soc.has_value()) {
        out.bulk_soc = static_cast<int32_t>(in.bulk_soc.value());
    }
    return out;
}

types::iso15118::ACChargingParameters
convert_ac_charging_parameters(const iso15118::session::feedback::AcEvChargeParameters& in) {
    types::iso15118::ACChargingParameters out;
    out.energy_amount = in.e_amount;
    out.ev_min_current = in.min_current;
    out.ev_max_current = in.max_current;
    out.ev_max_voltage = in.max_voltage;
    return out;
}

auto fill_mobility_needs_modes_from_config(const module::Conf& module_config) {

    std::vector<iso15118::d20::ControlMobilityNeedsModes> mobility_needs_modes{};

    if (module_config.supported_dynamic_mode) {
        mobility_needs_modes.push_back({dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc});
        if (module_config.supported_mobility_needs_mode_provided_by_secc) {
            mobility_needs_modes.push_back({dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc});
        }
    }

    if (module_config.supported_scheduled_mode) {
        mobility_needs_modes.push_back({dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc});
    }

    if (mobility_needs_modes.empty()) {
        EVLOG_warning << "Control mobility modes are empty! Setting dynamic mode as default!";
        mobility_needs_modes.push_back({dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc});
    }

    return mobility_needs_modes;
}

types::iso15118::AcEvDynamicModeValues fill_ac_ev_dynamic_control_mode(const dt::Dynamic_AC_CLReqControlMode& mode) {
    types::iso15118::AcEvDynamicModeValues values{};

    values.departure_time = mode.departure_time;
    values.target_energy_request = dt::from_RationalNumber(mode.target_energy_request);
    values.max_energy_request = dt::from_RationalNumber(mode.max_energy_request);
    values.min_energy_request = dt::from_RationalNumber(mode.min_energy_request);

    return values;
}

types::iso15118::EnergyTransferMode get_energy_transfer_mode(const dt::ServiceCategory& service_category,
                                                             const std::optional<dt::AcConnector>& ac_connector) {
    using EnergyTransferMode = types::iso15118::EnergyTransferMode;

    EnergyTransferMode requested_energy_transfer = EnergyTransferMode::AC_single_phase_core;

    if (service_category == dt::ServiceCategory::AC && ac_connector.has_value()) {
        if (ac_connector.value() == dt::AcConnector::SinglePhase) {
            requested_energy_transfer = EnergyTransferMode::AC_single_phase_core;
        } else if (ac_connector.value() == dt::AcConnector::ThreePhase) {
            requested_energy_transfer = EnergyTransferMode::AC_three_phase_core;
        }
    } else if (service_category == dt::ServiceCategory::AC_BPT) {
        requested_energy_transfer = EnergyTransferMode::AC_BPT;
    } else if (service_category == dt::ServiceCategory::DC) {
        requested_energy_transfer = EnergyTransferMode::DC;
    } else if (service_category == dt::ServiceCategory::DC_ACDP) {
        requested_energy_transfer = EnergyTransferMode::DC_ACDP;
    } else if (service_category == dt::ServiceCategory::DC_BPT) {
        requested_energy_transfer = EnergyTransferMode::DC_BPT;
    } else if (service_category == dt::ServiceCategory::DC_ACDP_BPT) {
        requested_energy_transfer = EnergyTransferMode::DC_ACDP_BPT;
    } else if (service_category == dt::ServiceCategory::MCS) {
        requested_energy_transfer = EnergyTransferMode::MCS;
    } else if (service_category == dt::ServiceCategory::MCS_BPT) {
        requested_energy_transfer = EnergyTransferMode::MCS_BPT;
    } else if (service_category == dt::ServiceCategory::AC_DER_IEC) {
        requested_energy_transfer = EnergyTransferMode::AC_DER_IEC;
    } else if (service_category == dt::ServiceCategory::AC_DER_SAE) {
        requested_energy_transfer = EnergyTransferMode::AC_DER_SAE;
    }

    return requested_energy_transfer;
}

} // namespace

void ISO15118_chargerImpl::init() {

    // setup logging routine
    iso15118::io::set_logging_callback([](const iso15118::LogLevel& level, const std::string& msg) {
        switch (level) {
        case iso15118::LogLevel::Error:
            EVLOG_error << msg;
            break;
        case iso15118::LogLevel::Warning:
            EVLOG_warning << msg;
            break;
        case iso15118::LogLevel::Info:
            EVLOG_info << msg;
            break;
        case iso15118::LogLevel::Debug:
            EVLOG_debug << msg;
            break;
        case iso15118::LogLevel::Trace:
            EVLOG_verbose << msg;
            break;
        default:
            EVLOG_critical << "(Loglevel not defined) - " << msg;
            break;
        }
    });

    supported_vas_services_per_provider.reserve(mod->r_iso15118_vas.size());

    offered_vas_per_provider.resize(mod->r_iso15118_vas.size());

    for (size_t i = 0; i < mod->r_iso15118_vas.size(); i++) {
        supported_vas_services_per_provider.emplace_back();

        this->mod->r_iso15118_vas[i]->subscribe_offered_vas(
            [this, i](const types::iso15118_vas::OfferedServices& offered_services) {
                std::vector<uint16_t> service_ids;
                std::scoped_lock lock(vas_mutex);

                service_ids.reserve(offered_services.services.size());
                for (const auto& item : offered_services.services) {
                    service_ids.push_back(item.service_id);
                }
                // ISO 15118-2 advertises name, scope and the free flag per service (the -20 list above
                // carries ids only), so keep the full offer as well.
                offered_vas_per_provider[i] = offered_services.services;

                EVLOG_verbose << fmt::format("Updated Supported VAS services for provider #{} ({} service{})", i,
                                             offered_services.services.size(),
                                             offered_services.services.size() > 1 ? "s" : "");
                supported_vas_services_per_provider[i] = service_ids;

                // report to controller if it exists, also check for duplicate service ids
                update_supported_vas_services();
            });
    }

    mod->register_der_directive_callback([this]() { apply_active_der_directives(); });
}

void ISO15118_chargerImpl::apply_active_der_directives() {
    // Hold for the whole function so two concurrent applies cannot interleave their per-name update loops.
    std::scoped_lock apply_lock(der_apply_mutex);

    const auto directives = mod->get_active_der_directives();
    if (not directives.has_value()) {
        return;
    }

    // Snapshot the GEL-protected setup_config fields under the lock, then release it before touching the
    // controller: update_*_der_functions reach into the evse_setup monitor (a second lock), and holding GEL
    // across that would risk lock inversion. controller is set once in ready() and lives for the impl lifetime,
    // so the snapshotted pointer stays valid after the lock is released.
    iso15118::TbdController* controller_ptr = nullptr;
    float volt_base = 0.0f;
    float watt_base = 0.0f;
    std::optional<float> var_base;
    {
        std::scoped_lock lock(GEL);
        controller_ptr = controller.get();
        volt_base =
            setup_config.ac_setup_config.has_value() ? static_cast<float>(setup_config.ac_setup_config->voltage) : 0.0f;
        watt_base = dt::from_RationalNumber(setup_config.ac_limits.charge_power.max);
        var_base = evse_max_reactive_power;
    }

    if (controller_ptr == nullptr) {
        EVLOG_info << "grid_support DER directives stored before HLC controller ready; will apply once the first V2G "
                      "session starts.";
        return;
    }

    const auto der_map =
        module::map_active_directives_to_der_functions(directives.value(), volt_base, watt_base, var_base);

    for (const auto name : {iso15118::iec::DERControlName::VoltVarMode, iso15118::iec::DERControlName::WattVarMode,
                            iso15118::iec::DERControlName::WattCosPhiMode}) {
        const auto it = der_map.find(name);
        if (it != der_map.end()) {
            controller_ptr->update_supported_der_functions(name, it->second);
        } else {
            controller_ptr->update_unsupported_der_functions(name);
        }
    }
}

void ISO15118_chargerImpl::ready() {
    // enforce_tls_1_3 pins the server to TLS 1.3, but ISO 15118-2 mandates TLS 1.2. With both enabled
    // every ISO 15118-2 client would silently fail the handshake while ISO 15118-2 is still advertised at
    // SAP, so refuse to start. Mirrors the Ev15118 module guard.
    if (mod->config.enforce_tls_1_3 and mod->config.supported_ISO15118_2) {
        EVLOG_error << "Evse15118D20: enforce_tls_1_3 and supported_ISO15118_2 are both set, but ISO 15118-2 "
                       "requires TLS 1.2; disable one of them. The SECC will not start";
        return;
    }

    // Priority-ordered list of protocol generations the SECC offers in the SupportedAppProtocol handshake:
    // ISO 15118-20 (highest priority), then ISO 15118-2, then DIN SPEC 70121, each enabled via config.
    if (not mod->config.supported_ISO15118_20 and not mod->config.supported_ISO15118_2 and
        not mod->config.supported_DIN70121) {
        EVLOG_error << "Evse15118D20: all of supported_ISO15118_20, supported_ISO15118_2 and supported_DIN70121 "
                       "are disabled; at least one protocol must be offered. The SECC will not start";
        return;
    }

    // Whether a TLS endpoint can be offered at all: it needs the SECC leaf certificate from the security
    // module. A missing certificate is NOT fatal on its own -- ISO 15118-2 runs EIM sessions over plain
    // TCP and DIN SPEC 70121 has no TLS at all ([V2G-DC-869]: DIN is never negotiated on a TLS
    // connection) -- so the SECC still comes up for those. Determined before the protocol offer is built
    // because ISO 15118-20 depends on it.
    auto tls_strategy = convert_tls_negotiation_strategy(mod->config.tls_negotiation_strategy);
    // Built by the same path the certificate_store_update subscriber uses for live rotation (see
    // build_current_ssl_config / map_valid_chains): every valid SECC leaf chain, each tagged with the TLS
    // version it is presented on (ISO 15118-2 secp256r1 -> TLS 1.2, ISO 15118-20 secp521r1 / Ed448 -> TLS 1.3).
    auto ssl_for_controller = build_current_ssl_config();
    const bool tls_available = not ssl_for_controller.chains.empty();

    if (not tls_available) {
        if (tls_strategy == iso15118::config::TlsNegotiationStrategy::ENFORCE_TLS) {
            EVLOG_error << "Evse15118D20: no V2G leaf certificate is available, but tls_negotiation_strategy is "
                           "ENFORCE_TLS, so every session would have to be refused. The SECC will not start";
            return;
        }
        // Answer SDP with a plain endpoint straight away instead of failing the TLS listener per request
        // (the controller's ACCEPT_CLIENT_OFFER fallback would do it one connection at a time, and an
        // EV would be told TLS is on offer when it is not). Also makes DIN negotiable, which the SECC
        // refuses on a TLS connection.
        if (tls_strategy == iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER) {
            tls_strategy = iso15118::config::TlsNegotiationStrategy::ENFORCE_NO_TLS;
        }
        EVLOG_warning << "Evse15118D20: no V2G leaf certificate is available; TLS is disabled and only unsecured "
                         "sessions are offered. Plug-and-Charge is unavailable (EIM only)";
    }

    // ISO 15118-20 mandates TLS -- [V2G20-2677]: "Only full-handshake TLS shall be used for V2G
    // communication between EVCC and SECC" -- so without a certificate it cannot be offered at all. ISO
    // 15118-2 and DIN SPEC 70121 are offered either way and simply run unsecured.
    const bool offer_iso15118_20 = mod->config.supported_ISO15118_20 and tls_available;
    iso15118_20_offerable = offer_iso15118_20;
    if (mod->config.supported_ISO15118_20 and not offer_iso15118_20) {
        EVLOG_warning << "Evse15118D20: supported_ISO15118_20 is set but no V2G leaf certificate is available; "
                         "ISO 15118-20 requires TLS [V2G20-2677] and is not offered";
    }
    if (not offer_iso15118_20 and not mod->config.supported_ISO15118_2 and not mod->config.supported_DIN70121) {
        EVLOG_error << "Evse15118D20: ISO 15118-20 is the only enabled protocol but no V2G leaf certificate is "
                       "available, and it cannot run unsecured. The SECC will not start";
        return;
    }
    // A certificate exists, so -20 stays in the offer -- but ENFORCE_NO_TLS means every connection is
    // plain TCP, and -20 is TLS-only [V2G20-2677]. The offer is deliberately left alone (an EV that only
    // speaks -20 would otherwise get Failed_NoNegotiation instead of a working, if non-conformant,
    // session), so flag the combination once at startup rather than only per session in the SAP handshake.
    if (offer_iso15118_20 and tls_strategy == iso15118::config::TlsNegotiationStrategy::ENFORCE_NO_TLS) {
        EVLOG_warning << "Evse15118D20: supported_ISO15118_20 is set together with "
                         "tls_negotiation_strategy ENFORCE_NO_TLS. ISO 15118-20 mandates TLS [V2G20-2677], so any "
                         "negotiated -20 session will not be standard-conformant. Set supported_ISO15118_20 to false "
                         "to offer only ISO 15118-2 / DIN SPEC 70121 on unsecured connections";
    }
    // Each protocol generation needs its own SECC leaf: ISO 15118-2 a secp256r1 leaf on TLS 1.2, ISO
    // 15118-20 a secp521r1 / Ed448 leaf on TLS 1.3. Without a leaf for a version the TLS server falls
    // back to whatever is installed, which works with lenient EVs but is not standard-conformant, so
    // say so once at startup.
    if (tls_available and tls_strategy != iso15118::config::TlsNegotiationStrategy::ENFORCE_NO_TLS) {
        const auto has_leaf_for = [&ssl_for_controller](iso15118::config::ChainTlsVersion version) {
            return std::any_of(ssl_for_controller.chains.begin(), ssl_for_controller.chains.end(),
                               [version](const iso15118::config::ChainConfig& chain) {
                                   return chain.tls_version == version or
                                          chain.tls_version == iso15118::config::ChainTlsVersion::ANY;
                               });
        };
        if (offer_iso15118_20 and not has_leaf_for(iso15118::config::ChainTlsVersion::TLS_1_3)) {
            EVLOG_warning << "Evse15118D20: ISO 15118-20 is offered but no secp521r1 / Ed448 SECC leaf certificate is "
                             "installed; TLS 1.3 sessions will be presented the ISO 15118-2 (secp256r1) leaf, which "
                             "ISO 15118-20 EVs may reject";
        }
        if (mod->config.supported_ISO15118_2 and not has_leaf_for(iso15118::config::ChainTlsVersion::TLS_1_2)) {
            EVLOG_warning << "Evse15118D20: ISO 15118-2 is offered but no secp256r1 SECC leaf certificate is "
                             "installed; TLS 1.2 sessions will be presented the ISO 15118-20 leaf, which ISO "
                             "15118-2 EVs may reject";
        }
    }

    std::vector<iso15118::ProtocolId> supported_protocols;
    types::iso15118::SupportedAppProtocols secc_app_protocols;

    if (offer_iso15118_20) {
        supported_protocols.push_back(iso15118::ProtocolId::ISO15118_20);
        secc_app_protocols.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118D20);
    }
    if (mod->config.supported_ISO15118_2) {
        supported_protocols.push_back(iso15118::ProtocolId::ISO15118_2);
        secc_app_protocols.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118D2);
    }
    if (mod->config.supported_DIN70121) {
        supported_protocols.push_back(iso15118::ProtocolId::DIN70121);
        secc_app_protocols.app_protocols.push_back(types::iso15118::SupportedAppProtocol::DIN70121);
    }

    {
        std::scoped_lock lock(GEL);
        configured_protocol_offer = supported_protocols;
        apply_supported_protocols();
    }

    publish_supported_app_protocols_secc(secc_app_protocols);

    while (true) {
        if (setup_steps_done.all_set()) {
            break;
        }
        std::this_thread::sleep_for(WAIT_FOR_SETUP_DONE_MS);
    }

    if (mod->config.logging_path != ".") {
        EVLOG_warning << "The config option `logging_path` should no longer be used and will be removed in the future. "
                         "Instead use the PacketSniffer module, tpcdump or Wireshark to log the v2gtp messages";
    }

    const auto v2g_root_cert_path = mod->r_security->call_get_verify_file(types::evse_security::CaCertificateType::V2G);
    const auto mo_root_cert_path = mod->r_security->call_get_verify_file(types::evse_security::CaCertificateType::MO);

    // Contract certificate chain roots for the ISO 15118-2 Plug-and-Charge PaymentDetails validation.
    setup_config.contract_mo_root_path = mo_root_cert_path;
    setup_config.contract_v2g_root_path = v2g_root_cert_path;

    // TODO(mlitre): Should be updated once libiso supports service renegotiation
    this->mod->p_extensions->publish_service_renegotiation_supported(false);

    // ssl_for_controller was built above; without a leaf certificate it carries no chain and the controller
    // only ever brings up plain endpoints (tls_strategy was forced to ENFORCE_NO_TLS above; ENFORCE_TLS
    // already refused to start). The roots in it still gate contract-certificate validation.

    iso15118::TbdConfig tbd_config = {
        std::move(ssl_for_controller),
        mod->config.device,
        tls_strategy,
        mod->config.enable_sdp_server,
    };
    auto callbacks = create_callbacks();

    setup_config.control_mobility_modes = fill_mobility_needs_modes_from_config(mod->config);

    if (not mod->config.custom_protocol_namespace.empty()) {
        setup_config.custom_protocol.emplace(mod->config.custom_protocol_namespace);
    }

    setup_config.selecting_sap_based_on_energy_service = mod->config.selecting_sap_based_on_energy_service;

    // How long the ISO 15118-2 / DIN SPEC 70121 SECC keeps answering EVSEProcessing=Ongoing while waiting
    // for the authorization result; 0 waits indefinitely. The manifest constrains both to >= 0, but a
    // config source that bypasses validation must not wrap the unsigned field, so clamp.
    const auto auth_timeout_seconds = [](int configured, const char* name) -> uint32_t {
        if (configured < 0) {
            EVLOG_warning << fmt::format("Evse15118D20: {} is negative ({}); treating it as 0 (wait indefinitely)",
                                         name, configured);
            return 0;
        }
        return static_cast<uint32_t>(configured);
    };
    setup_config.auth_timeout_eim_s = auth_timeout_seconds(mod->config.auth_timeout_eim, "auth_timeout_eim");
    setup_config.auth_timeout_pnc_s = auth_timeout_seconds(mod->config.auth_timeout_pnc, "auth_timeout_pnc");

    // IEC DER limits pass through from ac_limits. Applying DER control directives to the EV is handled
    // separately by the DER control-function relay.
    {
        const auto& services = setup_config.supported_energy_services;
        if (everest::lib::util::exists(services, dt::ServiceCategory::AC_DER_IEC)) {
            setup_config.der_limits = build_iec_der_transfer_limits(setup_config.ac_limits);
        }
    }

    controller = std::make_unique<iso15118::TbdController>(std::move(tbd_config), std::move(callbacks), setup_config);

    // ISO 15118-2 Plug-and-Charge CertificateInstallation relay: forward the backend's
    // CertificateInstallationRes (received by the extensions impl) into libiso15118 as a control event.
    // Registered after the controller exists so the extensions impl (a separate command thread) can
    // safely inject the response into the running session (the control-event queue is mutex-protected).
    mod->on_certificate_response = [this](const types::iso15118::ResponseExiStreamStatus& response) {
        std::scoped_lock lock(GEL);
        if (not controller) {
            return;
        }
        iso15118::d20::CertificateResponse event;
        event.status_accepted = (response.status == types::iso15118::Status::Accepted);
        if (response.exi_response.has_value()) {
            event.exi_response_base64 = response.exi_response.value();
        }
        controller->send_control_event(event);
    };

    mod->r_security->subscribe_certificate_store_update(
        [this](const types::evse_security::CertificateStoreUpdate& event) { on_certificate_store_update(event); });

    // Close the startup window: a V2G certificate install completing between the initial config
    // build above and the subscription registration would otherwise be served only at the next
    // store update. Rebuild+apply once through the same idempotent path.
    module::charger::resync_ssl_config(
        [this] { return build_current_ssl_config(); },
        [this](iso15118::config::SSLConfig cfg) { controller->set_ssl_config(std::move(cfg)); });

    // if the vas providers report their supported vas services before the controller exists,
    // we need to update the controller with the supported vas services after instantiation
    {
        std::scoped_lock lock(vas_mutex);
        update_supported_vas_services();
    }

    apply_active_der_directives();

    try {
        controller->loop();
    } catch (const std::exception& e) {
        EVLOG_error << e.what();
    }
}

iso15118::config::SSLConfig ISO15118_chargerImpl::build_base_ssl_config() {
    iso15118::config::SSLConfig cfg{};
    cfg.backend = iso15118::config::CertificateBackend::EVEREST_LAYOUT;
    cfg.path_certificate_v2g_root = mod->r_security->call_get_verify_file(types::evse_security::CaCertificateType::V2G);
    cfg.path_certificate_mo_root = mod->r_security->call_get_verify_file(types::evse_security::CaCertificateType::MO);
    cfg.enable_ssl_logging = mod->config.enable_ssl_logging;
    cfg.enable_tls_key_logging = mod->config.enable_tls_key_logging;
    cfg.enforce_tls_1_3 = mod->config.enforce_tls_1_3;
    cfg.tls_key_logging_path = mod->config.tls_key_logging_path;
    return cfg;
}

iso15118::config::SSLConfig ISO15118_chargerImpl::build_current_ssl_config() {
    auto cfg = build_base_ssl_config();
    const auto certs_result = mod->r_security->call_get_all_valid_certificates_info(
        types::evse_security::LeafCertificateType::V2G, types::evse_security::EncodingFormat::PEM, true);
    cfg.chains = map_valid_chains(certs_result);
    return cfg;
}

void ISO15118_chargerImpl::on_certificate_store_update(const types::evse_security::CertificateStoreUpdate& event) {
    if (controller == nullptr) {
        EVLOG_warning << "certificate_store_update received before controller ready; ignoring";
        return;
    }
    module::charger::handle_certificate_store_update(
        event, [this] { return build_current_ssl_config(); },
        [this](iso15118::config::SSLConfig cfg) { controller->set_ssl_config(std::move(cfg)); });
}

void ISO15118_chargerImpl::update_supported_vas_services() {
    iso15118::d20::SupportedVASs supported_vas_services;

    for (const auto& provider_services : supported_vas_services_per_provider) {
        for (const auto& service_id : provider_services) {
            // Check for duplicate service IDs across all providers
            if (std::find(supported_vas_services.begin(), supported_vas_services.end(), service_id) !=
                supported_vas_services.end()) {
                EVLOG_error << "Duplicate VAS service ID found: " << std::to_string(service_id)
                            << ". Skipping this service.";
                continue;
            }

            supported_vas_services.push_back(service_id);
        }
    }

    // The same offers for the ISO 15118-2 ServiceList, with the per-service details the -20 view drops.
    // The library refuses the reserved ids (1 charging, 2 Certificate) and applies the -2 size limits.
    std::vector<iso15118::session::VasService> pre20_vas_services;
    for (const auto& provider_offers : offered_vas_per_provider) {
        for (const auto& offer : provider_offers) {
            const auto service_id = static_cast<uint16_t>(offer.service_id);
            if (std::find(supported_vas_services.begin(), supported_vas_services.end(), service_id) ==
                supported_vas_services.end()) {
                continue; // duplicate, already reported above
            }
            iso15118::session::VasService service;
            service.id = service_id;
            service.name = offer.service_name;
            service.scope = offer.service_scope;
            service.free_service = offer.free_service.value_or(true);
            pre20_vas_services.push_back(std::move(service));
        }
    }

    if (this->controller) {
        EVLOG_verbose << fmt::format("Updated controller VAS list: {}", fmt::join(supported_vas_services, ","));
        this->controller->update_supported_vas_services(supported_vas_services);
        this->controller->update_pre20_vas_services(pre20_vas_services);
    } else {
        EVLOG_verbose << "Controller not initialized, skipping setting supported VAS services.";
    }
}

std::optional<size_t> ISO15118_chargerImpl::get_vas_provider_index(uint16_t service_id) {
    std::scoped_lock lock(vas_mutex);

    for (size_t i = 0; i < supported_vas_services_per_provider.size(); i++) {
        const auto& provider_services = supported_vas_services_per_provider[i];
        if (everest::lib::util::exists(provider_services, service_id)) {
            return i;
        }
    }

    return std::nullopt; // Service ID not found in any provider's list
}

void ISO15118_chargerImpl::publish_grid_event(uint8_t condition) {
    const auto edge = grid_event_detector.peek(condition);

    if (edge.transition == Transition::None) {
        grid_event_detector.commit(condition);
        return;
    }

    // A grid fault must never throw out of the V2G session thread; commit only after publish succeeds.
    try {
        types::grid_support::GridAlarm alarm{};
        alarm.fault = edge.fault.value();
        alarm.alarm_ended = (edge.transition == Transition::Falling);
        alarm.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
        // TODO: set alarm.directive_type from a grid_event_condition to directive_type mapping once defined.

        if (edge.transition == Transition::Rising) {
            EVLOG_warning << "EV reported grid-event fault (condition " << static_cast<unsigned>(condition) << ")";
        } else {
            EVLOG_info << "EV grid-event fault cleared (condition " << static_cast<unsigned>(condition) << ")";
        }

        mod->p_grid_support->publish_alarm(alarm);

        // Publish succeeded: advance the fault state.
        grid_event_detector.commit(condition);
    } catch (const std::exception& e) {
        const auto direction = (edge.transition == Transition::Rising) ? "rising" : "falling";
        EVLOG_error << "grid-event alarm publish failed (condition " << static_cast<unsigned>(condition) << ", "
                    << direction << "): " << e.what();
        // Not committed: a persisting condition is re-peeked and retried next iteration.
    }
}

iso15118::session::feedback::Callbacks ISO15118_chargerImpl::create_callbacks() {

    using ScheduleControlModeDC = dt::Scheduled_DC_CLReqControlMode;
    using BPT_ScheduleReqControlModeDC = dt::BPT_Scheduled_DC_CLReqControlMode;
    using DynamicReqControlModeDC = dt::Dynamic_DC_CLReqControlMode;
    using BPT_DynamicReqControlModeDC = dt::BPT_Dynamic_DC_CLReqControlMode;

    using ScheduleControlModeAC = dt::Scheduled_AC_CLReqControlMode;
    using BPT_ScheduleReqControlModeAC = dt::BPT_Scheduled_AC_CLReqControlMode;
    using DynamicReqControlModeAC = dt::Dynamic_AC_CLReqControlMode;
    using BPT_DynamicReqControlModeAC = dt::BPT_Dynamic_AC_CLReqControlMode;

    namespace feedback = iso15118::session::feedback;

    feedback::Callbacks callbacks;

    callbacks.dc_pre_charge_target_voltage = [this](float target_voltage) {
        publish_dc_ev_target_voltage_current({target_voltage, 0});
    };

    callbacks.notify_ev_charging_needs = [this](const dt::ServiceCategory& service_category,
                                                const std::optional<dt::AcConnector>& ac_connector,
                                                const dt::ControlMode& control_mode,
                                                const dt::MobilityNeedsMode& mobility_needs_mode,
                                                const feedback::EvseTransferLimits& evse_limits,
                                                const feedback::EvTransferLimits& ev_limits,
                                                const feedback::EvSEControlMode& ev_control_mode,
                                                const std::vector<dt::ServiceCategory>& ev_energy_services) {
        types::iso15118::ChargingNeeds charging_needs;

        charging_needs.requested_energy_transfer = get_energy_transfer_mode(service_category, ac_connector);
        if (!ev_energy_services.empty()) {
            charging_needs.available_energy_transfer = std::vector<types::iso15118::EnergyTransferMode>{};
            charging_needs.available_energy_transfer->reserve(ev_energy_services.size());
            for (const auto& energy_transfer : ev_energy_services) {
                charging_needs.available_energy_transfer->emplace_back(
                    get_energy_transfer_mode(energy_transfer, std::nullopt));
            }
        }

        if (control_mode == dt::ControlMode::Scheduled) {
            charging_needs.control_mode = types::iso15118::ControlMode::ScheduledControl;
        } else if (control_mode == dt::ControlMode::Dynamic) {
            charging_needs.control_mode = types::iso15118::ControlMode::DynamicControl;
        } else {
            EVLOG_error << "Invalid value received for control mode! Not sending 'ChargingNeeds'.";
            return;
        }

        if (mobility_needs_mode == dt::MobilityNeedsMode::ProvidedByEvcc) {
            charging_needs.mobility_needs_mode = types::iso15118::MobilityNeedsMode::EVCC;
        } else if (mobility_needs_mode == dt::MobilityNeedsMode::ProvidedBySecc) {
            charging_needs.mobility_needs_mode = types::iso15118::MobilityNeedsMode::EVCC_SECC;
        } else {
            EVLOG_error << "Invalid value received for mobility needs mode! Not sending 'ChargingNeeds'.";
            return;
        }

        types::iso15118::V2XChargingParameters& v2x_charging_parameters =
            charging_needs.v2x_charging_parameters.emplace();

        if (const auto* dc_evse_limits = std::get_if<iso15118::d20::DcTransferLimits>(&evse_limits)) {
            if (const auto* dc_ev_limits = std::get_if<dt::DC_CPDReqEnergyTransferMode>(&ev_limits)) {
                fill_v2x_charging_parameters(v2x_charging_parameters, *dc_evse_limits, *dc_ev_limits);
            } else if (const auto* dc_ev_limits = std::get_if<dt::BPT_DC_CPDReqEnergyTransferMode>(&ev_limits)) {
                fill_v2x_charging_parameters(v2x_charging_parameters, *dc_evse_limits, *dc_ev_limits);
            }
        } else if (const auto* ac_evse_limits = std::get_if<iso15118::d20::AcTransferLimits>(&evse_limits)) {
            if (const auto* ac_ev_limits = std::get_if<dt::AC_CPDReqEnergyTransferMode>(&ev_limits)) {
                fill_v2x_charging_parameters(v2x_charging_parameters, *ac_evse_limits, *ac_ev_limits);
            } else if (const auto* ac_ev_limits = std::get_if<dt::BPT_AC_CPDReqEnergyTransferMode>(&ev_limits)) {
                fill_v2x_charging_parameters(v2x_charging_parameters, *ac_evse_limits, *ac_ev_limits);
            } else if (const auto* der_ac_ev_limits = std::get_if<dt::DER_AC_CPDReqEnergyTransferMode>(&ev_limits)) {
                // AC_DER_IEC: reuse the AC base-class slice for v2x params; DER limits go to der_charging_parameters.
                fill_v2x_charging_parameters(v2x_charging_parameters, *ac_evse_limits,
                                             static_cast<const dt::AC_CPDReqEnergyTransferMode&>(*der_ac_ev_limits));
                charging_needs.der_charging_parameters = to_der_charging_parameters(*der_ac_ev_limits);
                charging_needs.der_charging_parameters->ev_supported_dercontrol =
                    map_ev_supported_der_controls(ev_selected_der_control_functions);
            }
        } else {
            EVLOG_error << "Invalid type received for EVSE limits! Not sending 'ChargingNeeds'.";
            return;
        }

        if (const auto* ev_se_control_mode = std::get_if<dt::Scheduled_SEReqControlMode>(&ev_control_mode)) {
            fill_v2x_charging_parameters(v2x_charging_parameters, *ev_se_control_mode);
        } else if (const auto* ev_se_control_mode = std::get_if<dt::Dynamic_SEReqControlMode>(&ev_control_mode)) {
            fill_v2x_charging_parameters(v2x_charging_parameters, *ev_se_control_mode);
        } else {
            EVLOG_error << "Invalid type received for EV Control Mode! Not sending 'ChargingNeeds'.";
            return;
        }

        // Publish charging needs through the extensions
        this->mod->p_extensions->publish_charging_needs(charging_needs);
    };

    callbacks.dc_charge_loop_req = [this](const feedback::DcChargeLoopReq& dc_charge_loop_req) {
        if (const auto* dc_control_mode = std::get_if<feedback::DcReqControlMode>(&dc_charge_loop_req)) {
            if (const auto* scheduled_mode = std::get_if<ScheduleControlModeDC>(dc_control_mode)) {
                const auto target_voltage = dt::from_RationalNumber(scheduled_mode->target_voltage);
                const auto target_current = dt::from_RationalNumber(scheduled_mode->target_current);

                publish_dc_ev_target_voltage_current({target_voltage, target_current});

                // Each maximum is optional on its own (and DIN SPEC 70121 EVs commonly send voltage and
                // current but no power limit), so publish whichever subset the EV actually sent instead
                // of dropping all three when one is missing. All three fields of DcEvMaximumLimits are
                // optional as well.
                if (scheduled_mode->max_charge_current or scheduled_mode->max_voltage or
                    scheduled_mode->max_charge_power) {
                    publish_dc_ev_maximum_limits({convert_from_optional(scheduled_mode->max_charge_current),
                                                  convert_from_optional(scheduled_mode->max_charge_power),
                                                  convert_from_optional(scheduled_mode->max_voltage)});
                }

            } else if (const auto* bpt_scheduled_mode = std::get_if<BPT_ScheduleReqControlModeDC>(dc_control_mode)) {
                const auto target_voltage = dt::from_RationalNumber(bpt_scheduled_mode->target_voltage);
                const auto target_current = dt::from_RationalNumber(bpt_scheduled_mode->target_current);
                publish_dc_ev_target_voltage_current({target_voltage, target_current});

                if (bpt_scheduled_mode->max_charge_current or bpt_scheduled_mode->max_voltage or
                    bpt_scheduled_mode->max_charge_power) {
                    publish_dc_ev_maximum_limits({convert_from_optional(bpt_scheduled_mode->max_charge_current),
                                                  convert_from_optional(bpt_scheduled_mode->max_charge_power),
                                                  convert_from_optional(bpt_scheduled_mode->max_voltage)});
                }

                // publish_dc_ev_maximum_limits({max_limits.current, max_limits.power, max_limits.voltage});
            } else if (const auto* dynamic_mode = std::get_if<DynamicReqControlModeDC>(dc_control_mode)) {
                publish_d20_dc_dynamic_charge_mode(convert_dynamic_values(*dynamic_mode));
            } else if (const auto* bpt_dynamic_mode = std::get_if<BPT_DynamicReqControlModeDC>(dc_control_mode)) {
                publish_d20_dc_dynamic_charge_mode(convert_dynamic_values(*bpt_dynamic_mode));
            }
        } else if (const auto* display_parameters = std::get_if<dt::DisplayParameters>(&dc_charge_loop_req)) {
            publish_display_parameters(convert_display_parameters(*display_parameters));
        } else if (const auto* present_voltage = std::get_if<feedback::PresentVoltage>(&dc_charge_loop_req)) {
            publish_dc_ev_present_voltage(dt::from_RationalNumber(*present_voltage));
        } else if (const auto* meter_info_requested = std::get_if<feedback::MeterInfoRequested>(&dc_charge_loop_req)) {
            if (*meter_info_requested) {
                EVLOG_info << "Meter info is requested from EV";
                publish_meter_info_requested(nullptr);
            }
        }
    };

    // DIN SPEC 70121 / ISO 15118-2 only: the EV's DC_EVStatus, above all its state of charge, which the
    // -20 charge loop instead reports through DisplayParameters.
    callbacks.dc_ev_status = [this](const feedback::DcEvStatus& ev_status) {
        publish_dc_ev_status(convert_dc_ev_status(ev_status));
    };

    // DIN SPEC 70121 / ISO 15118-2 only: what the EV asked for in ChargeParameterDiscoveryReq. Fans out
    // to the per-session ev_info vars EvseManager consumes and to the OCPP ChargingNeeds notification on
    // the extensions interface. ISO 15118-20 reports the equivalent via notify_ev_charging_needs.
    callbacks.ev_charge_parameters = [this](const feedback::EvChargeParameters& parameters) {
        const auto transfer_mode = convert_energy_transfer_mode(parameters.requested_energy_transfer);
        publish_requested_energy_transfer_mode(transfer_mode);

        types::iso15118::ChargingNeeds charging_needs;
        charging_needs.requested_energy_transfer = transfer_mode;

        // The EV gives its departure time as seconds from now; both consumers get the same absolute
        // timestamp rather than two computed moments apart.
        if (parameters.departure_time.has_value()) {
            const auto departure_time = seconds_from_now_to_rfc3339(parameters.departure_time.value());
            publish_departure_time(departure_time);
            charging_needs.departure_time = departure_time;
        }

        if (parameters.dc.has_value()) {
            const auto& dc = parameters.dc.value();
            if (dc.energy_capacity.has_value()) {
                publish_dc_ev_energy_capacity(dc.energy_capacity.value());
            }
            if (dc.energy_request.has_value()) {
                publish_dc_ev_energy_request(dc.energy_request.value());
            }
            if (dc.full_soc.has_value()) {
                publish_dc_full_soc(dc.full_soc.value());
            }
            if (dc.bulk_soc.has_value()) {
                publish_dc_bulk_soc(dc.bulk_soc.value());
            }
            charging_needs.dc_charging_parameters = convert_dc_charging_parameters(dc);
        }

        if (parameters.ac.has_value()) {
            const auto& ac = parameters.ac.value();
            publish_ac_eamount(ac.e_amount);
            publish_ac_ev_max_voltage(ac.max_voltage);
            publish_ac_ev_max_current(ac.max_current);
            publish_ac_ev_min_current(ac.min_current);
            charging_needs.ac_charging_parameters = convert_ac_charging_parameters(ac);
        }

        this->mod->p_extensions->publish_charging_needs(charging_needs);
    };

    // DIN SPEC 70121 / ISO 15118-2 only: the EV's charge progress. The remaining times are absent when
    // the update came from a PowerDeliveryReq, which carries only the completion flags -- publishing
    // dc_ev_remaining_time then would clear the values the charge loop reported.
    callbacks.dc_ev_charge_progress = [this](const feedback::DcEvChargeProgress& progress) {
        if (progress.remaining_time_to_full_soc.has_value() or progress.remaining_time_to_bulk_soc.has_value()) {
            types::iso15118::DcEvRemainingTime remaining_time;
            if (progress.remaining_time_to_full_soc.has_value()) {
                remaining_time.ev_remaining_time_to_full_soc =
                    seconds_from_now_to_rfc3339(progress.remaining_time_to_full_soc.value());
            }
            if (progress.remaining_time_to_bulk_soc.has_value()) {
                remaining_time.ev_remaining_time_to_full_bulk_soc =
                    seconds_from_now_to_rfc3339(progress.remaining_time_to_bulk_soc.value());
            }
            publish_dc_ev_remaining_time(remaining_time);
        }

        // The remaining times change with nearly every CurrentDemandReq, so the progress feedback fires
        // that often; the completion flags rarely change and are published only when they do.
        if (last_charging_complete != progress.charging_complete) {
            last_charging_complete = progress.charging_complete;
            publish_dc_charging_complete(progress.charging_complete);
        }
        if (progress.bulk_charging_complete.has_value() and
            last_bulk_charging_complete != progress.bulk_charging_complete) {
            last_bulk_charging_complete = progress.bulk_charging_complete;
            publish_dc_bulk_charging_complete(progress.bulk_charging_complete.value());
        }
    };

    callbacks.dc_max_limits = [this](const feedback::DcMaximumLimits& max_limits) {
        publish_dc_ev_maximum_limits({max_limits.current, max_limits.power, max_limits.voltage});
    };

    callbacks.ac_limits = [this](const feedback::AcLimits& limits) {
        if (const auto* ac_transfer_mode = std::get_if<dt::AC_CPDReqEnergyTransferMode>(&limits)) {
            publish_ac_ev_power_limits(fill_ac_ev_power_limits(*ac_transfer_mode));
        } else if (const auto* ac_bpt_transfer_mode = std::get_if<dt::BPT_AC_CPDReqEnergyTransferMode>(&limits)) {
            publish_ac_ev_power_limits(fill_ac_ev_power_limits(*ac_bpt_transfer_mode));
        } else if (const auto* der_iec_transfer_mode = std::get_if<dt::DER_AC_CPDReqEnergyTransferMode>(&limits)) {
            publish_ac_ev_power_limits(fill_ac_ev_power_limits(*der_iec_transfer_mode));
        }
    };

    callbacks.ac_charge_loop_req = [this](const feedback::AcChargeLoopReq& ac_charge_loop_req) {
        if (const auto* ac_control_mode = std::get_if<feedback::AcReqControlMode>(&ac_charge_loop_req)) {
            if (const auto* scheduled_mode = std::get_if<ScheduleControlModeAC>(ac_control_mode)) {
                if (scheduled_mode->max_charge_power.has_value() or scheduled_mode->max_charge_power_L2.has_value() or
                    scheduled_mode->max_charge_power_L3.has_value() or scheduled_mode->min_charge_power.has_value() or
                    scheduled_mode->min_charge_power_L2.has_value() or
                    scheduled_mode->min_charge_power_L3.has_value()) {
                    publish_ac_ev_power_limits(fill_ac_ev_power_limits(*scheduled_mode));
                }
                publish_ac_ev_present_powers(fill_ac_ev_present_power_values(*scheduled_mode));
                // TODO(SL): publish_scheduled_control_mode
            } else if (const auto* bpt_scheduled_mode = std::get_if<BPT_ScheduleReqControlModeAC>(ac_control_mode)) {
                if (bpt_scheduled_mode->max_charge_power.has_value() or
                    bpt_scheduled_mode->max_charge_power_L2.has_value() or
                    bpt_scheduled_mode->max_charge_power_L3.has_value() or
                    bpt_scheduled_mode->min_charge_power.has_value() or
                    bpt_scheduled_mode->min_charge_power_L2.has_value() or
                    bpt_scheduled_mode->min_charge_power_L3.has_value()) {
                    publish_ac_ev_power_limits(fill_ac_ev_power_limits(*bpt_scheduled_mode));
                }
                publish_ac_ev_present_powers(fill_ac_ev_present_power_values(*bpt_scheduled_mode));
                // TODO(SL): publish_scheduled_control_mode
            } else if (const auto* dynamic_mode = std::get_if<DynamicReqControlModeAC>(ac_control_mode)) {
                publish_ac_ev_power_limits(fill_ac_ev_power_limits(*dynamic_mode));
                publish_ac_ev_present_powers(fill_ac_ev_present_power_values(*dynamic_mode));
                publish_ac_ev_dynamic_control_mode(fill_ac_ev_dynamic_control_mode(*dynamic_mode));
            } else if (const auto* bpt_dynamic_mode = std::get_if<BPT_DynamicReqControlModeAC>(ac_control_mode)) {
                publish_ac_ev_power_limits(fill_ac_ev_power_limits(*bpt_dynamic_mode));
                publish_ac_ev_present_powers(fill_ac_ev_present_power_values(*bpt_dynamic_mode));
                auto ev_dynamic_values = fill_ac_ev_dynamic_control_mode(*bpt_dynamic_mode);
                ev_dynamic_values.max_v2x_energy_request =
                    convert_from_optional(bpt_dynamic_mode->max_v2x_energy_request);
                ev_dynamic_values.min_v2x_energy_request =
                    convert_from_optional(bpt_dynamic_mode->min_v2x_energy_request);
                publish_ac_ev_dynamic_control_mode(ev_dynamic_values);
            } else if (const auto* der_scheduled_mode =
                           std::get_if<dt::DER_Scheduled_AC_CLReqControlMode>(ac_control_mode)) {
                publish_ac_ev_power_limits(fill_ac_ev_power_limits(*der_scheduled_mode));
                publish_ac_ev_present_powers(fill_ac_ev_present_power_values(*der_scheduled_mode));
                publish_grid_event(der_scheduled_mode->grid_event_condition);
                // TODO(ml): reactive-power fields are not yet surfaced.
            } else if (const auto* der_dynamic_mode =
                           std::get_if<dt::DER_Dynamic_AC_CLReqControlMode>(ac_control_mode)) {
                publish_ac_ev_power_limits(fill_ac_ev_power_limits(*der_dynamic_mode));
                publish_ac_ev_present_powers(fill_ac_ev_present_power_values(*der_dynamic_mode));
                auto ev_dynamic_values = fill_ac_ev_dynamic_control_mode(*der_dynamic_mode);
                ev_dynamic_values.max_v2x_energy_request =
                    convert_from_optional(der_dynamic_mode->max_v2x_energy_request);
                ev_dynamic_values.min_v2x_energy_request =
                    convert_from_optional(der_dynamic_mode->min_v2x_energy_request);
                publish_ac_ev_dynamic_control_mode(ev_dynamic_values);
                publish_grid_event(der_dynamic_mode->grid_event_condition);
                // TODO(ml): reactive-power fields and session_total_discharge_energy_available are
                // not yet surfaced.
            }
        } else if (const auto* display_parameters = std::get_if<dt::DisplayParameters>(&ac_charge_loop_req)) {
            publish_display_parameters(convert_display_parameters(*display_parameters));
        } else if (const auto* meter_info_requested = std::get_if<feedback::MeterInfoRequested>(&ac_charge_loop_req)) {
            if (*meter_info_requested) {
                EVLOG_info << "Meter info is requested from EV";
                publish_meter_info_requested(nullptr);
            }
        }
    };

    callbacks.signal = [this](feedback::Signal signal) {
        using Signal = feedback::Signal;
        switch (signal) {
        case Signal::PRE_CHARGE_STARTED:
            publish_start_pre_charge(nullptr);
            break;
        case Signal::CHARGE_LOOP_STARTED:
            publish_current_demand_started(nullptr);
            break;
        case Signal::SETUP_FINISHED:
            // Reset so a fault held from a prior session does not dedup a genuine new one.
            grid_event_detector.reset();
            // Clear the prior session's negotiated DER controls; the next session renegotiates them.
            ev_selected_der_control_functions.reset();
            publish_v2g_setup_finished(nullptr);
            break;
        case Signal::START_CABLE_CHECK:
            publish_start_cable_check(nullptr);
            break;
        case Signal::REQUIRE_AUTH_EIM:
            publish_require_auth_eim(nullptr);
            break;
        case Signal::CHARGE_LOOP_FINISHED:
            publish_current_demand_finished(nullptr);
            break;
        case Signal::DC_OPEN_CONTACTOR:
            publish_dc_open_contactor(nullptr);
            break;
        case Signal::AC_CLOSE_CONTACTOR:
            publish_ac_close_contactor(nullptr);
            break;
        case Signal::AC_OPEN_CONTACTOR:
            publish_ac_open_contactor(nullptr);
            break;
        case Signal::DLINK_TERMINATE:
            report_hlc_session_failed();
            publish_dlink_terminate(nullptr);
            break;
        case Signal::DLINK_PAUSE:
            // A pause is not a session failure, so nothing is reported -- but the data link ends here
            // too, so the per-session state still has to go (EvseV2G reinitialises it on every
            // connection teardown, pause included).
            reset_session_state();
            publish_dlink_pause(nullptr);
            break;
        case Signal::DLINK_ERROR:
            report_hlc_session_failed();
            publish_dlink_error(nullptr);
            break;
        }
    };

    callbacks.v2g_message = [this](const iso15118::V2gMessageType& id, const iso15118::io::StreamInputView& exi_frame) {
        // Always tracked: hlc_session_failed derives its reason from the last message at teardown.
        last_v2g_message = id;
        // Published only in debug mode, mirroring EvseV2G's debugMode gate on this var. The frame is
        // the complete V2GTP message (8-byte header + EXI payload), rendered in the same two encodings
        // EvseV2G uses so existing log tooling can decode either one (v2g_server.cpp:275-288). xml and
        // v2g_json stay unset: neither stack decodes the EXI back into a document.
        if (debug_mode) {
            types::iso15118::V2gMessages message;
            message.id = convert_v2g_message_type(id);
            if (exi_frame.payload != nullptr and exi_frame.payload_len > 0) {
                message.exi = to_hex_string(exi_frame);
                message.exi_base64 = to_base64_string(exi_frame);
            }
            publish_v2g_messages(message);
        }
    };

    // Debug only, like EvseV2G (v2g_server.cpp:467): the protocol list the EV offered in
    // SupportedAppProtocolReq, reported whether or not the negotiation went on to succeed.
    callbacks.ev_app_protocols = [this](const iso15118::message_20::SupportedAppProtocolRequest& req) {
        if (not debug_mode) {
            return;
        }
        types::iso15118::AppProtocols app_protocols;
        app_protocols.Protocols.reserve(req.app_protocol.size());
        for (const auto& protocol : req.app_protocol) {
            app_protocols.Protocols.push_back(convert_app_protocol(protocol));
        }
        publish_ev_app_protocol(app_protocols);
    };

    callbacks.evccid = [this](const std::string& evccid) { publish_evcc_id(evccid); };

    callbacks.selected_protocol = [this](const std::string& protocol) { publish_selected_protocol(protocol); };

    // DIN SPEC 70121 / ISO 15118-2 only; ISO 15118-20 negotiates authorization services instead and
    // leaves this var untouched (EvseV2G iso_server.cpp:1020 parity).
    callbacks.selected_payment_option = [this](iso15118::shared_datatypes::PaymentOption payment_option) {
        publish_selected_payment_option(payment_option == iso15118::shared_datatypes::PaymentOption::Contract
                                            ? types::iso15118::PaymentOption::Contract
                                            : types::iso15118::PaymentOption::ExternalPayment);
    };

    callbacks.session_stop_res_sent = [this](iso15118::session::feedback::SessionStopAction action) {
        // Anchor of the CP-oscillator retain time [V2G-DC-968] (Terminate/Pause) resp. of the
        // immediate oscillator-off in the error case [V2G-DC-942] (FailedTermination); the DLINK_*
        // signal still follows after the TCP connection is closed.
        using LibAction = iso15118::session::feedback::SessionStopAction;
        auto everest_action = types::iso15118::SessionStopAction::Terminate;
        if (action == LibAction::Pause) {
            everest_action = types::iso15118::SessionStopAction::Pause;
        } else if (action == LibAction::FailedTermination) {
            everest_action = types::iso15118::SessionStopAction::FailedTermination;
        }
        publish_session_stop_res_sent(everest_action);
    };

    callbacks.selected_service_parameters = [this](const iso15118::d20::SelectedServiceParameters& parameters) {
        // Captured for ChargeParameterDiscovery to surface DERChargingParameters.ev_supported_dercontrol.
        ev_selected_der_control_functions = parameters.selected_der_control_functions;

        types::iso15118::SelectedServiceParameters selected_parameters{};

        if (parameters.selected_energy_service == dt::ServiceCategory::AC) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::AC;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::DC) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::DC;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::WPT) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::WPT;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::DC_ACDP) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::DC_ACDP;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::AC_BPT) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::AC_BPT;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::DC_BPT) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::DC_BPT;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::DC_ACDP_BPT) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::DC_ACDP_BPT;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::MCS) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::MCS;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::MCS_BPT) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::MCS_BPT;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::AC_DER_IEC) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::AC_DER_IEC;
        } else if (parameters.selected_energy_service == dt::ServiceCategory::AC_DER_SAE) {
            selected_parameters.energy_transfer = types::iso15118::ServiceCategory::AC_DER_SAE;
        } else {
            EVLOG_critical << "Energy service is apparently no energy service!";
        }

        if (const auto* ac_connector = std::get_if<dt::AcConnector>(&parameters.selected_connector)) {
            if (*ac_connector == dt::AcConnector::SinglePhase) {
                selected_parameters.connector = types::iso15118::Connector::SinglePhase;
            } else {
                selected_parameters.connector = types::iso15118::Connector::ThreePhase;
            }
        } else if (const auto* dc_connector = std::get_if<dt::DcConnector>(&parameters.selected_connector)) {
            if (*dc_connector == dt::DcConnector::Core) {
                selected_parameters.connector = types::iso15118::Connector::Core;
            } else if (*dc_connector == dt::DcConnector::Extended) {
                selected_parameters.connector = types::iso15118::Connector::Extended;
            } else if (*dc_connector == dt::DcConnector::Dual2) {
                selected_parameters.connector = types::iso15118::Connector::Dual2;
            } else {
                selected_parameters.connector = types::iso15118::Connector::Dual4;
            }
        } else if (const auto* mcs_connector = std::get_if<dt::McsConnector>(&parameters.selected_connector)) {
            if (*mcs_connector == dt::McsConnector::Mcs) {
                selected_parameters.connector = types::iso15118::Connector::Mcs;
            } else if (*mcs_connector == dt::McsConnector::Chaoji) {
                selected_parameters.connector = types::iso15118::Connector::Chaoji;
            } else if (*mcs_connector == dt::McsConnector::UltraChaoji) {
                selected_parameters.connector = types::iso15118::Connector::UltraChaoji;
            } else if (*mcs_connector == dt::McsConnector::rMcs) {
                selected_parameters.connector = types::iso15118::Connector::rMcs;
            } else if (*mcs_connector == dt::McsConnector::xMcs) {
                selected_parameters.connector = types::iso15118::Connector::xMcs;
            } else if (*mcs_connector == dt::McsConnector::Aviation) {
                selected_parameters.connector = types::iso15118::Connector::Aviation;
            } else if (*mcs_connector == dt::McsConnector::Marine) {
                selected_parameters.connector = types::iso15118::Connector::Marine;
            } else {
                selected_parameters.connector = types::iso15118::Connector::Dual4;
            }
        }

        if (parameters.selected_control_mode == dt::ControlMode::Scheduled) {
            selected_parameters.control_mode = types::iso15118::ControlMode::ScheduledControl;
        } else {
            selected_parameters.control_mode = types::iso15118::ControlMode::DynamicControl;
        }
        if (parameters.selected_mobility_needs_mode == dt::MobilityNeedsMode::ProvidedByEvcc) {
            selected_parameters.mobility_needs_mode = types::iso15118::MobilityNeedsMode::EVCC;
        } else {
            selected_parameters.mobility_needs_mode = types::iso15118::MobilityNeedsMode::EVCC_SECC;
        }
        selected_parameters.pricing = static_cast<types::iso15118::Pricing>(parameters.selected_pricing);

        if (parameters.selected_bpt_channel.has_value()) {
            if (parameters.selected_bpt_channel.value() == dt::BptChannel::Unified) {
                selected_parameters.bpt_channel = types::iso15118::BptChannel::Unified;
            } else {
                selected_parameters.bpt_channel = types::iso15118::BptChannel::Separated;
            }
        }
        if (parameters.selected_generator_mode.has_value()) {
            if (parameters.selected_generator_mode.value() == dt::GeneratorMode::GridFollowing) {
                selected_parameters.generator_mode = types::iso15118::GeneratorMode::GridFollowing;
            } else {
                selected_parameters.generator_mode = types::iso15118::GeneratorMode::GridForming;
            }
        }

        publish_selected_service_parameters(selected_parameters);
    };

    callbacks.selected_vas_services = [this](const dt::VasSelectedServiceList& selected_services) {
        // Group selected services by VAS provider index
        std::map<size_t, std::vector<types::iso15118_vas::SelectedService>> services_by_provider;

        for (const auto& service : selected_services) {
            const auto provider_index = get_vas_provider_index(service.service_id);
            if (not provider_index.has_value()) {
                EVLOG_warning << "Selected Service ID " << service.service_id
                              << " is not supported by any VAS provider";
                continue;
            }

            types::iso15118_vas::SelectedService converted_service{service.service_id, service.parameter_set_id};
            services_by_provider[*provider_index].push_back(converted_service);
        }

        // Notify providers about their selected services
        for (const auto& [provider_index, services] : services_by_provider) {
            mod->r_iso15118_vas[provider_index]->call_selected_services(services);
        }
    };

    callbacks.get_vas_parameters = [this](uint16_t service_id) -> std::optional<dt::ServiceParameterList> {
        const auto provider_index = get_vas_provider_index(service_id);
        if (not provider_index.has_value()) {
            // note: this can only happen if the service ID was removed from the provider after ServiceDiscoveryRes
            // was sent
            EVLOG_warning << "Service ID " << service_id << " is not supported by any VAS provider";
            return std::nullopt;
        }

        const auto vas_parameters = mod->r_iso15118_vas[*provider_index]->call_get_service_parameters(service_id);

        if (vas_parameters.empty()) {
            EVLOG_warning << "No parameters found for service ID " << service_id << " in provider #" << *provider_index;
            return std::nullopt;
        }

        try {
            return convert_parameter_set_list(vas_parameters);
        } catch (const std::exception& e) {
            EVLOG_error << "Failed to convert VAS parameters for service ID " << service_id << " from provider #"
                        << *provider_index << ": " << e.what();
            return std::nullopt;
        }
    };
    callbacks.ev_information = [this](const iso15118::d20::EVInformation& ev_info) {
        types::iso15118::EvInformation info = convert_ev_info(ev_info);
        this->mod->p_extensions->publish_ev_info(info);
    };

    callbacks.ev_termination = [this](const std::string& ev_termination_code,
                                      const std::string& ev_termination_explanation) {
        types::iso15118::EvTermination termination_ctx;
        termination_ctx.ev_termination_code = ev_termination_code;
        termination_ctx.ev_termination_explanation = ev_termination_explanation;
        this->mod->p_charger->publish_ev_termination(termination_ctx);
    };

    // ISO 15118-2 Plug-and-Charge: the library verified a signed AuthorizationReq and asks the higher
    // layer to authorize the contract eMAID. Republish it as a PnC ProvidedIdToken (EvseManager forwards
    // it to Auth, which validates it and answers via handle_authorization_response).
    callbacks.require_auth_pnc = [this](const std::string& emaid, const std::string& contract_chain_pem) {
        types::authorization::ProvidedIdToken token;
        token.id_token = {emaid, types::authorization::IdTokenType::eMAID};
        token.authorization_type = types::authorization::AuthorizationType::PlugAndCharge;
        if (not contract_chain_pem.empty()) {
            token.certificate = contract_chain_pem;
        }
        this->mod->p_charger->publish_require_auth_pnc(token);
    };

    // ISO 15118-2 Plug-and-Charge CertificateInstallation relay: libiso15118 forwards the raw
    // CertificateInstallationReq EXI (base64). Republish it verbatim on the iso15118_extensions
    // interface (iso15118_certificate_request) so the CSMS/CPS backend can build the response. The
    // response is delivered async via handle_set_get_certificate_response (see on_certificate_response).
    callbacks.certificate_request = [this](const std::string& exi_request_base64,
                                           iso15118::session::feedback::CertificateExchangeAction action) {
        types::iso15118::RequestExiStreamSchema request;
        request.exi_request = exi_request_base64;
        request.iso15118_schema_version = "urn:iso:15118:2:2013:MsgDef";
        request.certificate_action = (action == iso15118::session::feedback::CertificateExchangeAction::Update)
                                         ? types::iso15118::CertificateActionEnum::Update
                                         : types::iso15118::CertificateActionEnum::Install;
        this->mod->p_extensions->publish_iso15118_certificate_request(request);
    };

    return callbacks;
}

void ISO15118_chargerImpl::reset_session_state() {
    // Per-session state that must not leak into the following session. Run at every end of the data
    // link, pause included: EvseV2G clears the equivalent fields from v2g_ctx_init_charging_state()
    // (v2g_ctx.cpp:104), which connection_teardown() calls before it looks at the D-LINK action, so a
    // paused session leaves nothing behind either.
    last_v2g_message.reset();
    graceful_stop_requested = false;
    emergency_shutdown_requested = false;
    last_charging_complete.reset();
    last_bulk_charging_complete.reset();
}

void ISO15118_chargerImpl::report_hlc_session_failed() {
    // Runs on the session loop thread from the DLINK_TERMINATE / DLINK_ERROR feedback. Derive a
    // protocol-agnostic failure reason from the last V2G message the session handled and publish it,
    // unless the EVSE ended the session itself.
    //
    // Both EVSE-initiated ends suppress the report, matching EvseV2G's
    // `stop_hlc || intl_emergency_shutdown` (connection.cpp:518). An emergency shutdown is therefore not
    // reported as an HLC session failure: it is an EVSE fault the module already raised as an error
    // (handle_send_error), not a failure of the V2G session, and EvseManager learns about it that way.
    std::optional<types::evse_manager::HlcSessionFailedReasonEnum> reason;
    if (last_v2g_message.has_value()) {
        reason = map_v2g_message_to_hlc_failed_reason(*last_v2g_message);
    }

    const bool evse_initiated_stop = graceful_stop_requested.load() or emergency_shutdown_requested.load();
    if (reason.has_value() and not evse_initiated_stop) {
        publish_hlc_session_failed(*reason);
    }

    reset_session_state();
}

void ISO15118_chargerImpl::handle_setup(types::iso15118::EVSEID& evse_id,
                                        types::iso15118::SaeJ2847BidiMode& sae_j2847_mode, bool& debug_mode) {

    // SAE J2847/2 is not implemented here (EvseV2G offers it). Say so rather than have EvseManager
    // wait for a sae_bidi_mode_active that never comes.
    if (sae_j2847_mode != types::iso15118::SaeJ2847BidiMode::None) {
        EVLOG_warning << "SAE J2847/2 bidirectional mode ("
                      << types::iso15118::sae_j2847bidi_mode_to_string(sae_j2847_mode)
                      << ") is not supported by this module; the SAE service is not offered";
    }
    // debug_mode gates the v2g_messages and ev_app_protocol publishes like EvseV2G's debugMode.
    this->debug_mode = debug_mode;
    if (debug_mode) {
        EVLOG_info << "debug_mode enabled: publishing v2g_messages (message id plus the raw V2GTP frame as hex "
                      "and base64) and ev_app_protocol";
    }

    std::scoped_lock lock(GEL);
    setup_config.evse_id = evse_id.evse_id; // TODO(SL): Check format for d20

    setup_steps_done.set(SetupStep::SETUP);
}

void ISO15118_chargerImpl::handle_set_charging_parameters(types::iso15118::SetupPhysicalValues& physical_values) {
    // Physical EVSE parameters for the ISO 15118-2 / DIN SPEC 70121 EVSEChargeParameter elements:
    // EVSENominalVoltage (AC) and EVSEPeakCurrentRipple / EVSECurrentRegulationTolerance /
    // EVSEEnergyToBeDelivered (DC). ISO 15118-20 carries the same information in its limit structures and
    // ignores this. EvseManager calls this at setup (AC) and on every power-supply capabilities push (DC),
    // which may land mid-session, so a live controller is updated too (mirrors EvseV2G's handler).
    iso15118::d20::PhysicalValues values;
    values.ac_nominal_voltage = physical_values.ac_nominal_voltage;
    values.dc_current_regulation_tolerance = physical_values.dc_current_regulation_tolerance;
    values.dc_peak_current_ripple = physical_values.dc_peak_current_ripple;
    values.dc_energy_to_be_delivered = physical_values.dc_energy_to_be_delivered;

    std::scoped_lock lock(GEL);
    setup_config.physical_values = values;
    if (controller) {
        controller->update_physical_values(values);
    }
}

void ISO15118_chargerImpl::handle_session_setup(std::vector<types::iso15118::PaymentOption>& payment_options,
                                                bool& supported_certificate_service,
                                                bool& central_contract_validation_allowed) {
    std::scoped_lock lock(GEL);

    std::vector<dt::Authorization> auth_services;

    bool contract_offered = false;
    for (auto& option : payment_options) {
        if (option == types::iso15118::PaymentOption::ExternalPayment) {
            auth_services.push_back(dt::Authorization::EIM);
        } else if (option == types::iso15118::PaymentOption::Contract) {
            // ISO 15118-20 PnC is not yet wired; the ISO 15118-2 SECC engine does support Plug-and-Charge
            // (Contract payment) and is enabled via setup_config.iso2_pnc_enabled below.
            contract_offered = true;
        }
    }

    setup_config.authorization_services = auth_services;
    setup_config.iso2_pnc_enabled = contract_offered;
    setup_config.enable_certificate_install_service = supported_certificate_service;
    // ISO 15118-2 PnC: accept a contract without a local MO root and forward it for central validation
    // (OCPP CentralContractValidationAllowed, via EvseManager).
    setup_config.central_contract_validation_allowed = central_contract_validation_allowed;

    // session_setup is (re)sent by EvseManager for every session: push the updated auth/PnC setup into
    // the already running controller so runtime changes (e.g. via OCPP) apply to the next session.
    if (controller) {
        controller->update_authorization_services(auth_services, supported_certificate_service);
        controller->update_iso2_pnc_config(contract_offered, central_contract_validation_allowed);
    }

    setup_steps_done.set(SetupStep::AUTH_SETUP);
}

void ISO15118_chargerImpl::handle_bpt_setup(types::iso15118::BptSetup& bpt_config) {
    std::scoped_lock lock(GEL);

    // Existing values in bpt_setup_config will be overwritten
    auto& bpt_setup = setup_config.bpt_setup_config.emplace();

    bpt_setup.bpt_channel = bpt_config.bpt_channel == types::iso15118::BptChannel::Unified ? dt::BptChannel::Unified
                                                                                           : dt::BptChannel::Separated;
    bpt_setup.generator_mode = bpt_config.generator_mode == types::iso15118::GeneratorMode::GridFollowing
                                   ? dt::GeneratorMode::GridFollowing
                                   : dt::GeneratorMode::GridForming;

    if (bpt_config.grid_code_detection.has_value()) {
        bpt_setup.grid_code_detection_method =
            bpt_config.grid_code_detection.value() == types::iso15118::GridCodeIslandingDetectionMethod::Active
                ? dt::GridCodeIslandingDetectionMethod::Active
                : dt::GridCodeIslandingDetectionMethod::Passive;
    }
}

void ISO15118_chargerImpl::handle_set_powersupply_capabilities(types::power_supply_DC::Capabilities& capabilities) {
    std::scoped_lock lock(GEL);

    // Export Limits - prefer nominal values over regular values
    const auto max_export_current_A =
        capabilities.nominal_max_export_current_A.value_or(capabilities.max_export_current_A);
    const auto min_export_current_A =
        capabilities.nominal_min_export_current_A.value_or(capabilities.min_export_current_A);
    const auto max_export_power_W = capabilities.nominal_max_export_power_W.value_or(capabilities.max_export_power_W);
    const auto max_export_voltage_V =
        capabilities.nominal_max_export_voltage_V.value_or(capabilities.max_export_voltage_V);
    const auto min_export_voltage_V =
        capabilities.nominal_min_export_voltage_V.value_or(capabilities.min_export_voltage_V);

    setup_config.powersupply_limits.charge_limits.current.max = dt::from_float(max_export_current_A);
    setup_config.powersupply_limits.charge_limits.current.min = dt::from_float(min_export_current_A);
    setup_config.powersupply_limits.charge_limits.power.max = dt::from_float(max_export_power_W);
    setup_config.powersupply_limits.charge_limits.power.min =
        dt::from_float(min_export_current_A * min_export_voltage_V);
    setup_config.powersupply_limits.voltage.max = dt::from_float(max_export_voltage_V);
    setup_config.powersupply_limits.voltage.min = dt::from_float(min_export_voltage_V);

    // Discharge Limits - prefer nominal values over regular values
    const auto max_import_power_W = capabilities.nominal_max_import_power_W.has_value()
                                        ? capabilities.nominal_max_import_power_W
                                        : capabilities.max_import_power_W;
    const auto min_import_current_A = capabilities.nominal_min_import_current_A.has_value()
                                          ? capabilities.nominal_min_import_current_A
                                          : capabilities.min_import_current_A;
    const auto min_import_voltage_V = capabilities.nominal_min_import_voltage_V.has_value()
                                          ? capabilities.nominal_min_import_voltage_V
                                          : capabilities.min_import_voltage_V;
    const auto max_import_current_A = capabilities.nominal_max_import_current_A.has_value()
                                          ? capabilities.nominal_max_import_current_A
                                          : capabilities.max_import_current_A;

    if (max_import_power_W.has_value() or (min_import_current_A.has_value() and min_import_voltage_V.has_value()) or
        max_import_current_A.has_value() or min_import_current_A.has_value()) {
        auto& discharge_power = (setup_config.powersupply_limits.discharge_limits.has_value())
                                    ? setup_config.powersupply_limits.discharge_limits.value()
                                    : setup_config.powersupply_limits.discharge_limits.emplace();

        if (mod->config.negative_bidirectional_limits) {
            discharge_power.power.max = dt::from_float(-std::fabs(max_import_power_W.value_or(0.0)));
            discharge_power.power.min =
                dt::from_float(-std::fabs(min_import_current_A.value_or(0.0)) * min_import_voltage_V.value_or(0.0));
            discharge_power.current.max = dt::from_float(-std::fabs(max_import_current_A.value_or(0.0)));
            discharge_power.current.min = dt::from_float(-std::fabs(min_import_current_A.value_or(0.0)));
        } else {
            discharge_power.power.max = dt::from_float(max_import_power_W.value_or(0.0));
            discharge_power.power.min =
                dt::from_float(min_import_current_A.value_or(0.0) * min_import_voltage_V.value_or(0.0));
            discharge_power.current.max = dt::from_float(max_import_current_A.value_or(0.0));
            discharge_power.current.min = dt::from_float(min_import_current_A.value_or(0.0));
        }
    }

    if (controller) {
        controller->update_powersupply_limits(setup_config.powersupply_limits);
    }

    setup_steps_done.set(SetupStep::MAX_LIMITS);
    setup_steps_done.set(SetupStep::MIN_LIMITS);
}

void ISO15118_chargerImpl::handle_authorization_response(
    types::authorization::AuthorizationStatus& authorization_status,
    types::authorization::CertificateStatus& certificate_status) {

    std::scoped_lock lock(GEL);
    const bool authorized = (authorization_status == types::authorization::AuthorizationStatus::Accepted);
    // ISO 15118-2 Plug-and-Charge: a rejection because the contract certificate is revoked is named as
    // such in the AuthorizationRes (FAILED_CertificateRevoked). Only meaningful with a rejection.
    const bool certificate_revoked =
        not authorized and certificate_status == types::authorization::CertificateStatus::CertificateRevoked;

    if (controller) {
        controller->send_control_event(iso15118::d20::AuthorizationResponse{authorized, certificate_revoked});
    }
}

void ISO15118_chargerImpl::handle_ac_contactor_closed(bool& status) {
    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::ClosedContactor{status});
    }
}

void ISO15118_chargerImpl::handle_cp_state_changed(types::iso15118::CpState& cp_state) {
    using LibCpState = iso15118::d20::CpState;
    auto state = LibCpState::A;
    switch (cp_state) {
    case types::iso15118::CpState::A:
        state = LibCpState::A;
        break;
    case types::iso15118::CpState::B:
        state = LibCpState::B;
        break;
    case types::iso15118::CpState::C:
        state = LibCpState::C;
        break;
    case types::iso15118::CpState::D:
        state = LibCpState::D;
        break;
    case types::iso15118::CpState::E:
        state = LibCpState::E;
        break;
    case types::iso15118::CpState::F:
        state = LibCpState::F;
        break;
    }
    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::CpStateChanged{state});
    }
}

void ISO15118_chargerImpl::handle_dlink_ready(bool& value) {
    std::scoped_lock lock(GEL);
    if (controller) {
        controller->set_dlink_ready(value);
    }
}

void ISO15118_chargerImpl::handle_cable_check_finished(bool& status) {

    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::CableCheckFinished{status});
    }
}

void ISO15118_chargerImpl::handle_receipt_is_required(bool& receipt_required) {
    // Request a (signed) MeteringReceipt from the EV: the SECC sets ReceiptRequired in the DC
    // CurrentDemandRes / AC ChargingStatusRes charge loop (PnC only). EvseManager calls this from its
    // own ready(), whose order vs this module's ready() (which creates the controller) is not
    // guaranteed. Store it in setup_config so a controller created later picks it up, AND update a live
    // controller for calls that arrive after creation. (Mirrors EvseV2G's evse_v2g_data.receipt_required.)
    std::scoped_lock lock(GEL);
    setup_config.iso2_receipt_required = receipt_required;
    if (controller) {
        controller->update_receipt_required(receipt_required);
    }
}

void ISO15118_chargerImpl::handle_stop_charging(bool& stop) {

    if (stop) {
        // A graceful EVSE-initiated stop is not a session failure: suppress the hlc_session_failed
        // report at teardown (unless an emergency shutdown also fired).
        graceful_stop_requested = true;
    }

    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::StopCharging{stop});
    }
}

void ISO15118_chargerImpl::handle_pause_charging(bool& pause) {
    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::PauseCharging{pause});
    }
}

void ISO15118_chargerImpl::handle_no_energy_pause_charging(types::iso15118::NoEnergyPauseMode& mode) {
    // IEC 61851-23:2023 CC.3.5.3: no energy is available for this session. The ISO 15118-2 and DIN SPEC
    // 70121 SECC engines signal EVSENotification StopCharging in ChargeParameterDiscoveryRes and stop
    // before starting the charge loop (EvseV2G parity). ISO 15118-20 does not act on it.
    auto pause = iso15118::d20::NoEnergyPauseMode::None;
    switch (mode) {
    case types::iso15118::NoEnergyPauseMode::PauseBeforeCableCheck:
        pause = iso15118::d20::NoEnergyPauseMode::BeforeCableCheck;
        break;
    case types::iso15118::NoEnergyPauseMode::PauseAfterPrecharge:
        pause = iso15118::d20::NoEnergyPauseMode::AfterCableCheckPreCharge;
        break;
    case types::iso15118::NoEnergyPauseMode::AllowEvToIgnorePause:
        pause = iso15118::d20::NoEnergyPauseMode::AllowEvToIgnorePause;
        break;
    }

    std::scoped_lock lock(GEL);
    // Stored as well as pushed: EvseManager may signal this before the V2G session exists. The stored
    // value is one-shot -- the next session to start consumes it.
    setup_config.no_energy_pause = pause;
    if (controller) {
        controller->update_no_energy_pause(pause);
    }
}

bool ISO15118_chargerImpl::handle_update_supported_app_protocols(
    types::iso15118::SupportedAppProtocols& supported_app_protocols) {
    if (supported_app_protocols.app_protocols.empty()) {
        EVLOG_warning << "No supported app protocols configured";
        return false;
    }

    std::string configured_protocols;

    for (const auto& protocol : supported_app_protocols.app_protocols) {
        if (!configured_protocols.empty()) {
            configured_protocols += ", ";
        }
        configured_protocols += types::iso15118::supported_app_protocol_to_string(protocol);
    }

    EVLOG_info << "Configured charging protocols: [" << configured_protocols << "]";

    // Narrow the SupportedAppProtocol offer to the requested subset of the configured protocols (the
    // interface exists so a test setup can switch a protocol off). Kept in priority order -- ISO 15118-20,
    // ISO 15118-2, DIN SPEC 70121 -- regardless of the order requested. Takes effect at the next session,
    // as with EvseV2G. A protocol that is not configured cannot be switched on here.
    bool all_supported{true};
    const auto requested = [&](types::iso15118::SupportedAppProtocol protocol) {
        return std::find(supported_app_protocols.app_protocols.begin(), supported_app_protocols.app_protocols.end(),
                         protocol) != supported_app_protocols.app_protocols.end();
    };
    std::vector<iso15118::ProtocolId> offered;
    if (requested(types::iso15118::SupportedAppProtocol::ISO15118D20) and iso15118_20_offerable) {
        offered.push_back(iso15118::ProtocolId::ISO15118_20);
    }
    if (requested(types::iso15118::SupportedAppProtocol::ISO15118D2) and mod->config.supported_ISO15118_2) {
        offered.push_back(iso15118::ProtocolId::ISO15118_2);
    }
    if (requested(types::iso15118::SupportedAppProtocol::DIN70121) and mod->config.supported_DIN70121) {
        offered.push_back(iso15118::ProtocolId::DIN70121);
    }

    for (const auto& protocol : supported_app_protocols.app_protocols) {
        const bool configured =
            (protocol == types::iso15118::SupportedAppProtocol::ISO15118D20 and iso15118_20_offerable) or
            (protocol == types::iso15118::SupportedAppProtocol::ISO15118D2 and mod->config.supported_ISO15118_2) or
            (protocol == types::iso15118::SupportedAppProtocol::DIN70121 and mod->config.supported_DIN70121);
        if (not configured) {
            EVLOG_warning << fmt::format("Unsupported app protocol: {}",
                                         types::iso15118::supported_app_protocol_to_string(protocol));
            all_supported = false;
        }
    }

    if (offered.empty()) {
        EVLOG_warning << "None of the requested app protocols is configured; keeping the current offer";
        return false;
    }

    std::scoped_lock lock(GEL);
    configured_protocol_offer = offered;
    apply_supported_protocols();
    return all_supported;
}

void ISO15118_chargerImpl::apply_supported_protocols() {
    auto offered = configured_protocol_offer;

    // DIN SPEC 70121 is DC only, so it must not be offered on an AC charger -- an EV picking it would only
    // run into FAILED_WrongEnergyTransferType at ChargeParameterDiscovery. EvseV2G does the same
    // (EvseV2G/charger/ISO15118_chargerImpl.cpp handle_update_energy_transfer_modes).
    bool din_removed{false};
    if (ac_energy_transfer_mode) {
        const auto din = std::find(offered.begin(), offered.end(), iso15118::ProtocolId::DIN70121);
        if (din != offered.end()) {
            offered.erase(din);
            din_removed = true;
        }
    }

    if (offered == setup_config.supported_protocols) {
        // Nothing changed; stay quiet (the energy transfer modes are updated during every session setup).
        return;
    }

    if (din_removed) {
        EVLOG_warning << "Removed DIN SPEC 70121 from the list of supported protocols as AC is enabled";
    }
    if (offered.empty()) {
        EVLOG_error << "No protocol left to offer: DIN SPEC 70121 is the only configured protocol but the "
                       "charger offers AC energy transfer. Every SupportedAppProtocol handshake will fail";
    }

    setup_config.supported_protocols = offered;
    if (controller) {
        controller->update_supported_protocols(offered);
    }
}

void ISO15118_chargerImpl::handle_update_energy_transfer_modes(
    std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) {

    std::scoped_lock lock(GEL);

    std::vector<dt::ServiceCategory> services;
    bool has_ac{false};

    for (const auto& mode : supported_energy_transfer_modes) {
        switch (mode) {
        case types::iso15118::EnergyTransferMode::AC_single_phase_core:
        case types::iso15118::EnergyTransferMode::AC_two_phase:
        case types::iso15118::EnergyTransferMode::AC_three_phase_core:
            services.push_back(dt::ServiceCategory::AC);
            has_ac = true;
            break;
        case types::iso15118::EnergyTransferMode::AC_BPT:
        case types::iso15118::EnergyTransferMode::AC_BPT_DER:
            services.push_back(dt::ServiceCategory::AC_BPT);
            has_ac = true;
            break;
        case types::iso15118::EnergyTransferMode::AC_DER_IEC:
            services.push_back(dt::ServiceCategory::AC_DER_IEC);
            has_ac = true;
            break;
        case types::iso15118::EnergyTransferMode::AC_DER_SAE:
            services.push_back(dt::ServiceCategory::AC_DER_SAE);
            has_ac = true;
            break;
        case types::iso15118::EnergyTransferMode::DC:
        case types::iso15118::EnergyTransferMode::DC_core:
        case types::iso15118::EnergyTransferMode::DC_extended:
        case types::iso15118::EnergyTransferMode::DC_combo_core:
        case types::iso15118::EnergyTransferMode::DC_unique:
            services.push_back(dt::ServiceCategory::DC);
            break;
        case types::iso15118::EnergyTransferMode::DC_BPT:
            services.push_back(dt::ServiceCategory::DC_BPT);
            break;
        case types::iso15118::EnergyTransferMode::DC_ACDP:
            services.push_back(dt::ServiceCategory::DC_ACDP);
            break;
        case types::iso15118::EnergyTransferMode::DC_ACDP_BPT:
            services.push_back(dt::ServiceCategory::DC_ACDP_BPT);
            break;
        case types::iso15118::EnergyTransferMode::WPT:
            services.push_back(dt::ServiceCategory::WPT);
            break;
        case types::iso15118::EnergyTransferMode::MCS:
            services.push_back(dt::ServiceCategory::MCS);
            break;
        case types::iso15118::EnergyTransferMode::MCS_BPT:
            services.push_back(dt::ServiceCategory::MCS_BPT);
            break;
        }
    }

    setup_config.supported_energy_services = services;

    // The ISO 15118-2 / DIN SPEC 70121 engines advertise the configured modes verbatim, so they keep the
    // distinctions the -20 service categories above collapse (DC_core vs DC_extended, DC_combo_core,
    // DC_unique). Modes that only exist in ISO 15118-20 (BPT, WPT, MCS, DER, ...) have no pre-20
    // counterpart and are left out; the plain "DC" of a -20-style config means DC_extended.
    std::vector<iso15118::shared_datatypes::EnergyTransferMode> pre20_modes;
    for (const auto& mode : supported_energy_transfer_modes) {
        using Pre20 = iso15118::shared_datatypes::EnergyTransferMode;
        std::optional<Pre20> converted;
        switch (mode) {
        case types::iso15118::EnergyTransferMode::AC_single_phase_core:
            converted = Pre20::AC_single_phase_core;
            break;
        case types::iso15118::EnergyTransferMode::AC_three_phase_core:
            converted = Pre20::AC_three_phase_core;
            break;
        case types::iso15118::EnergyTransferMode::DC:
        case types::iso15118::EnergyTransferMode::DC_extended:
            converted = Pre20::DC_extended;
            break;
        case types::iso15118::EnergyTransferMode::DC_core:
            converted = Pre20::DC_core;
            break;
        case types::iso15118::EnergyTransferMode::DC_combo_core:
            converted = Pre20::DC_combo_core;
            break;
        case types::iso15118::EnergyTransferMode::DC_unique:
            converted = Pre20::DC_unique;
            break;
        default:
            break;
        }
        if (converted.has_value() and
            std::find(pre20_modes.begin(), pre20_modes.end(), converted.value()) == pre20_modes.end()) {
            pre20_modes.push_back(converted.value());
        }
    }
    setup_config.pre20_energy_transfer_modes = pre20_modes;

    ac_energy_transfer_mode = has_ac;
    apply_supported_protocols();

    if (controller) {
        controller->update_energy_modes(services);
        controller->update_pre20_energy_transfer_modes(pre20_modes);
    }

    setup_steps_done.set(SetupStep::ENERGY_SERVICE);
}

void ISO15118_chargerImpl::handle_update_ac_max_current(double& max_current) {
    std::scoped_lock lock(GEL);

    setup_config.iso2_ac_max_current = static_cast<float>(max_current);

    if (controller) {
        controller->update_iso2_ac_max_current(static_cast<float>(max_current));
    }
}

void ISO15118_chargerImpl::handle_update_ac_parameters(types::iso15118::AcParameters& ac_parameters) {
    {
        std::scoped_lock lock(GEL);

        setup_config.ac_limits.nominal_frequency = dt::from_float(ac_parameters.nominal_frequency);
        setup_config.ac_limits.max_power_asymmetry = convert_from_optional(ac_parameters.max_power_asymmetry);
        setup_config.ac_limits.power_ramp_limitation = convert_from_optional(ac_parameters.power_ramp_limitation);

        // Exisiting values in ac_setup_config will be overwritten
        auto& ac_setup_config = setup_config.ac_setup_config.emplace();
        ac_setup_config.voltage = static_cast<uint32_t>(ac_parameters.nominal_voltage);
        for (const auto& connector : ac_parameters.connectors) {
            if (connector == types::iso15118::Connector::SinglePhase) {
                ac_setup_config.connectors.emplace_back(dt::AcConnector::SinglePhase);
            } else if (connector == types::iso15118::Connector::ThreePhase) {
                ac_setup_config.connectors.emplace_back(dt::AcConnector::ThreePhase);
            }
        }

        evse_max_reactive_power = ac_parameters.evse_max_reactive_power;

        if (controller) {
            controller->update_ac_limits(setup_config.ac_limits);
        }
    }

    // volt_base and var_base changed; re-apply DER directives on the fresh bases. Must run after GEL is
    // released (GEL is non-recursive and apply_active_der_directives snapshots it internally).
    apply_active_der_directives();
}

void ISO15118_chargerImpl::handle_update_ac_maximum_limits(types::iso15118::AcEvseMaximumPower& maximum_limits) {
    {
        std::scoped_lock lock(GEL);

        // NOTE(SL): Only the total values are used here right now. The forwarding of the individual L1, L2 and L3
        // values will come later.
        setup_config.ac_limits.charge_power.max = dt::from_float(maximum_limits.charge_power.total);

        if (maximum_limits.discharge_power.has_value()) {
            auto& discharge_power = (setup_config.ac_limits.discharge_power.has_value())
                                        ? setup_config.ac_limits.discharge_power.value()
                                        : setup_config.ac_limits.discharge_power.emplace();
            discharge_power.max = dt::from_float((mod->config.negative_bidirectional_limits)
                                                     ? -std::fabs(maximum_limits.discharge_power.value().total)
                                                     : maximum_limits.discharge_power.value().total);
        }

        if (controller) {
            controller->update_ac_limits(setup_config.ac_limits);
        }

        setup_steps_done.set(SetupStep::MAX_LIMITS);
    }

    // watt_base changed; re-apply DER directives on the fresh base. Must run after GEL is released
    // (GEL is non-recursive and apply_active_der_directives snapshots it internally).
    apply_active_der_directives();
}

void ISO15118_chargerImpl::handle_update_ac_minimum_limits(types::iso15118::AcEvseMinimumPower& minimum_limits) {
    std::scoped_lock lock(GEL);

    // NOTE(SL): Only the total values are used here right now. The forwarding of the individual L1, L2 and L3 values
    // will come later.
    setup_config.ac_limits.charge_power.min = dt::from_float(minimum_limits.charge_power.total);

    if (minimum_limits.discharge_power.has_value()) {
        auto& discharge_power = (setup_config.ac_limits.discharge_power.has_value())
                                    ? setup_config.ac_limits.discharge_power.value()
                                    : setup_config.ac_limits.discharge_power.emplace();
        discharge_power.max = dt::from_float((mod->config.negative_bidirectional_limits)
                                                 ? -std::fabs(minimum_limits.discharge_power.value().total)
                                                 : minimum_limits.discharge_power.value().total);
    }

    if (controller) {
        controller->update_ac_limits(setup_config.ac_limits);
    }

    setup_steps_done.set(SetupStep::MIN_LIMITS);
}

void ISO15118_chargerImpl::handle_update_ac_target_values(types::iso15118::AcTargetValues& target_values) {
    std::scoped_lock lock(GEL);

    if (controller) {
        iso15118::d20::AcTargetPower target_power;

        // NOTE(SL): How to add L2 and L3 values?
        target_power.target_active_power = dt::from_float(target_values.target_active_power.total);
        if (target_values.target_reactive_power.has_value()) {
            target_power.target_reactive_power = dt::from_float(target_values.target_reactive_power.value().total);
        }
        target_power.target_frequency = convert_from_optional(target_values.target_frequency);
        controller->send_control_event(target_power);
    }
}

void ISO15118_chargerImpl::handle_update_ac_present_power(types::units::Power& present_power) {
    std::scoped_lock lock(GEL);

    if (controller) {
        iso15118::d20::AcPresentPower ac_present_power;
        // NOTE(SL): Only the total values are used here right now. The forwarding of the individual L1, L2 and L3
        // values will come later.
        ac_present_power.present_active_power.emplace(dt::from_float(present_power.total));
        controller->send_control_event(ac_present_power);
    }
}

void ISO15118_chargerImpl::handle_update_dc_maximum_limits(types::iso15118::DcEvseMaximumLimits& maximum_limits) {

    std::scoped_lock lock(GEL);
    setup_config.dc_limits.charge_limits.current.max = dt::from_float(maximum_limits.evse_maximum_current_limit);
    setup_config.dc_limits.charge_limits.power.max = dt::from_float(maximum_limits.evse_maximum_power_limit);
    setup_config.dc_limits.voltage.max = dt::from_float(maximum_limits.evse_maximum_voltage_limit);

    if (maximum_limits.evse_maximum_discharge_current_limit.has_value() or
        maximum_limits.evse_maximum_discharge_power_limit.has_value()) {
        auto& discharge_limits = (setup_config.dc_limits.discharge_limits.has_value())
                                     ? setup_config.dc_limits.discharge_limits.value()
                                     : setup_config.dc_limits.discharge_limits.emplace();
        if (maximum_limits.evse_maximum_discharge_current_limit.has_value()) {
            discharge_limits.current.max =
                dt::from_float((mod->config.negative_bidirectional_limits)
                                   ? -std::fabs(maximum_limits.evse_maximum_discharge_current_limit.value())
                                   : maximum_limits.evse_maximum_discharge_current_limit.value());
        }

        if (maximum_limits.evse_maximum_discharge_power_limit.has_value()) {
            discharge_limits.power.max =
                dt::from_float((mod->config.negative_bidirectional_limits)
                                   ? -std::fabs(maximum_limits.evse_maximum_discharge_power_limit.value())
                                   : maximum_limits.evse_maximum_discharge_power_limit.value());
        }
    }

    if (controller) {
        controller->update_dc_limits(setup_config.dc_limits);
    }
}

void ISO15118_chargerImpl::handle_update_dc_minimum_limits(types::iso15118::DcEvseMinimumLimits& minimum_limits) {

    std::scoped_lock lock(GEL);
    setup_config.dc_limits.charge_limits.current.min = dt::from_float(minimum_limits.evse_minimum_current_limit);

    setup_config.dc_limits.charge_limits.power.min = dt::from_float(minimum_limits.evse_minimum_power_limit);
    setup_config.dc_limits.voltage.min = dt::from_float(minimum_limits.evse_minimum_voltage_limit);

    if (minimum_limits.evse_minimum_discharge_current_limit.has_value() or
        minimum_limits.evse_minimum_discharge_power_limit.has_value()) {

        auto& discharge_limits = (setup_config.dc_limits.discharge_limits.has_value())
                                     ? setup_config.dc_limits.discharge_limits.value()
                                     : setup_config.dc_limits.discharge_limits.emplace();
        if (minimum_limits.evse_minimum_discharge_current_limit.has_value()) {
            discharge_limits.current.min =
                dt::from_float((mod->config.negative_bidirectional_limits)
                                   ? -std::fabs(minimum_limits.evse_minimum_discharge_current_limit.value())
                                   : minimum_limits.evse_minimum_discharge_current_limit.value());
        }

        if (minimum_limits.evse_minimum_discharge_power_limit.has_value()) {
            discharge_limits.power.min =
                dt::from_float((mod->config.negative_bidirectional_limits)
                                   ? -std::fabs(minimum_limits.evse_minimum_discharge_power_limit.value())
                                   : minimum_limits.evse_minimum_discharge_power_limit.value());
        }
    }

    if (controller) {
        controller->update_dc_limits(setup_config.dc_limits);
    }
}

void ISO15118_chargerImpl::handle_update_isolation_status(types::iso15118::IsolationStatus& isolation_status) {
    // The isolation-monitoring result reaches the ISO 15118-2 / DIN SPEC 70121 SECC engines, which report
    // it as DC_EVSEStatus.EVSEIsolationStatus in the DC responses after the cable check. Above all it is
    // how No_IMD (no insulation monitoring device fitted, cable check skipped) reaches the EV -- the
    // response builders cannot derive that from the cable-check result alone. CableCheckRes keeps its own
    // progress-derived status. ISO 15118-20 has no such element and ignores this.
    auto status = iso15118::d20::IsolationStatus::Invalid;
    switch (isolation_status) {
    case types::iso15118::IsolationStatus::Invalid:
        status = iso15118::d20::IsolationStatus::Invalid;
        break;
    case types::iso15118::IsolationStatus::Valid:
        status = iso15118::d20::IsolationStatus::Valid;
        break;
    case types::iso15118::IsolationStatus::Warning:
        status = iso15118::d20::IsolationStatus::Warning;
        break;
    case types::iso15118::IsolationStatus::Fault:
        status = iso15118::d20::IsolationStatus::Fault;
        break;
    case types::iso15118::IsolationStatus::No_IMD:
        status = iso15118::d20::IsolationStatus::NoImd;
        break;
    }

    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::UpdateIsolationStatus{status});
    }
}

void ISO15118_chargerImpl::handle_update_dc_present_values(
    types::iso15118::DcEvsePresentVoltageCurrent& present_voltage_current) {

    float voltage = present_voltage_current.evse_present_voltage;
    float current = present_voltage_current.evse_present_current.value_or(0);

    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::PresentVoltageCurrent{voltage, current});
    }
}

void ISO15118_chargerImpl::handle_update_meter_info(types::powermeter::Powermeter& powermeter) {
    // Forward the latest meter reading into the active session so the charge-loop responses can report it
    // as MeterInfo (ISO 15118-2: every ChargingStatusRes, and CurrentDemandRes in PnC sessions).
    iso15118::d20::MeterInfo meter_info{};
    meter_info.meter_id = powermeter.meter_id.value_or("");
    // energy_Wh_import.total is the billing-relevant sum in Wh.
    const float energy_wh = powermeter.energy_Wh_import.total;
    meter_info.meter_reading_wh = energy_wh > 0.0f ? static_cast<uint64_t>(energy_wh) : 0U;

    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(meter_info);
    }
}

void ISO15118_chargerImpl::handle_send_error(types::iso15118::EvseError& error) {
    // Map the EvseManager error to the library's neutral EVSE error code and forward it as a control
    // event. The SECC engines stamp Malfunction / UtilityInterruptEvent (RCD -> AC RCD flag) into the DC
    // charge responses so the EV sees the fault, and abort the session on EmergencyShutdown (mirrors
    // EvseV2G's handle_send_error).
    auto code = iso15118::d20::EvseErrorCode::None;
    switch (error) {
    case types::iso15118::EvseError::Error_Contactor:
        code = iso15118::d20::EvseErrorCode::Contactor;
        break;
    case types::iso15118::EvseError::Error_RCD:
        code = iso15118::d20::EvseErrorCode::RCD;
        break;
    case types::iso15118::EvseError::Error_UtilityInterruptEvent:
        code = iso15118::d20::EvseErrorCode::UtilityInterruptEvent;
        break;
    case types::iso15118::EvseError::Error_Malfunction:
        code = iso15118::d20::EvseErrorCode::Malfunction;
        break;
    case types::iso15118::EvseError::Error_EmergencyShutdown:
        code = iso15118::d20::EvseErrorCode::EmergencyShutdown;
        // The EVSE aborts the session; report hlc_session_failed at teardown even if a graceful stop
        // was also requested. This is how a failed cable check (isolation fault) surfaces a reason.
        emergency_shutdown_requested = true;
        break;
    }

    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::EvseError{code});
    }
}

void ISO15118_chargerImpl::handle_reset_error() {
    // Clear any active EVSE error so subsequent responses report the normal status again.
    std::scoped_lock lock(GEL);
    if (controller) {
        controller->send_control_event(iso15118::d20::EvseError{iso15118::d20::EvseErrorCode::None});
    }
}

} // namespace charger
} // namespace module
