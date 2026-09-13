// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

#include <iso15118/ev/der_control_functions.hpp>

namespace iso15118::ev {

// Default re-poll cadence for Ongoing FSM states (e.g. DC_CableCheck): non-zero so
// the EV paces re-polls instead of busy-looping (and flooding) the SECC.
constexpr auto DEFAULT_SEND_DELAY = std::chrono::milliseconds(25);

struct TlsConfig {
    // Request TLS in the SDP request and connect with TLS when the SECC offers it.
    bool use_tls{false};
    // Reject SDP responses without TLS (no plaintext downgrade). Implies use_tls.
    bool enforce_tls{false};
    // true: TLS 1.3 with client certificate (ISO 15118-20). false: TLS 1.2, cipher
    // ECDHE-ECDSA-AES128-SHA256, no client certificate (ISO 15118-2).
    bool enable_tls_1_3{false};
    bool verify_server_certificate{true};
    // PEM files. The V2G root verifies the SECC chain and seeds trusted_ca_keys.
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
    io::v2gtp::Security security;
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
    io::v2gtp::Security direct_security{io::v2gtp::Security::NO_TRANSPORT_SECURITY};

    TlsConfig tls{};

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

    // Preferred charge-loop control mode.
    message_20::datatypes::ControlMode control_mode{message_20::datatypes::ControlMode::Dynamic};

    std::vector<message_20::datatypes::Authorization> supported_auth_options{message_20::datatypes::Authorization::EIM};

    // Re-join a paused session: constrains the SAP offer to its protocol and the SDP security byte to
    // its security; SessionSetupReq carries its id.
    std::optional<PausedSession> resume{std::nullopt};

    // The owner reports the control pilot via Controller::set_cp_state; gates DC_CableCheck.
    bool has_cp_state_feedback{false};

    // Delay between a request becoming ready and being transmitted; events may replace it
    // within that window. Zero makes Ongoing re-poll states flood the SECC at reactor speed.
    std::chrono::milliseconds send_delay{DEFAULT_SEND_DELAY};

    // Response watchdog per request. Zero: the per-message table (ev/d20/timeouts.hpp).
    std::chrono::milliseconds response_timeout{0};

    // IEC DER control functions the EV declares support for; negotiated against the
    // SECC's AC_DER_IEC parameter sets in ServiceDetail. Only used for AC_DER_IEC.
    DerControlFunctions der_control_functions{};

    // true  -> stop the session when no offered AC_DER_IEC Dynamic set is a subset of
    //          der_control_functions.
    // false -> select the first Dynamic set anyway and warn about the unsupported
    //          functions.
    bool der_stop_on_unsupported_functions{true};

    // Security byte of the SDP request: TLS when TLS is requested, else the paused session's.
    io::v2gtp::Security sdp_security() const {
        if (tls.use_tls or tls.enforce_tls) {
            return io::v2gtp::Security::TLS;
        }
        return resume.has_value() ? resume->security : io::v2gtp::Security::NO_TRANSPORT_SECURITY;
    }
};

} // namespace iso15118::ev
