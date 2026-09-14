// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "datalink_controller.hpp"

#include <cstring>
#include <utility>

#include <everest/io/event/fd_event_handler.hpp>

#include "everest/logging.hpp"

namespace module {
namespace main {

namespace {

/// A backlog this deep means the loop is not running; further commands are dropped with an error.
constexpr std::size_t max_pending_commands = 64;

link_config to_link_config(datalink_controller::config const& settings) {
    link_config config;
    config.conn_retry_max = settings.conn_retry_max;
    config.link_detect_timeout_ms = settings.link_detect_timeout_ms;
    config.sync_repetition_ms = settings.sync_repetition_ms;
    config.retry_wait_ms = settings.retry_wait_ms;
    config.publish_ev_mac = settings.publish_ev_mac;
    return config;
}

} // namespace

datalink_controller::datalink_controller(config settings, callbacks handlers) :
    m_config(std::move(settings)),
    m_callbacks(std::move(handlers)),
    m_fsm(to_link_config(m_config)),
    m_watcher(m_config.device, m_config.neighbor_liveness) {

    m_link_detect_timer.set_single_shot(true);
    m_sync_repetition_timer.set_single_shot(true);
    m_retry_wait_timer.set_single_shot(true);
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
    // libio does not depend on the framework logger; without a sink diagnostics go to std::cerr.
    using severity = everest::lib::io::netlink::device_watcher::diagnostic_severity;
    watcher_handlers.on_diagnostic = [](severity level, std::string const& message) {
        if (level == severity::error) {
            EVLOG_error << "McsDataLink: " << message;
        } else {
            EVLOG_warning << "McsDataLink: " << message;
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

    if (m_watcher.error() == 0 and not m_watcher.register_events(handler)) {
        EVLOG_error << "McsDataLink: failed to register the rtnetlink watcher";
        registrations_ok = false;
    }
    if (not handler.register_event_handler(&m_link_detect_timer, [this]() { on_link_detect_timeout(); })) {
        EVLOG_error << "McsDataLink: failed to register the link detect timer";
        registrations_ok = false;
    }
    if (not handler.register_event_handler(&m_sync_repetition_timer, [this]() { on_sync_repetition_elapsed(); })) {
        EVLOG_error << "McsDataLink: failed to register the sync repetition timer";
        registrations_ok = false;
    }
    if (not handler.register_event_handler(&m_retry_wait_timer, [this]() { on_retry_wait_elapsed(); })) {
        EVLOG_error << "McsDataLink: failed to register the restart wait timer";
        registrations_ok = false;
    }
    if (m_config.neighbor_liveness and
        not handler.register_event_handler(&m_liveness_grace_timer, [this]() { on_liveness_grace(); })) {
        EVLOG_error << "McsDataLink: failed to register the liveness grace timer";
        registrations_ok = false;
    }
    if (not handler.register_event_handler(&m_command_event, [this]() { drain_commands(); })) {
        EVLOG_error << "McsDataLink: failed to register the command event";
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
    ok = handler.unregister_event_handler(&m_retry_wait_timer) and ok;
    ok = handler.unregister_event_handler(&m_sync_repetition_timer) and ok;
    ok = handler.unregister_event_handler(&m_link_detect_timer) and ok;
    if (m_watcher.error() == 0) {
        ok = m_watcher.unregister_events(handler) and ok;
    }
    return ok;
}

void datalink_controller::start() {
    m_fsm.start();
    run_effects();
}

void datalink_controller::on_initial_state() {
    if (m_watcher.device_present()) {
        EVLOG_info << "McsDataLink: supervising device " << m_config.device << " (carrier "
                   << (m_watcher.carrier_up() ? "up" : "down") << ", neighbour liveness "
                   << (m_config.neighbor_liveness ? "on" : "off") << ")";
        return;
    }
    EVLOG_info << "McsDataLink: device " << m_config.device
               << " does not exist yet; waiting for it to appear (expected when the network device "
                  "is created at runtime)";
}

// --- commands ---------------------------------------------------------------------------------

void datalink_controller::post(command item) {
    {
        auto queue = m_commands.handle();
        if (queue->size() >= max_pending_commands) {
            EVLOG_error << "McsDataLink: command backlog exceeded " << max_pending_commands
                        << " entries; dropping the command. The event loop is not running.";
            return;
        }
        queue->push_back(item);
    }
    m_command_event.notify();
}

void datalink_controller::post_reset(bool enable) {
    post(command{command_kind::reset, enable});
}

void datalink_controller::post_enter_bcd() {
    post(command{command_kind::enter_bcd, true});
}

void datalink_controller::post_leave_bcd() {
    post(command{command_kind::leave_bcd, true});
}

void datalink_controller::post_dlink_terminate() {
    post(command{command_kind::dlink_terminate, true});
}

void datalink_controller::post_dlink_error() {
    post(command{command_kind::dlink_error, true});
}

void datalink_controller::post_dlink_pause() {
    post(command{command_kind::dlink_pause, true});
}

void datalink_controller::drain_commands() {
    std::deque<command> batch;
    {
        auto queue = m_commands.handle();
        batch.swap(*queue);
    }
    for (auto const& item : batch) {
        apply(item);
        // Per command: a start_timer must take effect before the next command may stop it.
        run_effects();
    }
}

void datalink_controller::apply(command const& item) {
    switch (item.kind) {
    case command_kind::reset:
        EVLOG_info << "McsDataLink: reset(" << (item.enable ? "true" : "false") << ")";
        m_fsm.reset(item.enable);
        return;

    case command_kind::enter_bcd:
        if (not m_watcher.device_present()) {
            // Absence is a fault once an EV is present; EvseManager can then make the connector inoperative.
            raise_fault("MCS data link device '" + m_config.device +
                        "' does not exist while an EV is connected; the SPE link cannot be established");
        }
        m_fsm.enter_bcd(m_watcher.carrier_up());
        return;

    case command_kind::leave_bcd:
        forget_neighbors();
        m_fsm.leave_bcd();
        return;

    case command_kind::dlink_terminate:
        EVLOG_info << "McsDataLink: D-LINK_TERMINATE.request received, taking the data link down";
        forget_neighbors();
        m_fsm.dlink_terminate();
        return;

    case command_kind::dlink_error:
        EVLOG_warning << "McsDataLink: D-LINK_ERROR.request received; " << m_fsm.retry_count() << " of "
                      << m_config.conn_retry_max << " restart attempts used so far";
        forget_neighbors();
        m_fsm.dlink_error();
        return;

    case command_kind::dlink_pause:
        if (m_fsm.state() == internal_state::matched and not m_config.neighbor_liveness) {
            // With liveness off only a carrier edge ends the pause; the LAN8650 low-power mode is not
            // implemented, so the carrier stays up and that edge may never come.
            EVLOG_warning << "McsDataLink: pausing with neighbor_liveness disabled; link supervision "
                             "stays suspended until the carrier drops and returns";
        }
        if (m_fsm.state() != internal_state::matched) {
            EVLOG_warning << "McsDataLink: D-LINK_PAUSE.request received while not MATCHED (state "
                          << to_string(m_fsm.state()) << "); ignoring it";
            m_fsm.dlink_pause();
            return;
        }
        EVLOG_info << "McsDataLink: D-LINK_PAUSE.request received, staying MATCHED; carrier loss is "
                      "expected from here until the wake-up";
        // The PHY may power down in B0 and retire neighbour entries; forgetting them avoids a false liveness loss.
        forget_neighbors();
        m_fsm.dlink_pause();
        return;
    }
}

// --- effects ----------------------------------------------------------------------------------

void datalink_controller::run_effects() {
    for (auto const& item : m_fsm.take_effects()) {
        switch (item.what) {
        case effect::kind::publish_state:
            EVLOG_info << "McsDataLink: state " << to_string(item.state);
            if (m_callbacks.publish_state) {
                m_callbacks.publish_state(item.state);
            }
            break;

        case effect::kind::publish_dlink_ready:
            EVLOG_info << "McsDataLink: D-LINK_READY(" << (item.ready ? "link" : "no link") << ")";
            if (m_callbacks.publish_dlink_ready) {
                m_callbacks.publish_dlink_ready(item.ready);
            }
            break;

        case effect::kind::publish_request_error_routine:
            EVLOG_info << "McsDataLink: requesting the restart routine (IEC 61851-23-3 CC.5.2.3.2 "
                          "method 1, B0 to B)";
            if (m_callbacks.publish_request_error_routine) {
                m_callbacks.publish_request_error_routine();
            }
            break;

        case effect::kind::publish_ev_mac:
            EVLOG_info << "McsDataLink: EV MAC address " << item.mac;
            if (m_callbacks.publish_ev_mac) {
                m_callbacks.publish_ev_mac(item.mac);
            }
            break;

        case effect::kind::start_timer:
            if (not timer_for(item.timer).set_timeout_ms(item.timeout_ms)) {
                EVLOG_error << "McsDataLink: failed to arm the " << to_string(item.timer) << " timer";
            } else if (item.timer == timer_id::sync_repetition) {
                m_sync_window_open = true;
            }
            break;

        case effect::kind::stop_timer:
            if (not timer_for(item.timer).disarm()) {
                EVLOG_error << "McsDataLink: failed to disarm the " << to_string(item.timer) << " timer";
            }
            if (item.timer == timer_id::sync_repetition) {
                m_sync_window_open = false;
            }
            break;
        }
    }
}

everest::lib::io::event::timer_fd& datalink_controller::timer_for(timer_id id) {
    switch (id) {
    case timer_id::retry_wait:
        return m_retry_wait_timer;
    case timer_id::sync_repetition:
        return m_sync_repetition_timer;
    case timer_id::link_detect:
        break;
    }
    return m_link_detect_timer;
}

// --- watcher events ---------------------------------------------------------------------------

void datalink_controller::on_carrier_change(bool up) {
    EVLOG_info << "McsDataLink: carrier on " << m_config.device << " went " << (up ? "up" : "down");
    if (up) {
        // The kernel re-runs duplicate address detection on the edge; the link-local address is usable
        // about a second later, within the ISO 15118-10 timers.
        m_fsm.carrier_up();
    } else {
        forget_neighbors();
        m_fsm.carrier_down();
    }
    run_effects();
}

void datalink_controller::on_presence_change(bool present) {
    if (present) {
        EVLOG_info << "McsDataLink: device " << m_config.device << " appeared";
        clear_fault();
        return;
    }

    EVLOG_warning << "McsDataLink: device " << m_config.device << " disappeared";
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
            EVLOG_warning << "McsDataLink: no reachable neighbour left on " << m_config.device
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
    // V2G10-036: link loss detected without a carrier edge; the SECC has no PHY level peer signal.
    EVLOG_warning << "McsDataLink: no neighbour on " << m_config.device << " recovered within "
                  << m_config.liveness_grace_ms << " ms, treating the data link as lost";
    m_fsm.link_lost();
    run_effects();
}

void datalink_controller::on_link_detect_timeout() {
    EVLOG_warning << "McsDataLink: TT_EV_link_detect (" << m_config.link_detect_timeout_ms
                  << " ms) expired without a link on " << m_config.device
                  << "; communication initialization FAILED (V2G10-054)";
    // V2G10-056: repeat while TT_sync_repetition is open. With both defaults at 4 s the window is
    // already closed here; repetition only fires with a shortened link_detect_timeout_ms.
    m_fsm.link_detect_timeout(m_sync_window_open);
    run_effects();
}

void datalink_controller::on_sync_repetition_elapsed() {
    // V2G10-058: no further repetition. A running attempt continues; only its failure is now final.
    m_sync_window_open = false;
    EVLOG_info << "McsDataLink: TT_sync_repetition (" << m_config.sync_repetition_ms
               << " ms) expired; no further communication initialization retries for this "
                  "connection attempt";
}

void datalink_controller::on_retry_wait_elapsed() {
    m_fsm.retry_wait_elapsed(m_watcher.carrier_up());
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
        // Re-arming on every NUD_FAILED would push the deadline out indefinitely.
        return;
    }
    if (m_liveness_grace_timer.set_timeout_ms(m_config.liveness_grace_ms)) {
        m_liveness_grace_armed = true;
    } else {
        EVLOG_error << "McsDataLink: failed to arm the liveness grace timer";
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
    EVLOG_error << "McsDataLink: " << message;
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

internal_state datalink_controller::state() const {
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
