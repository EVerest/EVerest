// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "CommandRouter.hpp"

#include "Events.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/generic/string.hpp>
#include <utils/error.hpp>

#include <exception>
#include <stdexcept>
#include <string>

namespace module {

namespace {

namespace API_types = ev_API::V1_0::types;
namespace API_generic = API_types::generic;
namespace API_evsim = API_types::ev_simulator;

// Local mirror of `ev_board_support_API::make_error_string` — keeps the
// router self-contained instead of pulling in a dependency on a non-library
// helper.
std::string make_error_string(API_generic::Error const& err) {
    return "generic/" + API_generic::trimmed(API_generic::serialize(err.type));
}

} // namespace

void setup_command_router(EvSimRuntime& rt, EvSimulator& mod) {
    const auto& topics = mod.get_topics();
    auto& mqtt = mod.mqtt;

    // Common wrapper: resolves the topic, wraps the user handler in a
    // try/catch and swallows decode failures with a warning. The
    // UnsubscribeToken returned by `mqtt.subscribe` is intentionally
    // discarded — subscriptions live for the lifetime of the module, same
    // convention as the EVerestAPI subscribe_api_topic helper.
    auto subscribe = [&](const std::string& var, auto handler) {
        const auto topic = topics.extern_to_everest(var);
        mqtt.subscribe(topic, [topic, var, handler](const std::string& payload) {
            try {
                handler(payload);
            } catch (const std::exception& e) {
                EVLOG_warning << "Topic: '" << topic << "' (" << var << ") failed -> " << e.what() << " => " << payload;
            } catch (...) {
                EVLOG_warning << "Topic: '" << topic << "' (" << var << ") failed with unknown exception";
            }
        });
    };

    // ---- enable/disable -----------------------------------------------------
    // `bool` lives in the generic codec — the per-domain ev_simulator codec
    // only specializes domain types.
    subscribe("enable", [&rt](const std::string& payload) {
        bool v = false;
        if (!API_generic::adl_deserialize(payload, v)) {
            throw std::runtime_error("bad enable payload");
        }
        rt.enqueue(Event{v ? EventKind::Enable : EventKind::Disable, {}});
    });

    // ---- parameterless verbs -----------------------------------------------
    auto sub_void = [&](const std::string& var, EventKind k) {
        subscribe(var, [&rt, k](const std::string&) { rt.enqueue(Event{k, {}}); });
    };
    sub_void("plug", EventKind::Plug);
    sub_void("unplug", EventKind::Unplug);
    sub_void("stop_session", EventKind::StopSession);
    sub_void("pause_session", EventKind::PauseSession);
    sub_void("resume_session", EventKind::ResumeSession);
    sub_void("clear_fault", EventKind::ClearFault);
    sub_void("query_state", EventKind::QueryState);

    // ---- param-bearing verbs ------------------------------------------------
    subscribe("set_soc", [&rt](const std::string& payload) {
        API_evsim::SetSocParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad set_soc payload");
        }
        rt.enqueue(Event{EventKind::SetSoc, p});
    });
    subscribe("start_session", [&rt](const std::string& payload) {
        API_evsim::StartSessionParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad start_session payload");
        }
        rt.enqueue(Event{EventKind::StartSession, p});
    });
    subscribe("set_charging_current", [&rt](const std::string& payload) {
        API_evsim::SetChargingCurrentParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad set_charging_current payload");
        }
        rt.enqueue(Event{EventKind::SetChargingCurrent, p});
    });
    subscribe("inject_fault", [&rt](const std::string& payload) {
        API_evsim::InjectFaultParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad inject_fault payload");
        }
        rt.enqueue(Event{EventKind::InjectFault, p});
    });
    subscribe("bcb_toggle", [&rt](const std::string& payload) {
        API_evsim::BcbToggleParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad bcb_toggle payload");
        }
        rt.enqueue(Event{EventKind::BcbToggle, p});
    });
    subscribe("run_scenario", [&rt](const std::string& payload) {
        API_evsim::RunScenarioParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad run_scenario payload");
        }
        rt.enqueue(Event{EventKind::RunScenario, p});
    });

    // ---- direct (non-queue) handlers ---------------------------------------
    subscribe("communication_check", [&mod](const std::string& payload) {
        bool v = false;
        if (!API_generic::adl_deserialize(payload, v)) {
            throw std::runtime_error("bad communication_check payload");
        }
        mod.comm_check.set_value(v);
    });
    subscribe("raise_error", [&mod](const std::string& payload) {
        API_generic::Error err;
        if (!API_generic::adl_deserialize(payload, err)) {
            throw std::runtime_error("bad raise_error payload");
        }
        const auto sub = err.sub_type.value_or("");
        const auto msg = err.message.value_or("");
        const auto type = make_error_string(err);
        auto ev_error = mod.p_ev_manager->error_factory->create_error(type, sub, msg, Everest::error::Severity::High);
        mod.p_ev_manager->raise_error(ev_error);
    });
    subscribe("clear_error", [&mod](const std::string& payload) {
        API_generic::Error err;
        if (!API_generic::adl_deserialize(payload, err)) {
            throw std::runtime_error("bad clear_error payload");
        }
        const auto type = make_error_string(err);
        if (err.sub_type) {
            mod.p_ev_manager->clear_error(type, *err.sub_type);
        } else {
            mod.p_ev_manager->clear_error(type);
        }
    });
}

} // namespace module
