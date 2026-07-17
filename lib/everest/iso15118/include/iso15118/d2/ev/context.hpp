// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <iso15118/d2/context.hpp> // for d2::MessageExchange
#include <iso15118/d20/timeout.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/variant.hpp>
#include <iso15118/session/ev_feedback.hpp>
#include <iso15118/session/logger.hpp>

#include <everest/util/vector/fixed_vector.hpp>

#include "config.hpp"
#include "control_event.hpp"

namespace iso15118::d2::ev {

namespace dt = message_2::datatypes;

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// Aggregates the information the EV received from the SECC during the handshake.
struct EvseInfo {
    std::string evse_id;

    // Chosen ChargeService (from ServiceDiscoveryRes).
    uint16_t selected_charge_service_id{0};

    // From ChargeParameterDiscoveryRes.
    uint8_t sa_schedule_tuple_id{0};
    std::optional<dt::PMaxScheduleEntry> selected_pmax_entry{std::nullopt};
    // Full PMaxSchedule of the selected SAScheduleTuple, used to build the AC ChargingProfile.
    everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12> selected_pmax_schedule{};

    // SECC-advertised limits, populated in ChargeParameterDiscovery.
    std::optional<session::ev::feedback::DcMaximumLimits> dc_present_limits{std::nullopt};
    std::optional<dt::PhysicalValue> ac_nominal_voltage{std::nullopt};
    std::optional<dt::PhysicalValue> ac_max_current{std::nullopt};
};

class Context {
public:
    Context(session::ev::feedback::Callbacks, session::SessionLogger&, EvSessionConfig,
            const std::optional<ControlEvent>&, d2::MessageExchange&, d20::Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    // --- send path (EV -> SECC): the outgoing request goes into the response slot of the exchange ---
    template <typename RequestType> void send_request(const RequestType& msg) {
        message_exchange.set_response(msg);
    }

    // --- receive path (SECC -> EV): the incoming decoded message is stored in the request slot ---
    std::unique_ptr<message_2::Variant> pull_response();
    message_2::Type peek_response_type() const;

    template <typename T> T const* get_control_event() {
        if (not current_control_event.has_value()) {
            return nullptr;
        }
        if (not std::holds_alternative<T>(*current_control_event)) {
            return nullptr;
        }
        return &std::get<T>(*current_control_event);
    }

    // Fills the request header with the current session id.
    void setup_header(message_2::Header& header) const;

    void set_session_id(const dt::SessionId& id) {
        session_id = id;
    }

    const dt::SessionId& get_session_id() const {
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

    EvSessionConfig session_config;

    EvseInfo evse_info;

    bool session_stopped{false};
    bool session_paused{false};

    // Set when a stop/pause control event is received; checked at transition time so the state machine
    // can divert to SessionStop after the current exchange completes.
    std::optional<dt::ChargingSession> pending_stop{std::nullopt};

    // Cached control-event values consumed by the DC charging-loop states.
    struct DcControlCache {
        float present_voltage{0.0f};
        float present_current{0.0f};
        std::optional<float> target_voltage{std::nullopt};
        std::optional<float> target_current{std::nullopt};
        uint8_t present_soc{0};
        std::optional<float> max_energy_request{std::nullopt};
        std::optional<float> max_charge_power{std::nullopt};
        std::optional<float> max_charge_current{std::nullopt};
    } dc_cache;

private:
    const std::optional<ControlEvent>& current_control_event;
    d2::MessageExchange& message_exchange;

    // The SessionSetupReq carries an all-zero session id (or the resumed id); the SECC-assigned id is
    // stored after the response.
    dt::SessionId session_id{};

    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};
};

} // namespace iso15118::d2::ev
