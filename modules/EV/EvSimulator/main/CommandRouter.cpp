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

std::vector<Everest::UnsubscribeToken> setup_command_router(EvSimRuntime& rt, EvSimulator& mod) {
    const auto& topics = mod.get_topics();
    auto& mqtt = mod.mqtt;

    std::vector<Everest::UnsubscribeToken> tokens;
    tokens.reserve(17);

    // Common wrapper: resolves the topic, wraps the user handler in a
    // try/catch. Handler failures are logged at error and surfaced via a
    // CommandAck Rejected on the response topic so the caller sees feedback.
    // The catch must be total: this lambda runs on the framework's single
    // serial external_mqtt_worker_thread, flushed by wait_and_pop(). An
    // escaping exception (std or otherwise) ends that worker thread, after
    // which every subsequent MQTT command for the whole module is silently
    // dropped — strictly worse than swallowing one bad message. So a
    // non-std::exception is logged and contained too, never rethrown.
    // The UnsubscribeToken returned by `mqtt.subscribe` is captured into
    // `tokens` and invoked by ~EvSimRuntime so the handler stops firing
    // before the runtime references it captures are torn down.
    auto subscribe = [&](const std::string& var, auto handler) {
        const auto topic = topics.extern_to_everest(var);
        tokens.push_back(mqtt.subscribe(topic, [&rt, topic, var, handler](const std::string& payload) {
            try {
                handler(payload);
            } catch (const std::exception& e) {
                EVLOG_error << "Topic: '" << topic << "' (" << var << ") failed -> " << e.what() << " => " << payload;
                if (auto* ctx = rt.ctx_ptr()) {
                    ctx->publish_e2m_command_ack(var, e.what());
                }
            } catch (...) {
                EVLOG_error << "Topic: '" << topic << "' (" << var << ") failed -> unknown exception => " << payload;
                if (auto* ctx = rt.ctx_ptr()) {
                    ctx->publish_e2m_command_ack(var, "unknown exception");
                }
            }
        }));
    };

    // ---- enable/disable -----------------------------------------------------
    // `bool` lives in the generic codec — the per-domain ev_simulator codec
    // only specializes domain types.
    subscribe("enable", [&rt](const std::string& payload) {
        bool v = false;
        if (!API_generic::adl_deserialize(payload, v)) {
            throw std::runtime_error("bad enable payload");
        }
        if (v) {
            rt.enqueue(Event{EnableCmd{}});
        } else {
            rt.enqueue(Event{DisableCmd{}});
        }
    });

    // ---- parameterless verbs -----------------------------------------------
    auto sub_void = [&](const std::string& var, EventPayload payload) {
        subscribe(var, [&rt, payload](const std::string&) { rt.enqueue(Event{payload}); });
    };
    sub_void("plug", PlugCmd{});
    sub_void("unplug", UnplugCmd{});
    sub_void("stop_session", StopSessionCmd{});
    sub_void("pause_session", PauseSessionCmd{});
    sub_void("resume_session", ResumeSessionCmd{});
    sub_void("clear_fault", ClearFaultCmd{});
    sub_void("query_state", QueryStateCmd{});

    // ---- param-bearing verbs ------------------------------------------------
    subscribe("set_soc", [&rt](const std::string& payload) {
        API_evsim::SetSocParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad set_soc payload");
        }
        rt.enqueue(Event{p});
    });
    subscribe("configure_session", [&rt](const std::string& payload) {
        API_evsim::SessionConfigParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad configure_session payload");
        }
        rt.enqueue(Event{p});
    });
    subscribe("set_charging_current", [&rt](const std::string& payload) {
        API_evsim::SetChargingCurrentParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad set_charging_current payload");
        }
        rt.enqueue(Event{p});
    });
    subscribe("inject_fault", [&rt](const std::string& payload) {
        API_evsim::InjectFaultParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad inject_fault payload");
        }
        rt.enqueue(Event{p});
    });
    subscribe("bcb_toggle", [&rt](const std::string& payload) {
        API_evsim::BcbToggleParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad bcb_toggle payload");
        }
        rt.enqueue(Event{p});
    });
    subscribe("run_scenario", [&rt](const std::string& payload) {
        API_evsim::RunScenarioParams p;
        if (!API_evsim::adl_deserialize(payload, p)) {
            throw std::runtime_error("bad run_scenario payload");
        }
        rt.enqueue(Event{p});
    });

    // ---- direct (non-queue) handlers ---------------------------------------
    subscribe("communication_check", [&mod](const std::string& payload) {
        bool v = false;
        if (!API_generic::adl_deserialize(payload, v)) {
            throw std::runtime_error("bad communication_check payload");
        }
        mod.comm_check.set_value(v);
    });

    // raise_error / clear_error parse here on the MQTT thread but do NOT touch
    // p_ev_manager: the parsed args are enqueued and the actual error
    // interaction happens on the loop thread (which owns all peer/error
    // publishing), so it cannot interleave with Faulted::enter's fault
    // publishing.
    subscribe("raise_error", [&rt](const std::string& payload) {
        API_generic::Error err;
        if (!API_generic::adl_deserialize(payload, err)) {
            throw std::runtime_error("bad raise_error payload");
        }
        RaiseErrorCmd cmd;
        cmd.type = make_error_string(err);
        cmd.sub_type = err.sub_type.value_or("");
        cmd.message = err.message.value_or("");
        cmd.severity = Everest::error::Severity::High;
        rt.enqueue(Event{cmd});
    });
    subscribe("clear_error", [&rt](const std::string& payload) {
        API_generic::Error err;
        if (!API_generic::adl_deserialize(payload, err)) {
            throw std::runtime_error("bad clear_error payload");
        }
        ClearErrorCmd cmd;
        cmd.type = make_error_string(err);
        cmd.sub_type = err.sub_type;
        rt.enqueue(Event{cmd});
    });

    return tokens;
}

} // namespace module
