// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/ev/der_control_functions.hpp>

namespace iso15118::ev {

// Default re-poll cadence for Ongoing FSM states (e.g. DC_CableCheck): non-zero so
// the EV paces re-polls instead of busy-looping (and flooding) the SECC.
constexpr auto DEFAULT_SEND_DELAY = std::chrono::milliseconds(25);

/**
 * Static configuration for the EV-side \ref Controller.
 *
 * Carries the egress interface, how the SECC endpoint is resolved (SDP
 * discovery vs. a fixed endpoint), the transport security advertised in the SDP
 * exchange, the EVCC identifier surfaced to the SECC in the SessionSetupRequest,
 * and the SupportedAppProtocol list advertised in the SAP request.
 */
struct EvConfig {
    // Egress interface used to send the SDP multicast request and to scope the
    // data connection (e.g. "lo", "veth-ev").
    std::string interface_name;

    // true  -> resolve the SECC endpoint via SDP discovery.
    // false -> use fixed_endpoint (which must then be set).
    bool discover{true};

    // The SECC endpoint to use when discover == false.
    std::optional<io::Ipv6EndPoint> fixed_endpoint;

    // Transport security advertised in the SDP request. The EV connects over plain
    // TCP today regardless (no libio TLS client yet); this only drives the security
    // byte the SECC sees in the request.
    io::v2gtp::Security advertised_security{io::v2gtp::Security::NO_TRANSPORT_SECURITY};

    // EVCC identifier sent to the SECC.
    message_20::datatypes::Identifier evcc_id;

    // SupportedAppProtocol list advertised in the SAP request. Defaults to the
    // single ISO 15118-20 DC entry; only -20 is wired today.
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols{
        {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};

    // Energy service for this session; advertised_app_protocols must carry the matching SAP namespace.
    message_20::datatypes::ServiceCategory energy_service{message_20::datatypes::ServiceCategory::DC};

    // Delay between a request becoming ready (FSM-produced, or the first request
    // on connect) and it being transmitted. During this window events may replace
    // the pending request. Zero transmits on the next reactor pass, which makes
    // Ongoing re-poll states re-send at reactor speed and flood the SECC; the
    // default paces re-polls. Set 0 to opt into transmit-ASAP.
    std::chrono::milliseconds send_delay{DEFAULT_SEND_DELAY};

    // How long to wait for a response after a request is sent before the session
    // is failed (the response watchdog).
    std::chrono::milliseconds response_timeout{20000};

    // IEC DER control functions the EV declares support for; negotiated against the
    // SECC's AC_DER_IEC parameter sets in ServiceDetail. Only used for AC_DER_IEC.
    DerControlFunctions der_control_functions{};

    // true  -> stop the session when no offered AC_DER_IEC Dynamic set is a subset of
    //          der_control_functions.
    // false -> select the first Dynamic set anyway and warn about the unsupported
    //          functions.
    bool der_stop_on_unsupported_functions{true};
};

} // namespace iso15118::ev
