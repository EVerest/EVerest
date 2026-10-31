// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/v2gtp/sdp.hpp>

#include <iso15118/ev/d20/der/iec_functions.hpp>
#include <iso15118/ev/d20/der/sae_profile.hpp>
#include <iso15118/ev/d20/secc_clock.hpp>
#include <iso15118/ev/d20/selected_service.hpp>
#include <iso15118/ev/params/iso2_ac.hpp>

namespace iso15118::ev::config {

// Default re-poll cadence for Ongoing FSM states (e.g. DC_CableCheck): non-zero so
// the EV paces re-polls instead of busy-looping (and flooding) the SECC.
constexpr auto DEFAULT_SEND_DELAY = std::chrono::milliseconds(25);

struct WeldingDetectionConfig {
    float safe_voltage_v{60.0f};
    int max_cycles{20};
    std::chrono::milliseconds timeout{std::chrono::seconds(10)};
};

// Off: plaintext only. Prefer: request TLS in SDP, accept a plaintext offer. Require: reject an
// SDP response without TLS (no plaintext downgrade).
enum class TlsMode {
    Off,
    Prefer,
    Require,
};

// Auto: offer TLS 1.2 and 1.3 (1.2 only without a usable vehicle chain) and narrow the SAP offer
// to the negotiated version, -20 over 1.3 and -2 over 1.2. Tls1_2 / Tls1_3: exactly that version,
// SAP offer unchanged.
enum class TlsVersion {
    Auto,
    Tls1_2,
    Tls1_3,
};

struct TlsConfig {
    TlsMode mode{TlsMode::Off};
    // Ignored when mode is Off.
    TlsVersion version{TlsVersion::Auto};
    bool verify_server_certificate{true};
    // PEM files. The V2G root verifies the SECC chain and seeds trusted_ca_keys; under Auto it must
    // hold the -2 and the -20 roots.
    std::string v2g_root_cert_path;
    // Vehicle chain (leaf first) and key, TLS 1.3 only.
    std::string client_cert_chain_path;
    std::string client_key_path;
    std::string client_key_password;
    bool enable_key_logging{false};
    std::string key_logging_path;
};

// A paused session the next Controller may re-join.
struct PausedSession {
    std::array<uint8_t, 8> session_id;
    ProtocolId protocol;
    v2gtp::Security security;
    // -20 only: the resumed session continues this clock ([V2G20-1537]).
    d20::SeccClock::State secc_clock{};
    // -20 only: the selection OK_OldSessionJoined continues with. Unset when the session paused
    // before ServiceDetail selected a parameter set.
    std::optional<d20::SelectedService> selected_service{std::nullopt};
};

// ISO 15118-20 session settings, handed to the -20 engine as they are.
struct D20SessionConfig {
    // Preferred charge-loop control mode; ServiceDetail picks the parameter set matching it and falls
    // back to the first offered set.
    message_20::datatypes::ControlMode control_mode{message_20::datatypes::ControlMode::Dynamic};

    std::vector<message_20::datatypes::Authorization> supported_auth_options{message_20::datatypes::Authorization::EIM};

    // Bound on the ISO 15118-20 authorization loop, which re-sends while the EVSE reports Ongoing
    // and after a declined attempt. Zero: the ONGOING_AUTHORIZATION table value.
    std::chrono::milliseconds authorization_timeout{0};

    // IEC DER control functions the EV supports, matched in ServiceDetail. AC_DER_IEC only.
    d20::der::DerControlFunctions der_control_functions{};

    // false selects a set demanding unsupported functions anyway, which deviates from
    // [V2G20-3191]: the EV may only select AC_DER_IEC when it supports every demanded function.
    bool der_stop_on_unsupported_functions{true};

    // Static inverter description for AC_DER_SAE.
    d20::der::SaeInverterProfile sae_profile{};

    // ChargeParameterDiscovery rounds before the EV reports Processing::Finished.
    // 1 finishes on the first request.
    std::uint16_t cpd_rounds{1};

    // Structurally invalid received DER control content: true stops the session,
    // false warns and continues. Annex M does not oblige the EV to reject it.
    bool der_stop_on_invalid_control{false};
};

/**
 * Static configuration for the EV-side \ref Controller.
 */
struct EvConfig {
    // Egress interface used to send the SDP multicast request and to scope the
    // data connection (e.g. "lo", "veth-ev").
    std::string interface_name;

    // false: skip SDP and connect to direct_secc_endpoint with direct_security (tests).
    bool enable_sdp{true};
    std::optional<io::Ipv6EndPoint> direct_secc_endpoint{std::nullopt};
    v2gtp::Security direct_security{v2gtp::Security::NO_TRANSPORT_SECURITY};

    TlsConfig tls{};
    WeldingDetectionConfig welding_detection{};

    // EVCC identifier sent to the SECC.
    message_20::datatypes::Identifier evcc_id;

    // Priority-ordered protocol generations offered in the SAP request.
    std::vector<ProtocolId> supported_protocols{ProtocolId::ISO15118_20};
    std::optional<std::string> custom_protocol{std::nullopt};

    // Explicit SAP list; when non-empty it replaces the offer derived from supported_protocols,
    // energy_service and TLS (tests, custom namespaces).
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols{};

    // Energy service for this session.
    message_20::datatypes::ServiceCategory energy_service{message_20::datatypes::ServiceCategory::DC};

    D20SessionConfig d20{};

    // Re-join a paused session: constrains the SAP offer to its protocol and the SDP security byte to
    // its security; SessionSetupReq carries its id.
    std::optional<PausedSession> resume{std::nullopt};

    // ISO 15118-2 / DIN SPEC 70121 engine parameters (EVCCID MAC, energy transfer mode, AC values, PnC).
    params::EvSessionParams params{};

    // The owner reports the control pilot via Controller::set_cp_state; gates DC_CableCheck.
    bool has_cp_state_feedback{false};

    // Delay between a request becoming ready and being transmitted; events may replace it
    // within that window. Zero makes Ongoing re-poll states flood the SECC at reactor speed.
    std::chrono::milliseconds send_delay{DEFAULT_SEND_DELAY};

    // Response watchdog per request. Zero: the per-message table (ev/d20/timeouts.hpp).
    std::chrono::milliseconds response_timeout{0};

    // Security byte of the SDP request: TLS when TLS is requested, else the paused session's.
    v2gtp::Security sdp_security() const {
        if (tls.mode != TlsMode::Off) {
            return v2gtp::Security::TLS;
        }
        return resume.has_value() ? resume->security : v2gtp::Security::NO_TRANSPORT_SECURITY;
    }
};

} // namespace iso15118::ev::config
