// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "datalink_controller.hpp"

#include <utility>

#include <everest/io/event/fd_event_handler.hpp>

#include "everest/logging.hpp"

namespace module {
namespace main {

namespace {

/// A command backlog this deep means the loop is not running (or is wedged). Dropping is better
/// than growing without bound, and it is loud.
constexpr std::size_t max_pending_commands = 64;

link_config to_link_config(datalink_controller::config const& settings) {
    link_config config;
    config.link_detect_timeout_ms = settings.link_detect_timeout_ms;
    config.publish_connector_mac = settings.publish_connector_mac;
    return config;
}

} // namespace

datalink_controller::datalink_controller(config settings, callbacks handlers) :
    m_config(std::move(settings)),
    m_callbacks(std::move(handlers)),
    m_fsm(to_link_config(m_config)),
    m_watcher(m_config.device, m_config.neighbor_liveness) {

    // Both timers are deadlines, not ticks.
    m_link_detect_timer.set_single_shot(true);
    m_liveness_grace_timer.set_single_shot(true);

    everest::lib::io::netlink::device_watcher::callbacks watcher_handlers;
    watcher_handlers.on_carrier_change = [this](bool up) { on_carrier_change(up); };
    watcher_handlers.on_presence_change = [this](bool present) { on_presence_change(present); };
    if (m_config.neighbor_liveness) {
        watcher_handlers.on_neighbor = [this](everest::lib::io::netlink::neighbor_report const& report) {
            on_neighbor(report);
        };
    }
    watcher_handlers.on_initial_state = [this]() { on_initial_state(); };
    // libio must not depend on the framework logger, so the watcher hands its diagnostics to a
    // sink. Without this they would go to std::cerr and bypass the EVerest log entirely.
    using severity = everest::lib::io::netlink::device_watcher::diagnostic_severity;
    watcher_handlers.on_diagnostic = [](severity level, std::string const& message) {
        if (level == severity::error) {
            EVLOG_error << "McsEvDataLink: " << message;
        } else {
            EVLOG_warning << "McsEvDataLink: " << message;
        }
    };
    watcher_handlers.on_fatal_error = [this](std::string const& reason) { on_watcher_error(reason); };
    m_watcher.set_callbacks(std::move(watcher_handlers));
}

datalink_controller::~datalink_controller() = default;

bool datalink_controller::open() {
    return m_watcher.open();
}

int datalink_controller::error() const {
    return m_watcher.error();
}

bool datalink_controller::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto registrations_ok = true;

    // A watcher without a socket is not a registration failure: the module stays usable (commands
    // are answered, the setup deadline still fails communication initialization honestly) and the
    // CommunicationFault raised at open() time is what tells the operator what is wrong.
    if (m_watcher.error() == 0 and not m_watcher.register_events(handler)) {
        EVLOG_error << "McsEvDataLink: failed to register the rtnetlink watcher";
        registrations_ok = false;
    }
    if (not handler.register_event_handler(&m_link_detect_timer, [this]() { on_link_detect_timeout(); })) {
        EVLOG_error << "McsEvDataLink: failed to register the link detect timer";
        registrations_ok = false;
    }
    if (m_config.neighbor_liveness and
        not handler.register_event_handler(&m_liveness_grace_timer, [this]() { on_liveness_grace(); })) {
        EVLOG_error << "McsEvDataLink: failed to register the liveness grace timer";
        registrations_ok = false;
    }
    if (not handler.register_event_handler(&m_command_event, [this]() { drain_commands(); })) {
        EVLOG_error << "McsEvDataLink: failed to register the command event";
        registrations_ok = false;
    }

    return registrations_ok;
}

bool datalink_controller::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto ok = true;
    ok = handler.unregister_event_handler(&m_command_event) and ok;
    if (m_config.neighbor_liveness) {
        ok = handler.unregister_event_handler(&m_liveness_grace_timer) and ok;
    }
    ok = handler.unregister_event_handler(&m_link_detect_timer) and ok;
    if (m_watcher.error() == 0) {
        ok = m_watcher.unregister_events(handler) and ok;
    }
    return ok;
}

void datalink_controller::start() {
    // The interface needs a defined state before the first command arrives. The device's actual
    // situation is not known yet - the dump requested by open() is only read once the loop polls -
    // and it does not have to be: presence and carrier arrive as edges from the dump itself, and
    // UNMATCHED with no carrier is exactly where the machine has to start either way.
    m_fsm.start();
    run_effects();
}

void datalink_controller::on_initial_state() {
    if (m_watcher.device_present()) {
        EVLOG_info << "McsEvDataLink: supervising device " << m_config.device << " (carrier "
                   << (m_watcher.carrier_up() ? "up" : "down") << ", neighbour liveness "
                   << (m_config.neighbor_liveness ? "on" : "off") << ")";
        return;
    }
    // Normal on a Chargebridge: the TAP is created by the application at runtime, possibly long
    // after EVerest came up. Say it once at info level and then stay quiet until it appears.
    EVLOG_info << "McsEvDataLink: device " << m_config.device
               << " does not exist yet; waiting for it to appear (expected when the network device "
                  "is created at runtime)";
}

// --- commands ---------------------------------------------------------------------------------

bool datalink_controller::post(command_kind kind) {
    {
        auto queue = m_commands.handle();
        if (queue->size() >= max_pending_commands) {
            EVLOG_error << "McsEvDataLink: command backlog exceeded " << max_pending_commands
                        << " entries; dropping the command. The event loop is not running.";
            return false;
        }
        queue->push_back(kind);
    }
    m_command_event.notify();
    return true;
}

void datalink_controller::post_reset() {
    (void)post(command_kind::reset);
}

bool datalink_controller::post_trigger_matching() {
    return post(command_kind::trigger_matching);
}

void datalink_controller::drain_commands() {
    std::deque<command_kind> batch;
    {
        auto queue = m_commands.handle();
        batch.swap(*queue);
    }
    for (auto const& kind : batch) {
        apply(kind);
        // Per command rather than per batch: a start_timer must have taken effect before the next
        // command can ask for it to be stopped again.
        run_effects();
    }
}

void datalink_controller::apply(command_kind kind) {
    switch (kind) {
    case command_kind::reset:
        // EvManager calls this immediately before every trigger_matching, so it is the hot path and
        // deliberately cheap: a teardown that leaves the module ready, never a latch.
        EVLOG_debug << "McsEvDataLink: reset";
        forget_neighbors();
        m_fsm.reset();
        return;

    case command_kind::trigger_matching:
        // The EV stack asks for the data link. Per V2G10-030 this is the basic-signalling half of
        // the condition and the carrier is the other half, in either order - so the current carrier
        // level goes in with the event.
        EVLOG_info << "McsEvDataLink: trigger_matching (carrier " << (m_watcher.carrier_up() ? "up" : "down") << ")";
        if (not m_watcher.device_present()) {
            // The EV wants to talk and there is no network device to talk over. Absence was fine
            // while idle; now it is a fault worth surfacing rather than letting the session die on
            // a bare 4 s deadline.
            raise_fault("MCS data link device '" + m_config.device +
                        "' does not exist while the EV is requesting the data link");
        }
        m_fsm.trigger_matching(m_watcher.carrier_up());
        return;
    }
}

// --- effects ----------------------------------------------------------------------------------

void datalink_controller::run_effects() {
    for (auto const& item : m_fsm.take_effects()) {
        switch (item.what) {
        case effect::kind::publish_state:
            EVLOG_info << "McsEvDataLink: state " << to_string(item.state);
            if (m_callbacks.publish_state) {
                m_callbacks.publish_state(item.state);
            }
            break;

        case effect::kind::publish_dlink_ready:
            EVLOG_info << "McsEvDataLink: D-LINK_READY(" << (item.ready ? "link" : "no link") << ")";
            if (m_callbacks.publish_dlink_ready) {
                m_callbacks.publish_dlink_ready(item.ready);
            }
            break;

        case effect::kind::publish_connector_mac:
            EVLOG_info << "McsEvDataLink: charging connector MAC address " << item.mac;
            if (m_callbacks.publish_connector_mac) {
                m_callbacks.publish_connector_mac(item.mac);
            }
            break;

        case effect::kind::start_timer:
            if (not m_link_detect_timer.set_timeout_ms(item.timeout_ms)) {
                EVLOG_error << "McsEvDataLink: failed to arm the link detect timer";
            }
            break;

        case effect::kind::stop_timer:
            if (not m_link_detect_timer.disarm()) {
                EVLOG_error << "McsEvDataLink: failed to disarm the link detect timer";
            }
            break;
        }
    }
}

// --- watcher events ---------------------------------------------------------------------------

void datalink_controller::on_carrier_change(bool up) {
    EVLOG_info << "McsEvDataLink: carrier on " << m_config.device << " went " << (up ? "up" : "down");
    if (up) {
        // Carrier-up is not the same as "IPv6 usable": the kernel re-runs duplicate address
        // detection on the edge, so the link-local address takes about a second to become usable.
        // The seconds-scale deadline above absorbs that, but nothing here may assume it can send.
        m_fsm.carrier_up();
    } else {
        // A carrier drop is reported as a link loss whatever caused it - including the EV's own
        // sleep, where V2G10-040 allows the comm module to be switched off entirely. `ev_slac` has
        // no pause command, so the module cannot be told the difference, and reporting what it
        // observes is the honest answer: the link really is down, and the stack that decided to
        // sleep is the one that knows why. On the EVSE side dlink_pause exists and D-LINK_READY has
        // to survive the pause (V2G10-041); there is deliberately no analogue of that here.
        forget_neighbors();
        m_fsm.carrier_down();
    }
    run_effects();
}

void datalink_controller::on_presence_change(bool present) {
    if (present) {
        EVLOG_info << "McsEvDataLink: device " << m_config.device << " appeared";
        clear_fault();
        return;
    }

    EVLOG_warning << "McsEvDataLink: device " << m_config.device << " disappeared";
    forget_neighbors();
}

void datalink_controller::on_neighbor(everest::lib::io::netlink::neighbor_report const& report) {
    auto const verdict = m_neighbors.apply(report);

    if (verdict.cancel_grace) {
        cancel_liveness_grace();
    }
    if (not verdict.reachable_mac.empty()) {
        m_fsm.neighbor_reachable(verdict.reachable_mac);
    }
    if (verdict.arm_grace) {
        if (m_config.liveness_grace_ms <= 0) {
            EVLOG_warning << "McsEvDataLink: no reachable neighbour left on " << m_config.device
                          << ", treating the data link as lost";
            m_fsm.link_lost();
        } else {
            arm_liveness_grace();
        }
    }

    run_effects();
}

void datalink_controller::on_liveness_grace() {
    m_liveness_grace_armed = false;
    if (not m_neighbors.peer_is_lost()) {
        return;
    }
    // V2G10-036: a detected link loss, even though the carrier may still claim the local PHY is
    // fine. The peer here is the SECC.
    EVLOG_warning << "McsEvDataLink: no neighbour on " << m_config.device << " recovered within "
                  << m_config.liveness_grace_ms << " ms, treating the data link as lost";
    m_fsm.link_lost();
    run_effects();
}

void datalink_controller::on_link_detect_timeout() {
    EVLOG_warning << "McsEvDataLink: the communication setup deadline (" << m_config.link_detect_timeout_ms
                  << " ms) expired without a link on " << m_config.device
                  << "; communication initialization FAILED (V2G10-054). Waiting for the EVSE's restart "
                     "indication (V2G10-039) - this module does not retry on its own.";
    m_fsm.link_detect_timeout();
    run_effects();
}

void datalink_controller::on_watcher_error(std::string const& reason) {
    raise_fault("MCS data link supervision on device '" + m_config.device + "' is unavailable: " + reason);
}

// --- helpers ----------------------------------------------------------------------------------

void datalink_controller::forget_neighbors() {
    m_neighbors.clear();
    cancel_liveness_grace();
}

void datalink_controller::arm_liveness_grace() {
    if (m_liveness_grace_armed) {
        // Re-arming on every further NUD_FAILED would push the deadline out for as long as the
        // kernel keeps failing addresses, which is exactly when it should be running out.
        return;
    }
    if (m_liveness_grace_timer.set_timeout_ms(m_config.liveness_grace_ms)) {
        m_liveness_grace_armed = true;
    } else {
        EVLOG_error << "McsEvDataLink: failed to arm the liveness grace timer";
    }
}

void datalink_controller::cancel_liveness_grace() {
    if (not m_liveness_grace_armed) {
        return;
    }
    m_liveness_grace_armed = false;
    (void)m_liveness_grace_timer.disarm();
}

void datalink_controller::raise_fault(std::string const& message) {
    if (m_fault_raised and m_fault_message == message) {
        return;
    }
    m_fault_raised = true;
    m_fault_message = message;
    EVLOG_error << "McsEvDataLink: " << message;
    if (m_callbacks.raise_fault) {
        m_callbacks.raise_fault(message);
    }
}

void datalink_controller::clear_fault() {
    if (not m_fault_raised) {
        return;
    }
    m_fault_raised = false;
    m_fault_message.clear();
    if (m_callbacks.clear_fault) {
        m_callbacks.clear_fault();
    }
}

// --- accessors --------------------------------------------------------------------------------

link_state datalink_controller::state() const {
    return m_fsm.state();
}

bool datalink_controller::carrier_up() const {
    return m_watcher.carrier_up();
}

bool datalink_controller::device_present() const {
    return m_watcher.device_present();
}

} // namespace main
} // namespace module
