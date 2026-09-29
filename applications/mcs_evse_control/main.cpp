// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// mcs_evse_control - terminal control panel for the EVSE side of the MCS bench.
//
// The EVSE counterpart of mcs_ev_control. It talks to EvseManager and the EVSE's energy node only
// through the EVerest API (AsyncAPI over MQTT, docs/source/reference/EVerest_API), so the running
// EVerest config needs two API modules:
//
//   evse_manager_consumer_API            everest_api/1/evse_manager_consumer/<id>/...
//   external_energy_limits_consumer_API  everest_api/1/external_energy_limits_consumer/<id>/...
//
//   Pause / Resume   m2e/pause_charging, m2e/resume_charging (EVSE-initiated pause of the session)
//   Stop             m2e/stop_transaction, reason Local
//   Enable / Disable m2e/enable_disable, source LocalAPI at priority 100; Release hands the decision
//                    back to the other sources (Unassigned)
//   Force unlock     m2e/force_unlock, connector 1
//   Power / current  m2e/set_external_limits on the energy node: one import schedule entry with
//   limit            total_power_W or ac_max_current_A towards the leaves. Clear sends empty schedules,
//                    which drops back to the node's own fuse limit.
//
// Requests carry headers.replyTo pointing at a panel-private topic, so every reply ends up in the
// log. The panel answers the modules' communication check (m2e/communication_check, true) every
// second, and on connect it asks for the latched values (m2e/<var>/get) so the status is filled
// before the first change.
//
// Broker: --host/--port, else MQTT_SERVER_ADDRESS / MQTT_SERVER_PORT, else localhost:1883.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <mosquitto.h>
#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr const char* LIMIT_SOURCE = "mcs_evse_control";

struct Options {
    std::string host{"localhost"};
    int port{1883};
    std::string evse_api{"evse_manager_api"};
    std::string limits_api{"evse_energy_limits_api"};
};

Options parse_options(int argc, char** argv) {
    Options o;
    if (const char* e = std::getenv("MQTT_SERVER_ADDRESS"); e && *e) {
        o.host = e;
    }
    if (const char* e = std::getenv("MQTT_SERVER_PORT"); e && *e) {
        o.port = std::atoi(e);
    }
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << a << " needs a " << what << '\n';
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--host") {
            o.host = next("host name");
        } else if (a == "--port") {
            o.port = std::stoi(next("port number"));
        } else if (a == "--evse-api") {
            o.evse_api = next("module id");
        } else if (a == "--limits-api") {
            o.limits_api = next("module id");
        } else if (a == "-h" || a == "--help") {
            std::cout << "usage: mcs_evse_control [--host H] [--port P] [--evse-api ID] [--limits-api ID]\n"
                         "  --evse-api    module id of the evse_manager_consumer_API (default evse_manager_api)\n"
                         "  --limits-api  module id of the external_energy_limits_consumer_API\n"
                         "                (default evse_energy_limits_api)\n"
                         "  broker: $MQTT_SERVER_ADDRESS or localhost, $MQTT_SERVER_PORT or 1883\n";
            std::exit(0);
        } else {
            std::cerr << "unknown argument " << a << '\n';
            std::exit(2);
        }
    }
    return o;
}

// libmosquitto wrapper: threaded network loop, automatic reconnect, subscriptions restored on every
// (re)connect, incoming messages handed to one callback.
class Client {
public:
    using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;

    Client(const Options& o, std::vector<std::string> subscriptions, std::function<void()> on_connect,
           MessageHandler on_message, std::function<void()> on_change) :
        m_subscriptions(std::move(subscriptions)),
        m_on_connect(std::move(on_connect)),
        m_on_message(std::move(on_message)),
        m_on_change(std::move(on_change)) {
        mosquitto_lib_init();
        m_mosq = mosquitto_new(nullptr, true, this);
        if (m_mosq == nullptr) {
            throw std::runtime_error("mosquitto_new failed");
        }
        mosquitto_connect_callback_set(m_mosq, [](mosquitto* mosq, void* self, int rc) {
            auto* c = static_cast<Client*>(self);
            c->m_connected.store(rc == 0);
            c->set_last_error(rc == 0 ? "" : std::string{"connect refused: "} + mosquitto_connack_string(rc));
            if (rc == 0) {
                for (const auto& s : c->m_subscriptions) {
                    mosquitto_subscribe(mosq, nullptr, s.c_str(), 1);
                }
                c->m_on_connect();
            }
            c->m_on_change();
        });
        mosquitto_disconnect_callback_set(m_mosq, [](mosquitto*, void* self, int rc) {
            auto* c = static_cast<Client*>(self);
            c->m_connected.store(false);
            c->set_last_error(rc == 0 ? "disconnected" : std::string{"connection lost: "} + mosquitto_strerror(rc));
            c->m_on_change();
        });
        mosquitto_message_callback_set(m_mosq, [](mosquitto*, void* self, const mosquitto_message* m) {
            auto* c = static_cast<Client*>(self);
            const std::string payload(static_cast<const char*>(m->payload), static_cast<size_t>(m->payloadlen));
            c->m_on_message(m->topic, payload);
            c->m_on_change();
        });
        mosquitto_reconnect_delay_set(m_mosq, 1, 5, false);
        const int rc = mosquitto_connect_async(m_mosq, o.host.c_str(), o.port, 30);
        if (rc != MOSQ_ERR_SUCCESS) {
            set_last_error(std::string{"connect: "} + mosquitto_strerror(rc));
        }
        // The loop thread keeps retrying the connection with the delay set above.
        mosquitto_loop_start(m_mosq);
    }

    ~Client() {
        mosquitto_disconnect(m_mosq);
        mosquitto_loop_stop(m_mosq, true);
        mosquitto_destroy(m_mosq);
        mosquitto_lib_cleanup();
    }

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool publish(const std::string& topic, const std::string& payload) {
        const int rc = mosquitto_publish(m_mosq, nullptr, topic.c_str(), static_cast<int>(payload.size()),
                                         payload.data(), 1, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            set_last_error(std::string{"publish: "} + mosquitto_strerror(rc));
            return false;
        }
        return true;
    }

    bool connected() const {
        return m_connected.load();
    }

    std::string last_error() const {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return m_last_error;
    }

private:
    void set_last_error(std::string e) {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_last_error = std::move(e);
    }

    mosquitto* m_mosq{nullptr};
    std::atomic<bool> m_connected{false};
    std::vector<std::string> m_subscriptions;
    std::function<void()> m_on_connect;
    MessageHandler m_on_message;
    std::function<void()> m_on_change;
    mutable std::mutex m_mutex;
    std::string m_last_error;
};

std::string timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[16];
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

std::string rfc3339_now() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03dZ", buf, static_cast<int>(ms));
    return out;
}

std::optional<double> parse_number(const std::string& s) {
    try {
        size_t used = 0;
        const double v = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(v)) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string fmt(double v, int decimals, const char* unit) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f %s", decimals, v, unit);
    return buf;
}

// Formats a number at a JSON path, or "-" if it is missing. A path element is an object key.
std::string num_at(const json& j, std::initializer_list<const char*> path, int decimals, const char* unit,
                   double scale = 1.0) {
    const json* cur = &j;
    for (const char* key : path) {
        if (!cur->is_object() || !cur->contains(key)) {
            return "-";
        }
        cur = &(*cur)[key];
    }
    if (!cur->is_number()) {
        return "-";
    }
    return fmt(cur->get<double>() * scale, decimals, unit);
}

std::string str_at(const json& j, const char* key) {
    if (j.is_object() && j.contains(key)) {
        const auto& v = j[key];
        return v.is_string() ? v.get<std::string>() : v.dump();
    }
    return "-";
}

std::string duration(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) {
        return "-";
    }
    const auto s = j[key].get<long>();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

// Everything the panel shows, filled from the MQTT thread and read by the render thread.
struct State {
    std::mutex mutex;
    std::map<std::string, json> vars; // "<api>/<var>" -> last value
    std::map<std::string, Clock::time_point> heartbeat;
    std::deque<std::string> log;

    void note(std::string line) {
        log.push_front(timestamp() + "  " + std::move(line));
        while (log.size() > 12) {
            log.pop_back();
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    using namespace ftxui;

    const Options opts = parse_options(argc, argv);
    const std::string evse_base = "everest_api/1/evse_manager_consumer/" + opts.evse_api + "/";
    const std::string limits_base = "everest_api/1/external_energy_limits_consumer/" + opts.limits_api + "/";
    const std::string reply_base = "mcs_evse_control/" + std::to_string(getpid()) + "/reply/";

    // Latched vars requested once per connect, so the panel is filled before the next change.
    const std::vector<std::string> evse_vars{"session_info",   "ev_info",           "powermeter",
                                             "enforced_limits", "selected_protocol", "dlink_ready",
                                             "dc_voltage_current", "isolation_measurement", "hw_capabilities"};

    auto screen = ScreenInteractive::Fullscreen();
    State state;

    auto on_message = [&](const std::string& topic, const std::string& payload) {
        const std::lock_guard<std::mutex> lock{state.mutex};
        json value = json::parse(payload, nullptr, false);
        if (value.is_discarded()) {
            value = payload;
        }
        auto take = [&](const std::string& base, const char* api) {
            if (topic.rfind(base + "e2m/", 0) != 0) {
                return false;
            }
            const auto var = topic.substr(base.size() + 4);
            if (var == "heartbeat") {
                state.heartbeat[api] = Clock::now();
            } else if (!value.is_null()) { // a get for a value never published answers "null"
                if (var == "session_event") {
                    state.note(std::string{"event "} + str_at(value, "event"));
                }
                if (var == "hlc_session_failed") {
                    state.note("HLC session failed: " + value.dump());
                }
                state.vars[std::string{api} + "/" + var] = std::move(value);
            }
            return true;
        };
        if (take(evse_base, "evse") || take(limits_base, "limits")) {
            return;
        }
        if (topic.rfind(reply_base, 0) == 0) {
            state.note("reply " + topic.substr(reply_base.size()) + ": " + payload);
        }
    };

    // Set on every (re)connect; the worker thread below then asks for the latched values. Doing it
    // there keeps the connect callback from racing the construction of the client it would use.
    std::atomic<bool> fetch_latched{false};
    Client mqtt{opts,
                {evse_base + "e2m/#", limits_base + "e2m/#", reply_base + "#"},
                [&] { fetch_latched.store(true); },
                on_message,
                [&] { screen.Post(Event::Custom); }};

    // The API modules raise a communication fault if no client checks in within
    // cfg_communication_check_to_s; answer both once a second.
    std::atomic<bool> running{true};
    std::thread comm_check([&] {
        while (running.load()) {
            if (mqtt.connected() && fetch_latched.exchange(false)) {
                // Reply straight onto the var topic, so the answer takes the same path as a change.
                for (const auto& v : evse_vars) {
                    const json req{{"headers", {{"replyTo", evse_base + "e2m/" + v}}}};
                    mqtt.publish(evse_base + "m2e/" + v + "/get", req.dump());
                }
                const std::lock_guard<std::mutex> lock{state.mutex};
                state.note("connected, requested the latched EVSE values");
            }
            if (mqtt.connected()) {
                mqtt.publish(evse_base + "m2e/communication_check", "true");
                mqtt.publish(limits_base + "m2e/communication_check", "true");
            }
            for (int i = 0; i < 10 && running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

    auto note = [&](std::string line) {
        const std::lock_guard<std::mutex> lock{state.mutex};
        state.note(std::move(line));
    };

    // A request with a reply: payload is omitted for commands without one.
    auto request = [&](const std::string& op, const std::optional<json>& payload) {
        if (!mqtt.connected()) {
            note(op + ": NOT SENT, broker not connected");
            return;
        }
        json req{{"headers", {{"replyTo", reply_base + op}}}};
        if (payload) {
            req["payload"] = *payload;
        }
        if (mqtt.publish(evse_base + "m2e/" + op, req.dump())) {
            note("-> " + op + (payload ? " " + payload->dump() : ""));
        } else {
            note(op + ": publish failed - " + mqtt.last_error());
        }
    };

    auto enable_disable = [&](const char* enable_state) {
        request("enable_disable",
                json{{"connector_id", 0},
                     {"source", {{"enable_source", "LocalAPI"}, {"enable_state", enable_state}, {"enable_priority", 100}}}});
    };

    auto send_limits = [&](const json& limits, const std::string& label) {
        if (!mqtt.connected()) {
            note(label + ": NOT SENT, broker not connected");
            return;
        }
        if (mqtt.publish(limits_base + "m2e/set_external_limits", limits.dump())) {
            note("-> " + label);
        } else {
            note(label + ": publish failed - " + mqtt.last_error());
        }
    };

    // One import entry, valid from now on. The API has no reply for this command; the effect shows in
    // the enforced limits.
    auto import_limit = [&](const char* field, double value) {
        const json entry{{"timestamp", rfc3339_now()},
                         {"limits_to_root", json::object()},
                         {"limits_to_leaves", {{field, {{"value", value}, {"source", LIMIT_SOURCE}}}}}};
        return json{{"schedule_import", json::array({entry})},
                    {"schedule_export", json::array()},
                    {"schedule_setpoints", json::array()}};
    };

    std::string power_text = "100";
    std::string current_text = "16";
    int power_cursor = static_cast<int>(power_text.size());
    int current_cursor = static_cast<int>(current_text.size());
    auto number_only = CatchEvent([](Event e) {
        if (!e.is_character()) {
            return false;
        }
        const auto& c = e.character();
        return c.empty() || !(std::isdigit(static_cast<unsigned char>(c[0])) || c[0] == '.');
    });
    auto num_input = [&](std::string& content, int& cursor, const char* placeholder) {
        InputOption o;
        o.multiline = false;
        o.cursor_position = &cursor;
        return Input(&content, placeholder, o) | number_only;
    };
    auto power_input = num_input(power_text, power_cursor, "100");
    auto current_input = num_input(current_text, current_cursor, "16");

    auto btn = ButtonOption::Ascii();
    auto pause_btn = Button("Pause", [&] { request("pause_charging", std::nullopt); }, btn);
    auto resume_btn = Button("Resume", [&] { request("resume_charging", std::nullopt); }, btn);
    auto stop_btn = Button("Stop", [&] { request("stop_transaction", json{{"reason", "Local"}}); }, btn);
    auto enable_btn = Button("Enable", [&] { enable_disable("Enable"); }, btn);
    auto disable_btn = Button("Disable", [&] { enable_disable("Disable"); }, btn);
    auto release_btn = Button("Release", [&] { enable_disable("Unassigned"); }, btn);
    auto unlock_btn = Button("Force unlock", [&] { request("force_unlock", json(1)); }, btn);
    auto power_btn = Button(
        "Set power limit",
        [&] {
            if (const auto kw = parse_number(power_text); kw && *kw >= 0) {
                send_limits(import_limit("total_power_W", *kw * 1000.0), "power limit " + fmt(*kw, 1, "kW"));
            } else {
                note("power limit: '" + power_text + "' is not a number");
            }
        },
        btn);
    auto current_btn = Button(
        "Set current limit",
        [&] {
            if (const auto a = parse_number(current_text); a && *a >= 0) {
                send_limits(import_limit("ac_max_current_A", *a), "current limit " + fmt(*a, 1, "A"));
            } else {
                note("current limit: '" + current_text + "' is not a number");
            }
        },
        btn);
    auto clear_btn = Button(
        "Clear limits",
        [&] {
            send_limits(json{{"schedule_import", json::array()},
                             {"schedule_export", json::array()},
                             {"schedule_setpoints", json::array()}},
                        "clear limits (back to the node's fuse limit)");
        },
        btn);
    auto quit_btn = Button("Quit", screen.ExitLoopClosure(), btn);

    auto fields = Container::Vertical({power_input, current_input});
    auto root = Container::Vertical({fields, pause_btn, resume_btn, stop_btn, enable_btn, disable_btn, release_btn,
                                     unlock_btn, power_btn, current_btn, clear_btn, quit_btn});

    auto row = [](const std::string& label, const std::string& value) {
        return hbox({text(label) | size(WIDTH, EQUAL, 22) | dim, text(value)});
    };
    auto alive = [&](const char* api) {
        const auto it = state.heartbeat.find(api);
        if (it == state.heartbeat.end()) {
            return text("no heartbeat") | color(Color::Red);
        }
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - it->second).count();
        auto t = text("heartbeat " + std::to_string(age) + " s ago");
        return age <= 3 ? t | color(Color::Green) : t | color(Color::Red) | bold;
    };

    auto ui = Renderer(root, [&] {
        const std::lock_guard<std::mutex> lock{state.mutex};
        auto var = [&](const std::string& key) -> const json& {
            static const json empty = json::object();
            const auto it = state.vars.find(key);
            return it == state.vars.end() ? empty : it->second;
        };
        const auto& si = var("evse/session_info");
        const auto& ev = var("evse/ev_info");
        const auto& pm = var("evse/powermeter");
        const auto& lim = var("evse/enforced_limits");
        const auto& dc = var("evse/dc_voltage_current");
        const auto& imd = var("evse/isolation_measurement");
        const auto& proto = var("evse/selected_protocol");
        const auto& dlink = var("evse/dlink_ready");
        const bool up = mqtt.connected();

        auto session = vbox({
            text("Session") | bold,
            row("State", str_at(si, "state")),
            row("Protocol", proto.is_string() ? proto.get<std::string>() : str_at(si, "selected_protocol")),
            row("D-LINK ready", dlink.is_boolean() ? (dlink.get<bool>() ? "yes" : "no") : "-"),
            row("Session / transaction", duration(si, "session_duration_s") + " / " +
                                             duration(si, "transaction_duration_s")),
            row("Charged", num_at(si, {"charged_energy_wh"}, 2, "kWh", 0.001)),
            row("Power (session)", num_at(si, {"latest_total_w"}, 1, "kW", 0.001)),
        });
        auto supply = vbox({
            text("DC output") | bold,
            row("Supply V / I", num_at(dc, {"voltage_V"}, 1, "V") + " / " + num_at(dc, {"current_A"}, 1, "A")),
            row("Meter power", num_at(pm, {"power_W", "total"}, 1, "kW", 0.001)),
            row("Meter energy", num_at(pm, {"energy_Wh_import", "total"}, 2, "kWh", 0.001)),
            row("Isolation", num_at(imd, {"resistance_F_Ohm"}, 0, "kOhm", 0.001)),
            row("Enforced power", num_at(lim, {"limits_root_side", "total_power_W"}, 1, "kW", 0.001)),
            row("Enforced current", num_at(lim, {"limits_root_side", "ac_max_current_A"}, 1, "A")),
        });
        auto vehicle = vbox({
            text("EV") | bold,
            row("SoC", num_at(ev, {"soc"}, 1, "%")),
            row("Present V / I", num_at(ev, {"present_voltage"}, 1, "V") + " / " +
                                     num_at(ev, {"present_current"}, 1, "A")),
            row("Target V / I", num_at(ev, {"target_voltage"}, 1, "V") + " / " +
                                    num_at(ev, {"target_current"}, 1, "A")),
            row("Max V / I / P", num_at(ev, {"maximum_voltage_limit"}, 0, "V") + " / " +
                                     num_at(ev, {"maximum_current_limit"}, 0, "A") + " / " +
                                     num_at(ev, {"maximum_power_limit"}, 1, "kW", 0.001)),
            row("EVCC ID", str_at(ev, "evcc_id")),
        });

        Elements log_lines;
        for (const auto& l : state.log) {
            log_lines.push_back(paragraph(l));
        }

        return vbox({
                   text(" MCS EVSE control - " + opts.evse_api + " / " + opts.limits_api + " ") | bold | center,
                   separator(),
                   hbox({session | flex, separator(), supply | flex, separator(), vehicle | flex}),
                   separator(),
                   hbox({text("Session  ") | bold, pause_btn->Render(), text(" "), resume_btn->Render(), text(" "),
                         stop_btn->Render(), text("    EVSE  ") | bold, enable_btn->Render(), text(" "),
                         disable_btn->Render(), text(" "), release_btn->Render(), text(" "), unlock_btn->Render()}),
                   hbox({text("Power limit") | size(WIDTH, EQUAL, 14), power_input->Render() | size(WIDTH, EQUAL, 10) |
                                                                           border,
                         text(" kW "), power_btn->Render(), text("   Current limit "),
                         current_input->Render() | size(WIDTH, EQUAL, 8) | border, text(" A "), current_btn->Render(),
                         text("  "), clear_btn->Render()}),
                   separator(),
                   hbox({text("Broker " + opts.host + ":" + std::to_string(opts.port) + "  "),
                         up ? text("connected") | color(Color::Green) : text("NOT connected") | color(Color::Red) | bold,
                         text(up || mqtt.last_error().empty() ? "" : "  (" + mqtt.last_error() + ")") | dim,
                         text("   evse API: "), alive("evse"), text("   limits API: "), alive("limits"), filler(),
                         quit_btn->Render()}),
                   separator(),
                   vbox(log_lines) | flex,
                   separator(),
                   text("Tab / Shift-Tab and arrows move, Enter presses, q quits. Limits act on the energy node "
                        "and are capped by the grid connection fuse.") |
                       dim,
               }) |
               border;
    });

    // Cycle Tab over one flat list, so it reaches the buttons and not only the fields.
    const std::vector<Component> focusables{power_input, current_input, pause_btn,  resume_btn, stop_btn,
                                            enable_btn,  disable_btn,   release_btn, unlock_btn, power_btn,
                                            current_btn, clear_btn,     quit_btn};
    auto cycle_focus = [&](int step) {
        int current = 0;
        for (int i = 0; i < static_cast<int>(focusables.size()); ++i) {
            if (focusables[i]->Focused()) {
                current = i;
                break;
            }
        }
        const int n = static_cast<int>(focusables.size());
        focusables[(current + step + n) % n]->TakeFocus();
    };

    ui |= CatchEvent([&](Event e) {
        if (e == Event::Tab) {
            cycle_focus(+1);
            return true;
        }
        if (e == Event::TabReverse) {
            cycle_focus(-1);
            return true;
        }
        // 'q' quits unless a text field has the focus (where number_only swallows it).
        if (e == Event::Character('q') && !fields->Focused()) {
            screen.Exit();
            return true;
        }
        return false;
    });

    // Re-render once a second so the heartbeat ages and durations move without MQTT traffic.
    std::thread ticker([&] {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            screen.Post(Event::Custom);
        }
    });

    note("ready - broker " + opts.host + ":" + std::to_string(opts.port));
    screen.Loop(ui);
    running.store(false);
    comm_check.join();
    ticker.join();
    return 0;
}
