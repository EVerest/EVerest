// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/slac/msm/slac_evse_fsm.hpp>
#include <everest/slac/slac_fsm.hpp>
// clang-format off
#include <boost/msm/back/tools.hpp>
#include <type_traits>
#include "fsm/misc.hpp"
#include "nlohmann/json.hpp"
// clang-format on

namespace everest::lib::slac {

namespace {
inline msm::SlacFSM* to_msm(void* ptr) {
    return reinterpret_cast<msm::SlacFSM*>(ptr);
}

template <typename T, typename = void> struct is_submachine : std::false_type {};
template <typename T>
struct is_submachine<T, std::void_t<decltype(std::declval<T>().current_state())>> : std::true_type {};
template <typename T, typename = void> struct has_sessions : std::false_type {};
template <typename T> struct has_sessions<T, std::void_t<decltype(std::declval<T>().sessions)>> : std::true_type {};
template <typename T> T extract_wrap_type(boost::msm::wrap<T>);

std::string simplify_state_name(std::string name) {
    auto sm_prefix = std::string("boost::msm::back::state_machine<");
    auto start_pos = name.find(sm_prefix);

    if (start_pos != std::string::npos) {
        // relevant type is the first template parameter
        start_pos += sm_prefix.length();
        auto end_pos = name.find(',', start_pos);
        if (end_pos != std::string::npos) {
            name = name.substr(start_pos, end_pos - start_pos);
        }
    }

    // strip namespace
    auto last_colon = name.rfind("::");
    if (last_colon != std::string::npos) {
        name = name.substr(last_colon + 2);
    }

    return name;
}

template <class FSM, class Visitor> void traverse_fsm(FSM const& fsm, size_t depth, Visitor& visitor) {
    // 1. Traverse orthogonal regions
    for (auto region_id = 0U; region_id < FSM::nr_regions::value; ++region_id) {
        using Stt = typename FSM::stt;
        using all_states = typename boost::msm::back::generate_state_set<Stt>::type;

        auto active_id = fsm.current_state()[region_id];
        auto found = false;

        boost::mpl::for_each<all_states, boost::msm::wrap<boost::mpl::placeholders::_1>>([&](auto wrap) {
            if (found)
                return;

            using StateType = decltype(extract_wrap_type(wrap));
            auto this_state_id = boost::msm::back::get_state_id<Stt, StateType>::value;

            if (this_state_id == active_id) {
                found = true;
                visitor.template on_active_state<StateType>(active_id, depth);

                if constexpr (is_submachine<StateType>::value) {
                    auto const& sub_fsm = fsm.template get_state<StateType const&>();

                    // NEW: Submachine scope hooks
                    visitor.template on_submachine_start<StateType>(active_id, depth);
                    traverse_fsm(sub_fsm, depth + 1, visitor);
                    visitor.template on_submachine_end<StateType>(active_id, depth);
                }
            }
        });
    }

    // 2. Traverse dynamic sessions
    if constexpr (has_sessions<FSM>::value) {
        visitor.on_dynamic_sessions_size(fsm.sessions.size());

        int idx = 0;
        for (auto const& session : fsm.sessions) {
            visitor.on_dynamic_session_start(idx, depth);
            traverse_fsm(session, depth + 1, visitor);

            // NEW: Session scope hook
            visitor.on_dynamic_session_end(idx++, depth);
        }
    }
}

struct SignatureVisitor {
    std::vector<int>& sig;

    template <typename StateType> void on_active_state(int state_id, size_t /*depth*/) {
        sig.push_back(state_id);
    }

    void on_dynamic_sessions_size(size_t size) {
        sig.push_back(static_cast<int>(size));
    }

    void on_dynamic_session_start(int /*index*/, size_t /*depth*/) {
        // No action needed for the signature
    }

    template <typename StateType> void on_submachine_start(int, size_t) {
    }
    template <typename StateType> void on_submachine_end(int, size_t) {
    }
    void on_dynamic_session_end(int, size_t) {
    }
};

struct PrintVisitor {
    std::string& output;

    template <typename StateType> void on_active_state(int /*state_id*/, size_t depth) {
        std::string indent(depth * 4, ' ');
        std::string clean_name = simplify_state_name(boost::core::demangle(typeid(StateType).name()));
        output += indent + " -> " + clean_name + "\n";
    }

    void on_dynamic_sessions_size(size_t /*size*/) {
        // No action needed for printing
    }

    void on_dynamic_session_start(int index, size_t depth) {
        std::string indent(depth * 4, ' ');
        output += indent + " * [Dynamic Session " + std::to_string(index) + "]\n";
    }
    template <typename StateType> void on_submachine_start(int, size_t) {
    }
    template <typename StateType> void on_submachine_end(int, size_t) {
    }
    void on_dynamic_session_end(int, size_t) {
    }
};

struct JsonVisitor {
    std::vector<nlohmann::json> stack;
    std::string last_visited_state; // Remembers the state name for nesting

    JsonVisitor() {
        // Initialize the root JSON object on the stack
        stack.push_back(nlohmann::json::object());
    }

    template <typename StateType> void on_active_state(int /*state_id*/, size_t /*depth*/) {
        std::string clean_name = simplify_state_name(boost::core::demangle(typeid(StateType).name()));

        if (!stack.back().contains("states")) {
            stack.back()["states"] = nlohmann::json::array();
        }
        stack.back()["states"].push_back(clean_name);

        // Save the name in case this state immediately becomes a submachine
        last_visited_state = clean_name;
    }

    template <typename StateType> void on_submachine_start(int /*state_id*/, size_t /*depth*/) {
        // Push a fresh context for the submachine
        stack.push_back(nlohmann::json::object());
    }

    template <typename StateType> void on_submachine_end(int /*state_id*/, size_t /*depth*/) {
        // Pop the finished submachine context
        auto child = stack.back();
        stack.pop_back();

        // Attach it to the parent under the "submachines" object
        if (!stack.back().contains("submachines")) {
            stack.back()["submachines"] = nlohmann::json::object();
        }
        stack.back()["submachines"][last_visited_state] = child;
    }

    void on_dynamic_sessions_size(size_t size) {
        if (size > 0 && !stack.back().contains("sessions")) {
            stack.back()["sessions"] = nlohmann::json::array();
        }
    }

    void on_dynamic_session_start(int /*index*/, size_t /*depth*/) {
        stack.push_back(nlohmann::json::object());
    }

    void on_dynamic_session_end(int /*index*/, size_t /*depth*/) {
        auto child = stack.back();
        stack.pop_back();
        // Append the finished session to the parent's session array
        stack.back()["sessions"].push_back(child);
    }

    nlohmann::json get_result() const {
        return stack.front();
    }
};

} // namespace

void slac_fsm::event_post_processing() {
    auto print = ctx.slac_config.print_state_transitions;
    auto telemetry = ctx.slac_config.provide_telemetry;

    if (print or telemetry) {
        auto const& msm = *to_msm(fsm);
        std::vector<int> current_signature;
        current_signature.reserve(last_signature.capacity());

        SignatureVisitor sig_visitor{current_signature};
        traverse_fsm(msm, 0, sig_visitor);

        if (current_signature != last_signature) {
            if (print) {
                std::string current_state_str;
                PrintVisitor print_visitor{current_state_str};
                traverse_fsm(msm, 0, print_visitor);

                std::cout << current_state_str;
                std::cout << "-----------------------\n";
            }
            if (telemetry) {
                JsonVisitor json_visitor;
                traverse_fsm(msm, 0, json_visitor);
                ctx.telemetry("FSM", "state", json_visitor.get_result().dump());

                ctx.telemetry("generic", "status", serialize(ctx.status));
            }
            last_signature = std::move(current_signature);
        }
    }
}

slac_fsm::slac_fsm(fsm::evse::Context& ctx) : ctx(ctx) {
    auto ptr = new msm::SlacFSM(ctx);
    fsm = ptr;
}

slac_fsm::~slac_fsm() {
    auto ptr = to_msm(fsm);
    delete ptr;
}

void slac_fsm::reset() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::reset{});
    event_post_processing();
}

void slac_fsm::enter_bcd() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::enter_bcd{});
    event_post_processing();
}

void slac_fsm::leave_bcd() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::leave_bcd{});
    event_post_processing();
}

void slac_fsm::message(messages::HomeplugMessage msg) {
    msm::message event;
    event.payload = std::move(msg);
    auto ptr = to_msm(fsm);
    ptr->process_event(event);
    event_post_processing();
}

void slac_fsm::update() {
    auto ptr = to_msm(fsm);
    ptr->process_event(msm::update{});
    event_post_processing();
}

void slac_fsm::restart_fsm() {
    auto ptr = to_msm(fsm);
    ptr->start();
    event_post_processing();
}

} // namespace everest::lib::slac
