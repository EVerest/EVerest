// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include <iso15118/d20/context.hpp> // for d20::MessageExchange
#include <iso15118/d20/session.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/config.hpp>
#include <iso15118/session/ev_feedback.hpp>
#include <iso15118/session/logger.hpp>

#include "config.hpp"
#include "control_event.hpp"

namespace iso15118::d20::ev {

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// Aggregates the information the EV received from the SECC during the handshake.
struct EvseInfo {
    std::string evse_id;

    // From ServiceDiscoveryRes
    message_20::datatypes::ServiceList offered_energy_services;
    std::optional<message_20::datatypes::VasServiceList> offered_vas_services;

    // Parameter sets received from ServiceDetailRes, keyed by service id.
    std::map<uint16_t, message_20::datatypes::ServiceParameterList> offered_parameter_sets;

    // Received EVSE limits (populated in the charge parameter discovery states).
    std::optional<session::ev::feedback::DcMaximumLimits> dc_present_limits;
    std::optional<AcMaximumLimits> ac_present_limits;

    // Chosen during the handshake.
    message_20::datatypes::ServiceCategory selected_energy_service{};
    std::uint16_t selected_parameter_set_id{0};
    message_20::datatypes::ControlMode selected_control_mode{message_20::datatypes::ControlMode::Dynamic};
    message_20::datatypes::MobilityNeedsMode selected_mobility_needs_mode{
        message_20::datatypes::MobilityNeedsMode::ProvidedByEvcc};
};

class Context {
public:
    Context(session::ev::feedback::Callbacks, session::SessionLogger&, session::EvSessionConfig,
            const std::optional<ControlEvent>&, d20::MessageExchange&, Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    // --- send path (EV -> SECC): the outgoing request goes into the response slot of the exchange ---
    template <typename RequestType> void send_request(const RequestType& msg) {
        message_exchange.set_response(msg);
    }

    // --- receive path (SECC -> EV): the incoming decoded message is stored in the request slot ---
    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

    const auto& get_control_event() {
        return current_control_event;
    }

    template <typename T> T const* get_control_event() {
        if (not current_control_event.has_value()) {
            return nullptr;
        }
        if (not std::holds_alternative<T>(*current_control_event)) {
            return nullptr;
        }
        return &std::get<T>(*current_control_event);
    }

    // Fills the request header with the current session id and a fresh timestamp.
    void setup_header(message_20::Header& header) const;

    void set_session_id(const message_20::datatypes::SessionId& id) {
        session_id = id;
    }

    const message_20::datatypes::SessionId& get_session_id() const {
        return session_id;
    }

    void start_timeout(d20::TimeoutType type, uint32_t time_ms) {
        timeouts.start_timeout(type, time_ms);
    }

    void stop_timeout(d20::TimeoutType type) {
        timeouts.stop_timeout(type);
    }

    d20::TimeoutType const* get_active_timeout() {
        if (not current_timeout.has_value()) {
            return nullptr;
        }
        return &current_timeout.value();
    }

    void set_active_timeout(d20::TimeoutType timeout) {
        current_timeout = timeout;
    }

    const session::ev::Feedback feedback;

    session::SessionLogger& log;

    session::EvSessionConfig session_config;

    // Re-used for SelectedServiceParameters storage (the session id is tracked separately, see session_id).
    d20::Session session;

    EvseInfo evse_info;

    bool session_stopped{false};
    bool session_paused{false};

    // Set when a stop/pause control event is received during the handshake. Checked at transition time
    // so the state machine can divert to the SessionStop state after the current exchange completes.
    std::optional<message_20::datatypes::ChargingSession> pending_stop{std::nullopt};

    // Cached control-event values consumed by the DC charging-loop states. Updated on CONTROL_MESSAGE
    // (see handle_dc_control_event) so create_request can read the latest set points.
    struct DcControlCache {
        // Present voltage/current reported by the charger/simulator (PresentVoltageCurrent).
        float present_voltage{0.0f};
        float present_current{0.0f};
        // DC set points (UpdateDcTargets); fall back to the configured targets when unset.
        std::optional<float> target_voltage{std::nullopt};
        std::optional<float> target_current{std::nullopt};
        // Present state of charge 0..100 (UpdateSoc).
        uint8_t present_soc{0};
        // Dynamic energy-request window updates (UpdateDcParameters).
        std::optional<float> target_energy_request{std::nullopt};
        std::optional<float> max_energy_request{std::nullopt};
        std::optional<float> min_energy_request{std::nullopt};
        std::optional<float> max_charge_power{std::nullopt};
        std::optional<float> max_charge_current{std::nullopt};
    } dc_cache;

private:
    const std::optional<ControlEvent>& current_control_event;
    d20::MessageExchange& message_exchange;

    // The SessionSetupReq must carry an all-zero session id; the SECC-assigned id is stored after the response.
    message_20::datatypes::SessionId session_id{};

    Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};
};

} // namespace iso15118::d20::ev
