// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <iostream>
#include <optional>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/context.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/states.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/fsm/fsm.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/feedback.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

inline d20::EvseSetupConfig create_default_evse_setup() {
    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::DC};
    const auto cert_install = false;
    const std::vector<uint16_t> vas_services{}; // TODO(SL): Add Custom service
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const d20::DcTransferLimits dc_limits;
    const d20::AcTransferLimits ac_limits;
    const d20::DcTransferLimits powersupply_limits;
    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};

    return d20::EvseSetupConfig{
        evse_id, supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, control_mobility_modes, std::nullopt, std::nullopt, std::nullopt, powersupply_limits};
}

class FsmStateHelper {
public:
    FsmStateHelper(const d20::SessionConfig& config, std::optional<d20::PauseContext>& pause_ctx_,
                   const session::feedback::Callbacks& callbacks) :
        log(this), ctx(callbacks, log, config, pause_ctx_, active_control_event, msg_exch, timeouts) {

        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event& event) {
            if (const auto* simple_event = std::get_if<session::logging::SimpleEvent>(&event)) {
                printf("log(session: simple event): %s\n", simple_event->info.c_str());
            } else {
                printf("log(session): not decoded\n");
            }
        });

        io::set_logging_callback([](LogLevel level, std::string message) {
            printf("log(%d): %s\n", static_cast<int>(level), message.c_str());
        });
    };

    d20::Context& get_context();

    template <typename RequestType> void handle_request(const RequestType& request) {
        msg_exch.set_request(std::make_unique<message_20::Variant>(request));
    }

private:
    std::array<uint8_t, 1024> output_buffer{};
    io::StreamOutputView output_stream_view{output_buffer.data(), output_buffer.size()};

    d20::MessageExchange msg_exch{output_stream_view};
    std::optional<d20::ControlEvent> active_control_event;

    session::SessionLogger log;

    d20::Timeouts timeouts;

    d20::Context ctx;
};
