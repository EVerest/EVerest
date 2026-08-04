// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <iostream>
#include <optional>

#include <everest/util/fsm/fsm.hpp>
#include <iso15118/d20/config.hpp>
#include <iso15118/d20/context.hpp>
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/states.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/detail/cb_exi.hpp>
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

    d20::EvseSetupConfig setup{};
    setup.evse_id = evse_id;
    setup.supported_energy_services = supported_energy_services;
    setup.authorization_services = auth_services;
    setup.supported_vas_services = vas_services;
    setup.enable_certificate_install_service = cert_install;
    setup.dc_limits = dc_limits;
    setup.ac_limits = ac_limits;
    setup.der_limits = std::nullopt;
    setup.control_mobility_modes = control_mobility_modes;
    setup.powersupply_limits = powersupply_limits;
    return setup;
}

class FsmStateHelper {
public:
    FsmStateHelper(const d20::SessionConfig& config, std::optional<d20::PauseContext>& pause_ctx_,
                   const session::feedback::Callbacks& callbacks) :
        ctx(callbacks, config, pause_ctx_, active_control_event, msg_exch, timeouts) {

        io::set_logging_callback([](LogLevel level, std::string message) {
            printf("log(%d): %s\n", static_cast<int>(level), message.c_str());
        });
    };

    d20::Context& get_context();

    template <typename RequestType> void handle_request(const RequestType& request) {
        msg_exch.set_request(std::make_unique<message_20::Variant>(request));
    }

    void set_active_control_event(const std::optional<d20::ControlEvent>& event) {
        active_control_event = event;
    }

private:
    std::array<uint8_t, 1024> output_buffer{};
    io::StreamOutputView output_stream_view{output_buffer.data(), output_buffer.size()};

    d20::MessageExchange msg_exch{output_stream_view};
    std::optional<d20::ControlEvent> active_control_event;

    d20::Timeouts timeouts;

    d20::Context ctx;
};
