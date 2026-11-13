// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

#include <everest/slac/telemetry.hpp>

namespace module::main {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

// Everything the SLAC runtime tells the outside world goes through this. The module implements it
// on top of the generated interface (publish_*, raise_error, EVLOG); tests record the calls.
class SlacSink {
public:
    virtual ~SlacSink() = default;

    virtual void publish_state(everest::lib::slac::D3State state) = 0;
    virtual void publish_dlink_ready(bool ready) = 0;
    virtual void publish_ev_mac_address(std::string const& mac) = 0;
    virtual void request_error_routine() = 0;

    // \p type is the interface error type, e.g. "generic/CommunicationFault".
    virtual void raise_fault(std::string const& type, std::string const& sub_type, std::string const& message) = 0;
    virtual void clear_fault(std::string const& type) = 0;

    // Telemetry from the state machine; the module encodes and publishes it.
    virtual void publish_telemetry(everest::lib::slac::SlacFsmState const& fsm_state,
                                   everest::lib::slac::SlacTelemetry const& status) = 0;

    virtual void log(LogLevel level, std::string const& text) = 0;
};

} // namespace module::main
