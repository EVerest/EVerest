// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// mcs_ev_control - terminal control panel for the simulated EV on the two-ChargeBridge MCS bench.
//
// EvManager takes its simulation programme over external MQTT
// (everest_external/nodered/<connector>/carsim/cmd/{execute,modify}_charging_session, a
// ';'-separated command list). This panel composes those programmes from a few editable numbers
// and sends them on a button press, so a session no longer has to start at EVerest startup
// (auto_exec) and nobody has to type mosquitto_pub lines during a test.
//
//   Start session  execute_charging_session: plug in, SLAC, MCS -20 session, charge <charge> s,
//                  regular stop, then park in Car Paused (CP B, "sleep 36000").
//   Wake           modify_charging_session: CC.5.2.4 wake from Car Paused (cp_c_pulse 4) into
//                  a new session with the same charge/stop/park programme.
//   Stop charging  modify_charging_session: stop the running charge loop now
//                  (iso_wait_for_stop 0 -> PowerDelivery(stop) with CP held in C, SessionStop),
//                  then park.
//   Unplug         modify_charging_session: "unplug" - CP to A, session over. Meant for a
//                  parked EV; an unplug during the charge loop is an emergency C-exit and the
//                  EVSE latches a CEFAULT on MCS, so press Stop charging first.
//
// <cycles> > 1 chains further wake cycles onto Start/Wake: park <sleep> s, cp_c_pulse 4, session,
// ... - the same choreography run-mcs-wake-cycles.sh drives from the outside. "Unplug at end"
// replaces the final indefinite park with "sleep <sleep>;unplug".
//
// Bench flavour (--bench mcs|ccs, default mcs, switchable in the panel): MCS runs the MCS energy
// service and wakes with the CC.5.2.4 pulse; CCS runs the DC service and wakes with a BCB toggle
// (iso_start_bcb_toggle 1). Protocol: the panel narrows Ev15118's SupportedAppProtocol offer per
// session via its external command everest_external/nodered/ev15118/cmd/select_protocol
// (all | iso20 | iso2 | din), published right before every Start / Wake programme.
//
// Broker: --host/--port, else MQTT_SERVER_ADDRESS / MQTT_SERVER_PORT (the netns harness exports
// them - inside the namespace the broker sits on the veth, not on localhost), else localhost:1883.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <mosquitto.h>

namespace {

enum class Bench {
    MCS,
    CCS
};

struct Options {
    std::string host{"localhost"};
    int port{1883};
    int connector_id{1};
    Bench bench{Bench::MCS};
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
        } else if (a == "--connector") {
            o.connector_id = std::stoi(next("connector id"));
        } else if (a == "--bench") {
            const auto b = next("bench flavour (mcs|ccs)");
            if (b == "mcs") {
                o.bench = Bench::MCS;
            } else if (b == "ccs") {
                o.bench = Bench::CCS;
            } else {
                std::cerr << "--bench takes mcs or ccs, got " << b << '\n';
                std::exit(2);
            }
        } else if (a == "--ccs") {
            o.bench = Bench::CCS;
        } else if (a == "-h" || a == "--help") {
            std::cout << "usage: mcs_ev_control [--host H] [--port P] [--connector N] [--bench mcs|ccs]\n"
                         "  defaults: $MQTT_SERVER_ADDRESS or localhost, $MQTT_SERVER_PORT or 1883, connector 1, "
                         "bench mcs\n";
            std::exit(0);
        } else {
            std::cerr << "unknown argument " << a << '\n';
            std::exit(2);
        }
    }
    return o;
}

// Thin libmosquitto wrapper: threaded network loop, automatic reconnect, fire-and-forget publish.
class Publisher {
public:
    Publisher(const Options& o, std::function<void()> on_change) : m_on_change(std::move(on_change)) {
        mosquitto_lib_init();
        m_mosq = mosquitto_new(nullptr, true, this);
        if (m_mosq == nullptr) {
            throw std::runtime_error("mosquitto_new failed");
        }
        mosquitto_connect_callback_set(m_mosq, [](mosquitto*, void* self, int rc) {
            auto* p = static_cast<Publisher*>(self);
            p->m_connected.store(rc == 0);
            p->set_last_error(rc == 0 ? "" : std::string{"connect refused: "} + mosquitto_connack_string(rc));
            p->m_on_change();
        });
        mosquitto_disconnect_callback_set(m_mosq, [](mosquitto*, void* self, int rc) {
            auto* p = static_cast<Publisher*>(self);
            p->m_connected.store(false);
            p->set_last_error(rc == 0 ? "disconnected" : std::string{"connection lost: "} + mosquitto_strerror(rc));
            p->m_on_change();
        });
        mosquitto_reconnect_delay_set(m_mosq, 1, 5, false);
        const int rc = mosquitto_connect_async(m_mosq, o.host.c_str(), o.port, 30);
        if (rc != MOSQ_ERR_SUCCESS) {
            set_last_error(std::string{"connect: "} + mosquitto_strerror(rc));
        }
        // The loop thread keeps retrying the connection with the delay set above.
        mosquitto_loop_start(m_mosq);
    }

    ~Publisher() {
        mosquitto_disconnect(m_mosq);
        mosquitto_loop_stop(m_mosq, true);
        mosquitto_destroy(m_mosq);
        mosquitto_lib_cleanup();
    }

    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

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
    std::function<void()> m_on_change;
    mutable std::mutex m_mutex;
    std::string m_last_error;
};

// --- programme builder -----------------------------------------------------------------------

constexpr const char* PARK = "sleep 36000"; // Car Paused until the next wake or unplug

int parse_or(const std::string& s, int fallback) {
    try {
        return s.empty() ? fallback : std::stoi(s);
    } catch (const std::exception&) {
        return fallback;
    }
}

struct Programme {
    Bench bench{Bench::MCS};
    int charge_s{30};
    int sleep_s{10};
    int cycles{1};
    bool unplug_at_end{false};

    // MCS: the megawatt DC service (same DC states, different service id). CCS: plain DC.
    const char* energy() const {
        return bench == Bench::MCS ? "mcs" : "DC";
    }
    // Resume from Car Paused: MCS wakes the EVSE with the CC.5.2.4 CP pulse, CCS with a BCB toggle.
    const char* wake_step() const {
        return bench == Bench::MCS ? "cp_c_pulse 4" : "iso_start_bcb_toggle 1";
    }

    std::string session() const {
        return std::string{"iso_wait_slac_matched;iso_start_v2g_session "} + energy() +
               ";iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop " + std::to_string(charge_s) +
               ";iso_wait_v2g_session_stopped";
    }

    // Everything after the first session: further wake cycles, then the park or the unplug.
    std::string tail() const {
        std::string t;
        for (int i = 1; i < cycles; ++i) {
            t += ";sleep " + std::to_string(sleep_s) + ";" + wake_step() + ";" + session();
        }
        if (unplug_at_end) {
            t += ";sleep " + std::to_string(sleep_s) + ";unplug";
        } else {
            t += std::string{";"} + PARK;
        }
        return t;
    }

    std::string start() const {
        return "sleep 1;" + session() + tail();
    }
    std::string wake() const {
        return std::string{wake_step()} + ";" + session() + tail();
    }
    static std::string stop() {
        return std::string{"iso_wait_for_stop 0;iso_wait_v2g_session_stopped;"} + PARK;
    }
    static std::string unplug() {
        return "unplug";
    }
};

std::string timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[16];
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

} // namespace

int main(int argc, char** argv) {
    using namespace ftxui;

    const Options opts = parse_options(argc, argv);
    const std::string topic_base = "everest_external/nodered/" + std::to_string(opts.connector_id) + "/carsim/cmd/";

    auto screen = ScreenInteractive::Fullscreen();
    Publisher mqtt{opts, [&] { screen.Post(Event::Custom); }};

    // Editable numbers (kept as text for the Input widgets, parsed on use).
    std::string charge_text = "30";
    std::string sleep_text = "10";
    std::string cycles_text = "1";
    bool unplug_at_end = false;
    const std::vector<std::string> bench_entries{"MCS", "CCS"};
    int bench_sel = opts.bench == Bench::CCS ? 1 : 0;
    // Index order = payload order below.
    const std::vector<std::string> protocol_entries{"all (-20, -2, DIN in that order)", "ISO 15118-20", "ISO 15118-2",
                                                    "DIN SPEC 70121"};
    const std::vector<std::string> protocol_payloads{"all", "iso20", "iso2", "din"};
    int protocol_sel = 0;
    const std::string protocol_topic = "everest_external/nodered/ev15118/cmd/select_protocol";

    std::deque<std::string> log;
    auto note = [&](std::string line) {
        log.push_front(timestamp() + "  " + std::move(line));
        while (log.size() > 8) {
            log.pop_back();
        }
    };

    auto programme = [&] {
        Programme p;
        p.bench = bench_sel == 1 ? Bench::CCS : Bench::MCS;
        p.charge_s = std::max(0, parse_or(charge_text, 30));
        p.sleep_s = std::max(0, parse_or(sleep_text, 10));
        p.cycles = std::max(1, parse_or(cycles_text, 1));
        p.unplug_at_end = unplug_at_end;
        return p;
    };

    auto send = [&](const char* cmd, const std::string& payload, const char* label) {
        if (!mqtt.connected()) {
            note(std::string{label} + ": NOT SENT, broker not connected");
            return;
        }
        if (mqtt.publish(topic_base + cmd, payload)) {
            note(std::string{label} + " -> " + cmd + ": " + payload);
        } else {
            note(std::string{label} + ": publish failed - " + mqtt.last_error());
        }
    };
    // A programme that starts a session first tells Ev15118 which protocol generation(s) to offer.
    auto send_with_protocol = [&](const char* cmd, const std::string& payload, const char* label) {
        if (mqtt.connected()) {
            const auto& proto = protocol_payloads[static_cast<size_t>(protocol_sel)];
            if (mqtt.publish(protocol_topic, proto)) {
                note(std::string{"protocol -> "} + proto);
            } else {
                note(std::string{"protocol: publish failed - "} + mqtt.last_error());
            }
        }
        send(cmd, payload, label);
    };

    auto digits_only = CatchEvent([](Event e) {
        return e.is_character() &&
               (e.character().empty() || !std::isdigit(static_cast<unsigned char>(e.character()[0])));
    });
    // One option set per field: the cursor starts behind the preset value, not in front of it.
    int charge_cursor = static_cast<int>(charge_text.size());
    int sleep_cursor = static_cast<int>(sleep_text.size());
    int cycles_cursor = static_cast<int>(cycles_text.size());
    auto num_input = [&](std::string& content, int& cursor, const char* placeholder) {
        InputOption o;
        o.multiline = false;
        o.cursor_position = &cursor;
        return Input(&content, placeholder, o) | digits_only;
    };
    auto charge_input = num_input(charge_text, charge_cursor, "30");
    auto sleep_input = num_input(sleep_text, sleep_cursor, "10");
    auto cycles_input = num_input(cycles_text, cycles_cursor, "1");
    auto unplug_box = Checkbox("Unplug at the end (instead of parking)", &unplug_at_end);
    auto bench_toggle = Toggle(&bench_entries, &bench_sel);
    auto protocol_radio = Radiobox(&protocol_entries, &protocol_sel);

    auto btn = ButtonOption::Ascii();
    auto start_btn = Button(
        "Start session", [&] { send_with_protocol("execute_charging_session", programme().start(), "Start"); }, btn);
    auto wake_btn =
        Button("Wake", [&] { send_with_protocol("modify_charging_session", programme().wake(), "Wake"); }, btn);
    auto stop_btn = Button("Stop charging", [&] { send("modify_charging_session", Programme::stop(), "Stop"); }, btn);
    auto unplug_btn = Button("Unplug", [&] { send("modify_charging_session", Programme::unplug(), "Unplug"); }, btn);
    auto quit_btn = Button("Quit", screen.ExitLoopClosure(), btn);

    auto fields =
        Container::Vertical({charge_input, sleep_input, cycles_input, unplug_box, bench_toggle, protocol_radio});
    auto buttons = Container::Horizontal({start_btn, wake_btn, stop_btn, unplug_btn, quit_btn});
    auto root = Container::Vertical({fields, buttons});

    auto field_row = [](const char* label, Component& c, const char* unit) {
        return hbox({text(label) | size(WIDTH, EQUAL, 34), c->Render() | size(WIDTH, EQUAL, 8) | border, text(unit)});
    };

    auto ui = Renderer(root, [&] {
        const auto p = programme();
        const bool up = mqtt.connected();
        Elements log_lines;
        for (const auto& l : log) {
            log_lines.push_back(paragraph(l));
        }
        if (log_lines.empty()) {
            log_lines.push_back(text("no command sent yet") | dim);
        }
        return vbox({
                   text(" MCS EV control - EvManager connector " + std::to_string(opts.connector_id) + " ") | bold |
                       center,
                   separator(),
                   field_row("Charge time", charge_input, " s   charge loop per session"),
                   field_row("Sleep time", sleep_input, " s   park between cycles / before unplug"),
                   field_row("Cycles", cycles_input, "     wake cycles chained onto Start/Wake"),
                   unplug_box->Render(),
                   hbox({text("Bench") | size(WIDTH, EQUAL, 34), bench_toggle->Render(),
                         text(p.bench == Bench::MCS ? "   MCS energy service, CC.5.2.4 wake"
                                                    : "   DC energy service, BCB toggle wake") |
                             dim}),
                   hbox({text("Protocol offer (Ev15118)") | size(WIDTH, EQUAL, 34), protocol_radio->Render()}),
                   separator(),
                   hbox({start_btn->Render(), text(" "), wake_btn->Render(), text(" "), stop_btn->Render(), text(" "),
                         unplug_btn->Render(), filler(), quit_btn->Render()}),
                   separator(),
                   hbox(
                       {text("Broker " + opts.host + ":" + std::to_string(opts.port) + "  "),
                        up ? text("connected") | color(Color::Green) : text("NOT connected") | color(Color::Red) | bold,
                        text(up || mqtt.last_error().empty() ? "" : "  (" + mqtt.last_error() + ")") | dim}),
                   paragraph("Next Start: " + p.start()) | dim,
                   paragraph("Next Wake:  " + p.wake()) | dim,
                   separator(),
                   vbox(log_lines) | flex,
                   separator(),
                   text("Tab / Shift-Tab and arrows move, Enter presses, q quits. Unplug only a parked EV (Stop "
                        "charging first).") |
                       dim,
               }) |
               border;
    });

    // ftxui's containers cycle Tab only among their own children, which would trap Tab inside the
    // field group. Cycle over one flat list instead, so Tab/Shift-Tab reach the buttons too.
    const std::vector<Component> focusables{charge_input, sleep_input,    cycles_input, unplug_box,
                                            bench_toggle, protocol_radio, start_btn,    wake_btn,
                                            stop_btn,     unplug_btn,     quit_btn};
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
        // 'q' quits unless a text field has the focus (where it is just ignored by digits_only).
        if (e == Event::Character('q') && !fields->Focused()) {
            screen.Exit();
            return true;
        }
        return false;
    });

    note("ready - broker " + opts.host + ":" + std::to_string(opts.port) + ", bench " +
         (opts.bench == Bench::MCS ? "MCS" : "CCS"));
    screen.Loop(ui);
    return 0;
}
