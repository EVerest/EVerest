// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "protocol/cb_config.h"
#include <charge_bridge/charge_bridge.hpp>
#include <charge_bridge/discovery.hpp>
#include <charge_bridge/firmware_update/sync_fw_updater.hpp>
#include <charge_bridge/heartbeat_service.hpp>
#include <charge_bridge/io_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/print_config.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <charge_bridge/utilities/sync_udp_client.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/netlink/vcan_netlink_manager.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/util/misc/bind.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

namespace charge_bridge {

namespace {
constexpr auto discovery_attempt_timeout = std::chrono::seconds(10);
constexpr auto discovery_retry_delay = std::chrono::seconds(1);
constexpr auto manager_base_cycle = std::chrono::seconds(10);
// Consecutive failed liveness probes before a heartbeat-less ChargeBridge is declared gone. Two
// probes debounce a single lost datagram (the probe itself already retries), at the price of one
// extra manager cycle: a device that stops answering is detected after two failed probes, i.e. after
// at most 2 * (manager_base_cycle + failed probe) ~ 21 s.
constexpr int liveness_probe_failure_limit = 2;

// Request/reply budget of a single liveness probe, deliberately far below the firmware defaults
// (3 x 3000 ms = 9 s): the probe runs on the manager thread, so its budget is what a *failed* probe
// costs that thread and adds to the detection latency above. Three 200 ms reply windows keep a
// single lost datagram from failing the probe (the ping is retransmitted after each) and bound a
// failed probe at ~600 ms - well inside the 10 s cycle. A reachable device answers the first
// attempt, so the budget costs a healthy probe nothing.
constexpr std::uint16_t liveness_probe_timeout_ms = 200;
constexpr std::uint16_t liveness_probe_retries = 3;

std::pair<bool, std::set<std::string>> make_interface_list(std::string const& str, std::string const& pattern) {
    auto const raw = utilities::string_after_pattern(str, pattern);
    if (raw.size() < 3 || raw.front() != '(' || raw.back() != ')') {
        return {false, {}};
    }
    auto const list = raw.substr(1, raw.size() - 2);
    auto exclude = list.front() == '!';
    auto items = utilities::csv_to_set(list.substr(exclude ? 1 : 0));
    // No logging here: this has no instance context, and the configured list is already reported by
    // print_charge_bridge_config ("* remote:") and the interfaces actually used by discovery.
    return {exclude, items};
}

const int mqtt_reconnect_timeout_ms = 1000;

// Releases a monitor handle for the duration of a blocking, self-contained operation and re-acquires
// it on every exit path, including exception unwinding. The manager loop reads guarded state and
// waits on the same handle, so leaving its scope with the lock released would mean calling wait_for()
// on a non-owning lock - std::terminate.
//
// The invariant covers the whole unlocked window, not just the statement that blocks: no code that
// runs inside it - the operation itself, any callback it invokes and any exception handler that runs
// before the scope is left - may touch the guarded state, because reading or writing it without the
// lock is a data race.
//
// Logging from inside the window is fine, and the named call sites do it extensively (update_firmware()
// reports its whole progress from in there): the output sink takes its own lock and releases it again
// before returning, so that lock is never held while this guard re-acquires the monitor and no lock
// order can be inverted. The firmware-update and liveness-probe call sites still catch an exception
// inside the window, carry the message out and report it after the monitor is back - that keeps the
// report next to the state updates it belongs to, but it is not something this guard requires.
//
// The destructor relocks and is implicitly noexcept, so a throwing lock() (a mutex error) terminates
// the process. That is deliberate: the alternative is to continue with a handle that does not own its
// lock, which terminates in the wait_for() above anyway, after racy reads of the guarded state.
template <class HandleT> class scoped_monitor_unlock {
public:
    explicit scoped_monitor_unlock(HandleT& handle) : m_handle(handle) {
        m_handle.unlock();
    }
    ~scoped_monitor_unlock() {
        m_handle.lock();
    }
    scoped_monitor_unlock(scoped_monitor_unlock const&) = delete;
    scoped_monitor_unlock& operator=(scoped_monitor_unlock const&) = delete;

private:
    HandleT& m_handle;
};

// Runs one bridge constructor in isolation. Every bridge owns host-local devices whose creation can
// fail on its own (the vcan device needs CAP_NET_ADMIN, a pty symlink needs a writable target, ...),
// so such a failure must neither keep the remaining bridges from coming up nor escape to the caller:
// the missing bridge is simply retried on the next attempt. An existing object is left untouched.
// Failures are reported once per bridge (see failures_reported) because the retry runs on the ~10 s
// manager cadence and a permanently missing capability would otherwise flood the log.
template <class BridgeT, class FactoryT>
void create_bridge(std::string const& cb_name, std::string const& bridge_name, std::unique_ptr<BridgeT>& bridge,
                   std::set<std::string>& failures_reported, FactoryT&& factory) {
    if (bridge) {
        return;
    }
    try {
        bridge = factory();
        failures_reported.erase(bridge_name);
    } catch (std::exception const& e) {
        if (failures_reported.insert(bridge_name).second) {
            utilities::print_error(cb_name, "RUNTIME", -1)
                << "Failed to create " << bridge_name << ": " << e.what() << std::endl;
        }
    } catch (...) {
        if (failures_reported.insert(bridge_name).second) {
            utilities::print_error(cb_name, "RUNTIME", -1) << "Failed to create " << bridge_name << std::endl;
        }
    }
}

// Brings a bridge that was created after the internal runtime had already come up (see
// retry_missing_bridges) into that running runtime: connects its CB-side endpoint and registers its
// fds with the event loop, i.e. exactly the two steps start_internal_runtime() performs for the
// bridges that existed back then. Must only be called for a bridge that has just been created:
// register_event_handler() rejects an fd that is already in the epoll set, so handing it a live
// bridge would report a bogus failure and then tear a working bridge down.
// If activation fails the bridge is dropped again. That restores the exact state before the retry, so
// the next cadence retries the whole create + activate sequence instead of leaving an object behind
// that is never registered and can therefore never recover.
template <class BridgeT, class ConfigT>
void activate_late_bridge(everest::lib::io::event::fd_event_handler& handler, std::string const& cb_name,
                          std::string const& bridge_name, std::unique_ptr<BridgeT>& bridge,
                          std::optional<ConfigT> const& config, std::set<std::string>& failures_reported) {
    if (not bridge or not config.has_value()) {
        return;
    }
    try {
        bridge->connect_cb_endpoint(config->cb_remote);
        if (handler.register_event_handler(bridge.get())) {
            failures_reported.erase(bridge_name);
            return;
        }
        // Registration can stop half way through the bridge's fds, so drop whatever made it in
        // before the object - and with it its fds - goes away below.
        handler.unregister_event_handler(bridge.get());
    } catch (...) {
        // Nothing is registered on this path (connect_cb_endpoint() threw), so the reset below is
        // all the cleanup needed.
    }
    bridge.reset();
    // Reported once per failure episode: the retry runs on the ~10 s manager cadence.
    if (failures_reported.insert(bridge_name).second) {
        utilities::print_error(cb_name, "RUNTIME", 1)
            << "Failed to activate the newly created " << bridge_name << " (retrying in next cycle)" << std::endl;
    }
}

endpoint_intent_info parse_endpoint_intent(std::string const& cb_remote) {
    endpoint_intent_info result;

    if (utilities::string_starts_with(cb_remote, "ANY_EVSE")) {
        auto params = make_interface_list(cb_remote, "ANY_EVSE");
        result.value = endpoint_intent::any_evse_mdns;
        result.excluding_interfaces = params.first;
        result.interfaces = params.second;
    } else if (utilities::string_starts_with(cb_remote, "ANY_EV")) {
        auto params = make_interface_list(cb_remote, "ANY_EV");
        result.value = endpoint_intent::any_ev_mdns;
        result.excluding_interfaces = params.first;
        result.interfaces = params.second;
    }

    return result;
}

} // namespace

charge_bridge::charge_bridge(charge_bridge_config const& config,
                             std::function<void(utilities::chargebridge_status)> status_sink,
                             std::function<void(utilities::chargebridge_status)> tick_sink) :
    // Order matches the declaration order in the header (see the member-lifetime comment there).
    m_config(config), m_status_sink(std::move(status_sink)), m_tick_sink(std::move(tick_sink)) {
    m_endpoint_intent = parse_endpoint_intent(config.cb_remote);
}

void charge_bridge::init_discovery(discovery_device_type type, std::set<std::string> const& interfaces,
                                   bool excluding) {
    using namespace everest::lib::util;
    utilities::print_error(m_config.cb_name, "DISCOVERY", -1) << "Discovery pending" << std::endl;

    m_discovery = std::make_unique<discovery>(type, interfaces, excluding, m_config.cb_name);
    m_discovery->set_discovery_callback(bind_obj(&charge_bridge::handle_discovery, this));
    set_discovery_pending(true);
}

bool charge_bridge::is_mdns_endpoint() const {
    return m_endpoint_intent.value != endpoint_intent::fixed_ip;
}

discovery_device_type charge_bridge::mdns_device_type() const {
    if (m_endpoint_intent.value == endpoint_intent::any_evse_mdns) {
        return discovery_device_type::CB_EVSE;
    }
    return discovery_device_type::CB_EV;
}

std::set<std::string> charge_bridge::select_discovery_interfaces() const {
    std::set<std::string> available_interfaces;
    try {
        for (auto const& item : everest::lib::io::socket::get_all_interfaces()) {
            available_interfaces.insert(item.name);
        }
    } catch (std::exception const&) {
        return {};
    }

    if (m_endpoint_intent.interfaces.empty()) {
        return available_interfaces;
    }

    std::set<std::string> selected_interfaces;
    if (m_endpoint_intent.excluding_interfaces) {
        for (auto const& item : available_interfaces) {
            if (m_endpoint_intent.interfaces.count(item) == 0) {
                selected_interfaces.insert(item);
            }
        }
        return selected_interfaces;
    }

    for (auto const& item : m_endpoint_intent.interfaces) {
        if (available_interfaces.count(item) > 0) {
            selected_interfaces.insert(item);
        }
    }
    return selected_interfaces;
}

void charge_bridge::start_discovery_attempt(std::set<std::string> const& interfaces) {
    if (not m_event_handler) {
        return;
    }
    auto type = mdns_device_type();
    m_event_handler->add_action([this, type, interfaces]() {
        try {
            if (m_discovery) {
                m_event_handler->unregister_event_handler(m_discovery.get());
            }
            m_discovery.reset();
            init_discovery(type, interfaces, false);
            auto registered = m_event_handler->register_event_handler(m_discovery.get());
            if (not registered) {
                m_event_handler->unregister_event_handler(m_discovery.get());
                utilities::print_error(m_config.cb_name, "DISCOVERY", -1)
                    << "Failed to register mDNS discovery handler" << std::endl;
                std::unique_ptr<discovery> tmp;
                std::swap(m_discovery, tmp);
                set_discovery_pending(true);
            }
        } catch (std::exception const& e) {
            utilities::print_error(m_config.cb_name, "DISCOVERY", -1)
                << "Failed to start mDNS discovery: " << e.what() << std::endl;
            if (m_discovery) {
                m_event_handler->unregister_event_handler(m_discovery.get());
            }
            std::unique_ptr<discovery> tmp;
            std::swap(m_discovery, tmp);
            set_discovery_pending(true);
        }
        m_cb_status.notify_one();
    });
}

void charge_bridge::stop_discovery() {
    if (not m_event_handler) {
        return;
    }
    m_event_handler->add_action([this]() {
        std::unique_ptr<discovery> tmp;
        if (m_discovery) {
            m_event_handler->unregister_event_handler(m_discovery.get());
        }
        std::swap(m_discovery, tmp);
    });
}

void charge_bridge::handle_discovery(everest::lib::io::mdns::mDNS_discovery const& info) {
    // IPv4 preferred, IPv6 (with %scope for link-local) as fallback. With dual
    // transports the AAAA-bearing packet may win the race against the A record;
    // the first usable address of the current registry snapshot is used.
    auto const ip = everest::lib::io::mdns::select_address(info);
    utilities::print_error(m_config.cb_name, "DISCOVERY", 0) << "Discovered at: " + ip << std::endl;

    m_discovery_info = info;
    m_config.cb_remote = ip;
    if (m_config.can0) {
        m_config.can0->cb_remote = ip;
    }
    if (m_config.serial1) {
        m_config.serial1->cb_remote = ip;
    }
    if (m_config.serial2) {
        m_config.serial2->cb_remote = ip;
    }
    if (m_config.serial3) {
        m_config.serial3->cb_remote = ip;
    }
    if (m_config.plc) {
        m_config.plc->cb_remote = ip;
    }
    if (m_config.bsp) {
        m_config.bsp->cb_remote = ip;
    }
    if (m_config.heartbeat) {
        m_config.heartbeat->cb_remote = ip;
    }
    if (m_config.io) {
        m_config.io->cb_remote = ip;
    }

    m_config.firmware.cb_remote = ip;

    m_event_handler->add_action([this]() {
        std::unique_ptr<discovery> tmp;
        if (m_discovery) {
            m_event_handler->unregister_event_handler(m_discovery.get());
        }
        std::swap(m_discovery, tmp);

        set_discovery_pending(false);
    });
}

void charge_bridge::set_discovery_pending(bool pending) {
    auto handle = m_cb_status.handle();
    set_discovery_pending(*handle, pending);
}

void charge_bridge::set_discovery_pending(charge_bridge_status& status, bool pending) {
    auto changed = status.discovery_pending != pending;
    status.discovery_pending = pending;
    m_cb_status.notify_one();
    if (changed) {
        m_ready_notify.notify();
    }
}

void charge_bridge::set_bridges_cb_connection_status(bool connected) {
    if (m_plc) {
        m_plc->set_cb_connection_status(connected);
    }
    if (m_io) {
        m_io->set_cb_connection_status(connected);
    }
    if (m_can_0_client) {
        m_can_0_client->set_cb_connection_status(connected);
    }
}

// Without a 'heartbeat' config block there is no liveness signal from the ChargeBridge, so the
// connection state is derived from the internal runtime lifecycle instead: connected once the
// runtime is up (which only happens after update_firmware()'s successful connection probe) and
// disconnected once it has been torn down. Called from the manager thread with the status monitor
// already locked; the bridges' observables live on the event loop thread, so the cb connection
// status is applied there. No-op when a heartbeat service is configured: it owns is_connected then.
//
// Keying on the config rather than on m_heartbeat is safe because start_internal_runtime() refuses to
// start a runtime whose configured heartbeat service does not exist, which establishes the invariant
// "m_internal_runtime_started implies m_heartbeat != nullptr whenever a heartbeat is configured". So a
// configured heartbeat that stands down here always has an object writing is_connected instead.
// (m_heartbeat itself must not be read here at all: it belongs to the event loop thread, and the
// construction retry may create it while the manager is running.)
void charge_bridge::set_runtime_connection_status(charge_bridge_status& status, bool connected) {
    if (m_config.heartbeat.has_value()) {
        return;
    }
    status.is_connected = connected;
    if (m_event_handler) {
        m_event_handler->add_action([this, connected]() { set_bridges_cb_connection_status(connected); });
    }
    m_cb_status.notify_one();
}

// True while the connection state has to be derived from an explicit probe: no heartbeat service
// owns is_connected, the runtime is up and currently considered connected. With a heartbeat block
// this is always false, so the heartbeat behaviour is untouched — and because a started runtime
// always has its configured heartbeat service (see set_runtime_connection_status), "no heartbeat
// configured" and "no heartbeat object" mean the same thing for a started runtime.
bool charge_bridge::needs_liveness_probe(charge_bridge_status const& status) const {
    return not m_config.heartbeat.has_value() and m_internal_runtime_started and m_was_connected and
           status.is_connected;
}

// Cheap, silent liveness check: the same management-port request/reply ping the reconnect path uses
// (sync_fw_updater::ping() logs nothing, unlike quick_check_connection()), on a short-lived socket.
// Explicitly budgeted instead of running the firmware default of 9 s, which no periodic caller can
// afford (see liveness_probe_timeout_ms). The abort check is armed so a pending shutdown is not
// delayed by the remaining budget, and ping() matches the reply against the ping tag, so a late reply
// to some earlier request cannot pass as this probe's answer.
// A socket that never opened needs no special case: udp tx on a closed socket fails, and
// request_reply() gives up on the first failed transmission, so such a probe returns immediately.
// Blocks, so the caller must not hold the status monitor.
bool charge_bridge::probe_device_liveness(std::function<bool()> const& abort_requested) {
    firmware_update::sync_fw_updater updater(m_config.firmware, abort_requested);
    return updater.ping(liveness_probe_timeout_ms, liveness_probe_retries);
}

std::future<bool> charge_bridge::start_internal_runtime() {
    auto promise = std::make_shared<std::promise<bool>>();
    auto result = promise->get_future();

    if (not m_event_handler) {
        promise->set_value(false);
        return result;
    }

    m_event_handler->add_action([this, promise = std::move(promise)]() mutable {
        try {
            m_runtime_start_failure_reason.clear();
            // Construction is normally done eagerly at startup (see create_internal_runtime_eagerly),
            // so this only retries bridges whose local devices could not be created back then.
            create_internal_runtime();
            if (has_configured_bridge() and not has_existing_bridge()) {
                // Not a single configured bridge exists: there is nothing to connect or register, and
                // reporting success would latch the runtime as started and stop the construction
                // retries. Report the start as failed instead; the manager logs it and retries the
                // whole sequence — construction included — in the next cycle.
                m_runtime_start_failure_reason = "no bridge could be created";
                promise->set_value(false);
                m_cb_status.notify_one();
                return;
            }
            if (m_config.heartbeat.has_value() and not m_heartbeat) {
                // The heartbeat service is the liveness source of a config that has one: it is the only
                // writer of is_connected then, and both set_runtime_connection_status() and
                // needs_liveness_probe() stand down for it. A runtime started without it would have no
                // writer at all, so the connection state could never change again - and once anything
                // cleared it, the manager would tear the runtime down and rebuild it every cycle.
                // Report the start as failed instead: the manager retries the whole sequence,
                // construction included, so the runtime latches as started exactly when its liveness
                // source exists. The bridges that were created keep their host-local devices; only the
                // CB-side sockets their constructors brought up are dropped, which leaves the runtime
                // in the same state as after create_internal_runtime_eagerly().
                disconnect_internal_runtime_endpoints();
                m_runtime_start_failure_reason = "the heartbeat service could not be created";
                promise->set_value(false);
                m_cb_status.notify_one();
                return;
            }
            connect_internal_runtime_endpoints();
            auto runtime_registered = register_internal_events(*m_event_handler);
            if (not runtime_registered) {
                unregister_internal_runtime_events(*m_event_handler);
                // Recycle the CB side only. The bridge objects own host-local devices (pty + symlink,
                // vcan, tap) that EVerest modules are configured against, so a failure on the
                // ChargeBridge side must never make those devices disappear.
                disconnect_internal_runtime_endpoints();
                m_runtime_start_failure_reason = "no bridge could be connected and registered";
                promise->set_value(false);
                m_cb_status.notify_one();
                return;
            }

            promise->set_value(true);
            m_cb_status.notify_one();
        } catch (...) {
            unregister_internal_runtime_events(*m_event_handler);
            disconnect_internal_runtime_endpoints();
            promise->set_exception(std::current_exception());
            m_cb_status.notify_one();
        }
    });

    return result;
}

// Constructs the bridge objects and with them the host-local devices they own: the serial ptys and
// their symlinks, the vcan netlink device and the PLC tap device. None of this requires the
// ChargeBridge to be reachable; the CB-side sockets created along the way are dropped by the caller
// and (re)established by connect_internal_runtime_endpoints() once the CB answers. Objects that
// already exist are left untouched, so this is a no-op after a successful eager creation and a
// retry for whatever failed. Must not run concurrently with the event loop touching the bridges.
// Every bridge is constructed in isolation (see create_bridge): one bridge that cannot bring up its
// host-local device does not suppress the others, and is retried on the next call.
void charge_bridge::create_internal_runtime() {
    if (m_config.can0.has_value()) {
        create_bridge(m_config.cb_name, "can bridge", m_can_0_client, m_bridge_create_failures_reported,
                      [this]() {
                          auto cfg = m_config.can0.value();
                          // Bus-rate pacing needs the CB's CAN bitrate; the heartbeat config carries it.
                          if (m_config.heartbeat.has_value()) {
                              switch (m_config.heartbeat->cb_config.can.baudrate) {
                              case CBCBR_125000:
                                  cfg.can_bitrate_bps = 125000;
                                  break;
                              case CBCBR_250000:
                                  cfg.can_bitrate_bps = 250000;
                                  break;
                              case CBCBR_500000:
                                  cfg.can_bitrate_bps = 500000;
                                  break;
                              case CBCBR_1000000:
                                  cfg.can_bitrate_bps = 1000000;
                                  break;
                              default:
                                  break;
                              }
                          }
                          return std::make_unique<can_bridge>(cfg, m_ready_notify);
                      });
    }
    if (m_config.serial1.has_value()) {
        create_bridge(m_config.cb_name, "serial bridge 1", m_pty_1, m_bridge_create_failures_reported,
                      [this]() { return std::make_unique<serial_bridge>(m_config.serial1.value(), m_ready_notify); });
    }
    if (m_config.serial2.has_value()) {
        create_bridge(m_config.cb_name, "serial bridge 2", m_pty_2, m_bridge_create_failures_reported,
                      [this]() { return std::make_unique<serial_bridge>(m_config.serial2.value(), m_ready_notify); });
    }
    if (m_config.serial3.has_value()) {
        create_bridge(m_config.cb_name, "serial bridge 3", m_pty_3, m_bridge_create_failures_reported,
                      [this]() { return std::make_unique<serial_bridge>(m_config.serial3.value(), m_ready_notify); });
    }
    if (m_config.plc.has_value()) {
        create_bridge(m_config.cb_name, "plc bridge", m_plc, m_bridge_create_failures_reported,
                      [this]() { return std::make_unique<plc_bridge>(m_config.plc.value(), m_ready_notify); });
    }
    if (m_config.bsp.has_value()) {
        create_bridge(m_config.cb_name, "bsp bridge", m_bsp, m_bridge_create_failures_reported,
                      [this]() { return std::make_unique<bsp_bridge>(m_config.bsp.value(), m_ready_notify); });
    }
    if (m_config.io.has_value()) {
        create_bridge(m_config.cb_name, "io bridge", m_io, m_bridge_create_failures_reported,
                      [this]() { return std::make_unique<io_bridge>(m_config.io.value(), m_ready_notify); });
    }
    if (m_config.heartbeat.has_value()) {
        create_bridge(m_config.cb_name, "heartbeat service", m_heartbeat, m_bridge_create_failures_reported, [this]() {
            auto heartbeat_cb = [this](bool connected) {
                {
                    auto handle = m_cb_status.handle();
                    handle->is_connected = connected;
                }
                set_bridges_cb_connection_status(connected);
                m_cb_status.notify_one();
            };

            return std::make_unique<heartbeat_service>(m_config.heartbeat.value(), heartbeat_cb, m_ready_notify);
        });
    }
}

// True if the config asks for at least one bridge, i.e. if an empty runtime means something failed.
bool charge_bridge::has_configured_bridge() const {
    return m_config.can0.has_value() or m_config.serial1.has_value() or m_config.serial2.has_value() or
           m_config.serial3.has_value() or m_config.plc.has_value() or m_config.bsp.has_value() or
           m_config.io.has_value() or m_config.heartbeat.has_value();
}

// True if at least one bridge object exists, i.e. if there is anything to connect and register.
bool charge_bridge::has_existing_bridge() const {
    return m_can_0_client or m_pty_1 or m_pty_2 or m_pty_3 or m_plc or m_bsp or m_io or m_heartbeat;
}

// True if the config asks for a bridge whose object does not exist, i.e. if a construction retry has
// anything to do. False for a complete runtime, which makes retry_missing_bridges() a no-op then.
// Reads the bridge pointers, so it must run on the event loop thread once the loop is up.
bool charge_bridge::has_missing_configured_bridge() const {
    return (m_config.can0.has_value() and not m_can_0_client) or (m_config.serial1.has_value() and not m_pty_1) or
           (m_config.serial2.has_value() and not m_pty_2) or (m_config.serial3.has_value() and not m_pty_3) or
           (m_config.plc.has_value() and not m_plc) or (m_config.bsp.has_value() and not m_bsp) or
           (m_config.io.has_value() and not m_io) or (m_config.heartbeat.has_value() and not m_heartbeat);
}

// Retries the construction of configured bridges that are still missing while the internal runtime is
// up. start_internal_runtime() - the other place that (re)creates bridges - is only reachable while
// the ChargeBridge is not connected, so without this a bridge whose host-local device could not be
// created (no CAP_NET_ADMIN for the vcan device, a symlink target that was not writable yet, ...)
// would stay missing for the rest of the session even after the cause is gone.
// The work runs on the event loop thread: it is the only thread allowed to touch the bridge objects
// while the loop is running, and it is where the decision whether anything is missing has to be made.
// Only the bridges created by this call are connected and registered - the already-registered ones are
// not touched at all, so a working runtime cannot be disturbed.
void charge_bridge::retry_missing_bridges() {
    if (not m_event_handler) {
        return;
    }
    m_event_handler->add_action([this]() {
        if (not has_missing_configured_bridge()) {
            return;
        }

        // What is missing before create_internal_runtime() runs is exactly what may be activated
        // afterwards: re-registering a live bridge fails (its fds are already in the epoll set) and
        // re-connecting one would drop the CB endpoint it is currently talking to.
        auto const missing_can0 = not m_can_0_client;
        auto const missing_serial1 = not m_pty_1;
        auto const missing_serial2 = not m_pty_2;
        auto const missing_serial3 = not m_pty_3;
        auto const missing_plc = not m_plc;
        auto const missing_bsp = not m_bsp;
        auto const missing_io = not m_io;
        auto const missing_heartbeat = not m_heartbeat;

        create_internal_runtime();

        auto activate = [this](bool was_missing, auto& bridge, auto const& config, std::string const& bridge_name) {
            if (not was_missing) {
                return;
            }
            activate_late_bridge(*m_event_handler, m_config.cb_name, bridge_name, bridge, config,
                                 m_bridge_activate_failures_reported);
        };

        activate(missing_can0, m_can_0_client, m_config.can0, "can bridge");
        activate(missing_serial1, m_pty_1, m_config.serial1, "serial bridge 1");
        activate(missing_serial2, m_pty_2, m_config.serial2, "serial bridge 2");
        activate(missing_serial3, m_pty_3, m_config.serial3, "serial bridge 3");
        activate(missing_plc, m_plc, m_config.plc, "plc bridge");
        activate(missing_bsp, m_bsp, m_config.bsp, "bsp bridge");
        activate(missing_io, m_io, m_config.io, "io bridge");
        // Unreachable in practice: start_internal_runtime() does not latch a runtime whose configured
        // heartbeat service is missing, and this retry only runs for a started runtime. Kept so the
        // list stays exhaustive if that ever changes.
        activate(missing_heartbeat, m_heartbeat, m_config.heartbeat, "heartbeat service");

        // A bridge created here missed every set_bridges_cb_connection_status() that ran while it did
        // not exist, and nothing repeats that call for it: on a config without a heartbeat block the
        // state is published once per connection edge, so a late can/plc/io bridge would report
        // available() == false for the rest of the session. (Heartbeat configs heal themselves only
        // because the heartbeat republishes the state on every tick.) Apply the current state the way
        // heartbeat_cb does: read it under the monitor, then publish to the bridges with the lock
        // released - the observables belong to this thread, the flag belongs to the monitor.
        bool connected = false;
        {
            auto handle = m_cb_status.handle();
            connected = handle->is_connected;
        }
        set_bridges_cb_connection_status(connected);
    });
}

// Reports a failed internal runtime start once per failure episode: the manager retries on its ~10 s
// cadence, so logging every attempt would flood the log for a device that stays unusable, while the
// flag still lets the next failure after a successful start be reported.
void charge_bridge::report_runtime_start_failure(std::string const& reason) {
    if (m_runtime_start_failure_reported) {
        return;
    }
    m_runtime_start_failure_reported = true;
    utilities::print_error(m_config.cb_name, "RUNTIME", 1)
        << "Failed to start the internal runtime: " << reason << " (retrying in next cycle)" << std::endl;
}

// Creates the bridge objects at startup, before any connection to the ChargeBridge exists, so the
// host-local devices (pty symlinks, vcan, tap) are present from process start on: EVerest modules
// are configured against those device paths and open them during their own startup, which must work
// with the ChargeBridge unpowered or not yet discovered. A construction failure is reported but not
// fatal; start_internal_runtime() retries it on every connection attempt.
// The CB-side sockets the constructors bring up are dropped right away: the remote may not be known
// yet (mDNS) and connecting is the manager loop's business. That leaves the runtime in exactly the
// same state as after stop_internal_runtime(), so first connect and reconnect share one code path.
void charge_bridge::create_internal_runtime_eagerly() {
    try {
        // create_internal_runtime() already isolates and reports every per-bridge failure, so this
        // only catches something unexpected escaping it (a bad_alloc while logging, for instance).
        create_internal_runtime();
    } catch (std::exception const& e) {
        utilities::print_error(m_config.cb_name, "RUNTIME", -1)
            << "Failed to create local devices: " << e.what() << std::endl;
    } catch (...) {
        utilities::print_error(m_config.cb_name, "RUNTIME", -1) << "Failed to create local devices" << std::endl;
    }
    disconnect_internal_runtime_endpoints();
}

// (Re)connects the CB-side socket of every existing bridge to the current remote address (the
// configured one, or the one mDNS discovery found). The host-local devices are not touched.
void charge_bridge::connect_internal_runtime_endpoints() {
    if (m_can_0_client and m_config.can0.has_value()) {
        m_can_0_client->connect_cb_endpoint(m_config.can0->cb_remote);
    }
    if (m_pty_1 and m_config.serial1.has_value()) {
        m_pty_1->connect_cb_endpoint(m_config.serial1->cb_remote);
    }
    if (m_pty_2 and m_config.serial2.has_value()) {
        m_pty_2->connect_cb_endpoint(m_config.serial2->cb_remote);
    }
    if (m_pty_3 and m_config.serial3.has_value()) {
        m_pty_3->connect_cb_endpoint(m_config.serial3->cb_remote);
    }
    if (m_bsp and m_config.bsp.has_value()) {
        m_bsp->connect_cb_endpoint(m_config.bsp->cb_remote);
    }
    if (m_plc and m_config.plc.has_value()) {
        m_plc->connect_cb_endpoint(m_config.plc->cb_remote);
    }
    if (m_io and m_config.io.has_value()) {
        m_io->connect_cb_endpoint(m_config.io->cb_remote);
    }
    if (m_heartbeat and m_config.heartbeat.has_value()) {
        m_heartbeat->connect_cb_endpoint(m_config.heartbeat->cb_remote);
    }
}

void charge_bridge::disconnect_internal_runtime_endpoints() {
    if (m_can_0_client) {
        m_can_0_client->disconnect_cb_endpoint();
    }
    if (m_pty_1) {
        m_pty_1->disconnect_cb_endpoint();
    }
    if (m_pty_2) {
        m_pty_2->disconnect_cb_endpoint();
    }
    if (m_pty_3) {
        m_pty_3->disconnect_cb_endpoint();
    }
    if (m_bsp) {
        m_bsp->disconnect_cb_endpoint();
    }
    if (m_plc) {
        m_plc->disconnect_cb_endpoint();
    }
    if (m_io) {
        m_io->disconnect_cb_endpoint();
    }
    if (m_heartbeat) {
        m_heartbeat->disconnect_cb_endpoint();
    }
}

bool charge_bridge::unregister_internal_runtime_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    if (m_can_0_client) {
        result = handler.unregister_event_handler(m_can_0_client.get()) && result;
    }
    if (m_pty_1) {
        result = handler.unregister_event_handler(m_pty_1.get()) && result;
    }
    if (m_pty_2) {
        result = handler.unregister_event_handler(m_pty_2.get()) && result;
    }
    if (m_pty_3) {
        result = handler.unregister_event_handler(m_pty_3.get()) && result;
    }
    if (m_bsp) {
        result = handler.unregister_event_handler(m_bsp.get()) && result;
    }
    if (m_plc) {
        result = handler.unregister_event_handler(m_plc.get()) && result;
    }
    if (m_heartbeat) {
        result = handler.unregister_event_handler(m_heartbeat.get()) && result;
    }
    if (m_io) {
        result = handler.unregister_event_handler(m_io.get()) && result;
    }
    return result;
}

std::future<bool> charge_bridge::stop_internal_runtime() {
    auto promise = std::make_shared<std::promise<bool>>();
    auto result = promise->get_future();

    if (not m_event_handler) {
        // manage() installs the event handler before anything is connected or registered, so without
        // it there is nothing to unregister or disconnect. The bridge objects are kept: they own
        // host-local devices (pty + symlink, vcan, tap) that EVerest modules are configured against
        // and that must not disappear behind their back.
        promise->set_value(true);
        return result;
    }
    m_event_handler->add_action([this, promise = std::move(promise)]() mutable {
        try {
            unregister_internal_runtime_events(*m_event_handler);
            disconnect_internal_runtime_endpoints();
            promise->set_value(true);
        } catch (...) {
            disconnect_internal_runtime_endpoints();
            promise->set_exception(std::current_exception());
        }
        m_cb_status.notify_one();
    });

    return result;
}

charge_bridge::~charge_bridge() {
    m_cb_status.notify_one();
    if (m_manager.joinable()) {
        m_manager.join();
    }
}

void charge_bridge::manage(everest::lib::io::event::fd_event_handler& handler, std::atomic_bool const& run,
                           bool force_update) {
    if (m_manager.joinable()) {
        utilities::print_error(m_config.cb_name, "MANAGER", -1)
            << "manage() called while manager thread is already running" << std::endl;
        return;
    }

    using namespace std::chrono_literals;
    m_event_handler = &handler;
    m_force_firmware_update = force_update;

    // Bring up the host-local devices before anything else and independent of the ChargeBridge being
    // reachable. Done on the calling thread: manage() runs before the event loop is started and the
    // bridges are not registered with it yet, so there is nothing to race with.
    create_internal_runtime_eagerly();

    m_event_handler->add_action([this]() {
        if (m_config.telemetry.has_value()) {
            m_mqtt = std::make_unique<everest::lib::io::mqtt::mqtt_client>(mqtt_reconnect_timeout_ms);
            // Required before connect() so m_connected is set on CONNACK; else publish() drops all.
            m_mqtt->set_callback_connect([](auto&, auto, auto, auto const&) {});
            m_mqtt->connect(m_config.telemetry->mqtt_bind, m_config.telemetry->mqtt_remote,
                            m_config.telemetry->mqtt_port, m_config.telemetry->mqtt_ping_interval_ms);
        }
        // Drive the 1s status/telemetry tick regardless of telemetry config or broker reachability,
        // so the status report and the terminal UI's live readouts refresh even with telemetry
        // disabled or the MQTT broker down. (timer_fd is periodic, so a single arm keeps it firing.)
        m_1s_tick.set_timeout(std::chrono::seconds(1));
        // Register the manage events (readiness notifier + tick) regardless of telemetry, so the
        // status report is produced even when telemetry/MQTT is disabled. The MQTT fd itself is only
        // registered when the telemetry client exists (see register_manage_events).
        register_manage_events(*m_event_handler);
    });

    if (is_mdns_endpoint()) {
        set_discovery_pending(true);
    }

    using clock = std::chrono::steady_clock;
    auto action = [this, &run](auto& status_handle, charge_bridge_status& current_status,
                               std::optional<clock::time_point>& next_connect_retry_time,
                               std::future<bool>& startup_runtime, bool& startup_runtime_in_progress,
                               std::optional<clock::time_point>& discovery_attempt_deadline,
                               std::optional<clock::time_point>& discovery_retry_time, std::future<bool>& stop_runtime,
                               bool& runtime_stop_in_progress) {
        // Shutdown already requested: do not start anything new (a connection probe or a firmware
        // upload would only delay the join in the destructor).
        if (not run.load()) {
            return;
        }
        auto now = clock::now();
        if (runtime_stop_in_progress) {
            if (stop_runtime.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }
            try {
                if (not stop_runtime.get()) {
                    utilities::print_error(m_config.cb_name, "RUNTIME", 1)
                        << "Stopping the internal runtime reported a failure" << std::endl;
                }
            } catch (std::exception const& e) {
                utilities::print_error(m_config.cb_name, "RUNTIME", 1)
                    << "Stopping the internal runtime failed: " << e.what() << std::endl;
            } catch (...) {
                utilities::print_error(m_config.cb_name, "RUNTIME", 1)
                    << "Stopping the internal runtime failed with an unknown exception" << std::endl;
            }
            runtime_stop_in_progress = false;
            m_internal_runtime_started = false;
            m_was_connected = false;
            m_liveness_probe_failures = 0;
            m_next_liveness_probe.reset();
            m_next_bridge_retry.reset();
            set_runtime_connection_status(current_status, false);

            if (is_mdns_endpoint()) {
                set_discovery_pending(current_status, true);
                m_discovery_active = false;
                next_connect_retry_time.reset();
                discovery_attempt_deadline.reset();
                discovery_retry_time.reset();
                return;
            }
        }
        if (next_connect_retry_time.has_value() && now < next_connect_retry_time.value()) {
            return;
        } else if (next_connect_retry_time.has_value()) {
            next_connect_retry_time.reset();
        }
        if (current_status.discovery_pending) {
            if (m_discovery_active) {
                if (discovery_attempt_deadline.has_value() && now >= discovery_attempt_deadline.value()) {
                    stop_discovery();
                    m_discovery_active = false;
                    discovery_attempt_deadline.reset();
                    discovery_retry_time = now + discovery_retry_delay;
                }
                return;
            }

            if (discovery_retry_time.has_value() && now < discovery_retry_time.value()) {
                return;
            }

            auto discovery_interfaces = select_discovery_interfaces();
            if (discovery_interfaces.empty()) {
                discovery_retry_time = now + discovery_retry_delay;
                return;
            }

            start_discovery_attempt(discovery_interfaces);
            m_discovery_active = true;
            discovery_attempt_deadline = now + discovery_attempt_timeout;
            discovery_retry_time.reset();
            return;
        }

        if (m_discovery_active) {
            m_discovery_active = false;
            discovery_attempt_deadline.reset();
            discovery_retry_time.reset();
        }

        // Liveness fallback for configs without a heartbeat block: nothing else ever clears
        // is_connected there, so an unplugged ChargeBridge would report connected forever and mDNS
        // would never re-discover it. Probing rides the regular manager cadence, so it adds no
        // wakeups - and none at all for heartbeat configs, which never get here. A reachable device
        // answers the first datagram, so a healthy cycle is one round trip; a failed probe blocks this
        // thread for its budget (~600 ms, see liveness_probe_timeout_ms), which stretches the cycle to
        // ~10.6 s and puts detection at up to two cycles (~21 s) after the device stopped answering.
        if (needs_liveness_probe(current_status)) {
            if (not m_next_liveness_probe.has_value()) {
                // The runtime just came up after a successful connection probe: start the schedule
                // instead of probing the device again right away.
                m_next_liveness_probe = now + manager_base_cycle;
            } else if (now >= m_next_liveness_probe.value()) {
                m_next_liveness_probe = now + manager_base_cycle;
                // Blocking UDP request/reply with retries. Release the monitor across it exactly like
                // update_firmware() below does, so get_status() on the event-loop thread - and with it
                // every other bridge instance - is not stalled. The guard re-acquires the lock on every
                // exit path; nothing is logged before it does.
                bool alive = false;
                {
                    scoped_monitor_unlock unlocked(status_handle);
                    try {
                        alive = probe_device_liveness([&run]() { return not run.load(); });
                    } catch (...) {
                        // A probe that cannot even be sent counts as a failed probe; the reason is
                        // reported by the reconnect path once the runtime has been torn down.
                        alive = false;
                    }
                }

                if (not run.load()) {
                    // The probe was cancelled by a pending shutdown, so its result says nothing about
                    // the device. The loop exits right after this.
                    return;
                }
                if (alive) {
                    m_liveness_probe_failures = 0;
                } else if (++m_liveness_probe_failures >= liveness_probe_failure_limit) {
                    utilities::print_error(m_config.cb_name, "RUNTIME", 1)
                        << "ChargeBridge stopped answering (" << m_liveness_probe_failures
                        << " failed liveness probes), tearing the internal runtime down" << std::endl;
                    m_liveness_probe_failures = 0;
                    m_next_liveness_probe.reset();
                    // Hands over to the regular teardown below, which stops the runtime and - for
                    // mDNS endpoints - re-arms discovery, so the device is re-probed and re-discovered
                    // through the normal connect path.
                    set_runtime_connection_status(current_status, false);
                }
            }
        }

        // Keep retrying bridges that could not be constructed. The connect path retries them on every
        // attempt, but it is unreachable once the ChargeBridge is connected, so a configured bridge
        // that failed to bring up its host-local device would otherwise stay missing (and, since
        // get_status() reports it as unavailable, keep this instance red) for the whole session. The
        // bridge pointers may only be read on the event loop thread, so whether there is anything to
        // create at all is decided inside the posted action.
        if (m_internal_runtime_started and m_was_connected and current_status.is_connected) {
            if (not m_next_bridge_retry.has_value()) {
                // The runtime just came up and has created what it could: start the schedule instead
                // of retrying the same construction again right away.
                m_next_bridge_retry = now + manager_base_cycle;
            } else if (now >= m_next_bridge_retry.value()) {
                m_next_bridge_retry = now + manager_base_cycle;
                retry_missing_bridges();
            }
        }

        if (m_was_connected and not current_status.is_connected) {
            stop_runtime = stop_internal_runtime();
            runtime_stop_in_progress = true;
        }
        if (not m_was_connected) {
            if (startup_runtime_in_progress) {
                if (startup_runtime.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    bool runtime_started = false;
                    try {
                        runtime_started = startup_runtime.get();
                        if (not runtime_started) {
                            // The reason is written by the start action before it fulfils the promise,
                            // so get() above publishes it to this thread.
                            report_runtime_start_failure(m_runtime_start_failure_reason.empty()
                                                             ? "no bridge could be connected and registered"
                                                             : m_runtime_start_failure_reason);
                        }
                    } catch (std::exception const& e) {
                        report_runtime_start_failure(e.what());
                    } catch (...) {
                        report_runtime_start_failure("unknown exception");
                    }
                    startup_runtime_in_progress = false;
                    if (runtime_started) {
                        m_internal_runtime_started = true;
                        m_was_connected = true;
                        m_runtime_start_failure_reported = false;
                        set_runtime_connection_status(current_status, true);
                    }
                }
                return;
            }

            // update_firmware() blocks (connection probe, and potentially a multi-minute firmware
            // upload) and does not touch the guarded status. Release the monitor lock across it so
            // get_status() on the shared event-loop thread — and therefore every other bridge
            // instance — is not stalled for the duration. current_status stays valid; only the lock
            // is dropped and re-acquired.
            //
            // The run flag doubles as the upload's cancellation check: on SIGINT/SIGTERM (or 'q' in
            // the terminal UI) the chunk loop stops within one chunk, the update is reported as
            // failed, and this loop exits because run is false. Without it the manager join in
            // ~charge_bridge would block until the flash completes.
            //
            // Exceptions are contained here instead of escaping the thread callable (which would call
            // std::terminate() and take every other bridge down with it): this bridge simply stays
            // disconnected and retries in the next manager cycle. The failure is only formatted while
            // the monitor is released and logged after the guard has re-acquired it - print_error() can
            // throw, and doing that in the unlocked window would leave the loop below reading guarded
            // state and waiting on a lock it does not own.
            bool firmware_ok = false;
            std::string firmware_error;
            {
                scoped_monitor_unlock unlocked(status_handle);
                try {
                    firmware_ok = update_firmware(m_force_firmware_update, [&run]() { return not run.load(); });
                } catch (std::exception const& e) {
                    firmware_error = std::string("Firmware update failed: ") + e.what() + " (retrying in next cycle)";
                } catch (...) {
                    firmware_error = "Firmware update failed with an unknown exception (retrying in next cycle)";
                }
            }
            if (not firmware_error.empty()) {
                utilities::print_error(m_config.cb_name, "FIRMWARE", 1) << firmware_error << std::endl;
            }
            if (firmware_ok) {
                // m_internal_runtime_started and m_was_connected are always set and cleared together,
                // so reaching this with a started runtime is impossible; start_internal_runtime() also
                // covers the reconnect case (it connects the endpoints before registering).
                startup_runtime = start_internal_runtime();
                startup_runtime_in_progress = true;
            } else if (is_mdns_endpoint() && not m_internal_runtime_started) {
                set_discovery_pending(current_status, true);
                next_connect_retry_time = clock::now() + manager_base_cycle;
            }
        }
    };
    m_manager = std::thread([&run, action, this]() {
        using clock = std::chrono::steady_clock;
        std::future<bool> startup_runtime;
        bool startup_runtime_in_progress = false;
        std::future<bool> stop_runtime;
        bool runtime_stop_in_progress = false;
        std::optional<clock::time_point> discovery_attempt_deadline;
        std::optional<clock::time_point> discovery_retry_time;
        std::optional<clock::time_point> next_connect_retry_time;

        auto is_startup_runtime_ready = [&startup_runtime, &startup_runtime_in_progress]() {
            return startup_runtime_in_progress &&
                   startup_runtime.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        };
        auto is_stop_runtime_ready = [&stop_runtime, &runtime_stop_in_progress]() {
            return runtime_stop_in_progress &&
                   stop_runtime.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        };

        auto handle = m_cb_status.handle();
        bool last_is_connected = handle->is_connected;
        bool last_discovery_pending = handle->discovery_pending;
        auto compute_wait_timeout = [&](std::chrono::milliseconds wait_timeout) {
            if (handle->discovery_pending && is_mdns_endpoint()) {
                if (next_connect_retry_time.has_value()) {
                    auto now = clock::now();
                    auto retry_remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(next_connect_retry_time.value() - now);
                    if (retry_remaining < wait_timeout) {
                        return retry_remaining;
                    }
                }
                if (m_discovery_active && discovery_attempt_deadline.has_value()) {
                    auto now = clock::now();
                    auto attempt_remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(discovery_attempt_deadline.value() - now);
                    if (attempt_remaining < wait_timeout) {
                        return attempt_remaining;
                    }
                } else if (discovery_retry_time.has_value()) {
                    auto now = clock::now();
                    auto retry_remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(discovery_retry_time.value() - now);
                    if (retry_remaining < wait_timeout) {
                        return retry_remaining;
                    }
                }
            }
            return wait_timeout;
        };

        auto condition = [&] {
            if (handle->is_connected not_eq last_is_connected) {
                return true;
            }
            if (handle->discovery_pending not_eq last_discovery_pending) {
                return true;
            }
            if (is_startup_runtime_ready()) {
                return true;
            }
            if (is_stop_runtime_ready()) {
                return true;
            }
            if (handle->discovery_pending && m_discovery_active && discovery_attempt_deadline.has_value()) {
                if (clock::now() >= discovery_attempt_deadline.value()) {
                    return true;
                }
            }
            if (handle->discovery_pending && (not m_discovery_active) && discovery_retry_time.has_value()) {
                if (clock::now() >= discovery_retry_time.value()) {
                    return true;
                }
            }
            if (handle->discovery_pending && next_connect_retry_time.has_value()) {
                if (clock::now() >= next_connect_retry_time.value()) {
                    return true;
                }
            }
            if (not run.load()) {
                return true;
            }
            return false;
        };
        while (run.load()) {
            // Safety net around the whole cycle: an exception leaving this thread callable would call
            // std::terminate() and kill the process, so a failing cycle only degrades this bridge and
            // is retried after the regular wait. Every path that temporarily releases the monitor lock
            // does so through scoped_monitor_unlock, which re-acquires it on every exit path including
            // exception unwinding, so `handle` is locked here and the wait below stays valid.
            try {
                action(handle, *handle, next_connect_retry_time, startup_runtime, startup_runtime_in_progress,
                       discovery_attempt_deadline, discovery_retry_time, stop_runtime, runtime_stop_in_progress);
            } catch (std::exception const& e) {
                utilities::print_error(m_config.cb_name, "MANAGER", 1)
                    << "Manager cycle failed: " << e.what() << " (retrying in next cycle)" << std::endl;
            } catch (...) {
                utilities::print_error(m_config.cb_name, "MANAGER", 1)
                    << "Manager cycle failed with an unknown exception (retrying in next cycle)" << std::endl;
            }
            if (handle->discovery_pending && is_mdns_endpoint()) {
                auto wait_timeout =
                    compute_wait_timeout(std::chrono::duration_cast<std::chrono::milliseconds>(manager_base_cycle));
                if (wait_timeout < 0ms) {
                    wait_timeout = 0ms;
                }
                handle.wait_for(condition, wait_timeout);
                last_is_connected = handle->is_connected;
                last_discovery_pending = handle->discovery_pending;
                continue;
            }

            auto wait_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(manager_base_cycle);
            handle.wait_for(condition, wait_timeout);
            last_is_connected = handle->is_connected;
            last_discovery_pending = handle->discovery_pending;
        }
    });
}

bool charge_bridge::update_firmware(bool force, std::function<bool()> abort_requested) {
    auto is_aborted = [&abort_requested]() { return abort_requested and abort_requested(); };

    firmware_update::sync_fw_updater updater(m_config.firmware, abort_requested);
    auto is_connected = updater.quick_check_connection();
    if (not is_connected) {
        return false;
    }
    updater.print_fw_version();

    auto do_update = force or (m_config.firmware.fw_update_on_start and not updater.check_if_correct_fw_installed());

    if (not do_update) {
        return true;
    }
    // Never start a multi-minute flash when a shutdown is already pending.
    if (is_aborted()) {
        utilities::print_error(m_config.cb_name, "FIRMWARE", 1)
            << "Firmware update skipped: shutdown requested" << std::endl;
        return false;
    }
    auto result = updater.upload_fw() && updater.check_connection();
    if (not result) {
        utilities::print_error(m_config.cb_name, "FIRMWARE", 1)
            << (is_aborted() ? "Firmware update aborted: shutdown requested"
                             : "Could not install correct firmware version")
            << std::endl;
    }
    return result;
}

std::string charge_bridge::get_pty_1_slave_path() {
    if (m_pty_1) {
        return m_pty_1->get_slave_path();
    }
    return "";
}

std::string charge_bridge::get_pty_2_slave_path() {
    if (m_pty_2) {
        return m_pty_2->get_slave_path();
    }
    return "";
}

std::string charge_bridge::get_pty_3_slave_path() {
    if (m_pty_3) {
        return m_pty_3->get_slave_path();
    }
    return "";
}

utilities::chargebridge_status charge_bridge::get_status() {
    utilities::chargebridge_status status;

    status.cb_name = m_config.cb_name;
    {
        auto handle = m_cb_status.handle();
        status.connected = handle->is_connected;
        if (m_endpoint_intent.value != endpoint_intent::fixed_ip) {
            status.discovered = not handle->discovery_pending;
        }
    }

    // Read-only network identity: the IP we talk to (configured or discovered) and the mDNS
    // hostname/service/TXT records when discovered.
    if (not m_config.cb_remote.empty() || m_discovery_info.has_value()) {
        utilities::chargebridge_network_info net;
        net.ip = m_config.cb_remote;
        if (m_discovery_info.has_value()) {
            net.mdns_hostname = m_discovery_info->hostname;
            net.mdns_service = m_discovery_info->service_instance;
            for (auto const& [key, value] : m_discovery_info->txt) {
                net.mdns_txt.emplace_back(key, value);
            }
        }
        status.network = std::move(net);
    }

    // An unset optional means "not configured" to every consumer (the dashboard glyph, the status log
    // line's aggregate and the MQTT chargebridge/status roll-up all skip fields without a value), so a
    // bridge that is configured but has no object - its construction failed, e.g. no CAP_NET_ADMIN for
    // the vcan device - must be reported as not available instead of being left unset. Otherwise a
    // partial bridge set looks completely healthy while a service EVerest depends on is missing.
    if (m_can_0_client) {
        auto available = m_can_0_client->available();
        status.can0.emplace(available);
    } else if (m_config.can0.has_value()) {
        status.can0.emplace(false);
    }
    if (m_pty_1) {
        auto available = m_pty_1->available();
        status.serial1.emplace(available);
    } else if (m_config.serial1.has_value()) {
        status.serial1.emplace(false);
    }
    if (m_pty_2) {
        auto available = m_pty_2->available();
        status.serial2.emplace(available);
    } else if (m_config.serial2.has_value()) {
        status.serial2.emplace(false);
    }
    if (m_pty_3) {
        auto available = m_pty_3->available();
        status.serial3.emplace(available);
    } else if (m_config.serial3.has_value()) {
        status.serial3.emplace(false);
    }
    if (m_bsp) {
        auto available = m_bsp->available();
        status.bsp.emplace(available);
        status.cp_state = m_bsp->cp_state();
    } else if (m_config.bsp.has_value()) {
        status.bsp.emplace(false);
    }
    if (m_plc) {
        auto available = m_plc->available();
        status.plc.emplace(available);
    } else if (m_config.plc.has_value()) {
        status.plc.emplace(false);
    }
    if (m_heartbeat) {
        auto available = m_heartbeat->available();
        status.heartbeat.emplace(available);
        status.mcu_resets.emplace(m_heartbeat->mcu_reset_count());
        status.telemetry = m_heartbeat->latest_telemetry();
    } else if (m_config.heartbeat.has_value()) {
        // The numeric fields stay unset: there is no MCU reset count or telemetry to report, and
        // "N/A" is the honest rendering for them. Only the availability flag carries health.
        status.heartbeat.emplace(false);
    }
    if (m_io) {
        auto available = m_io->available();
        status.io.emplace(available);
        if (auto io = m_io->latest_io()) {
            status.gpio = std::move(io->gpio);
            status.adc = std::move(io->adc);
            status.io_telemetry = std::move(io->telemetry);
        }
    } else if (m_config.io.has_value()) {
        status.io.emplace(false);
    }

    return status;
}

void charge_bridge::handle_ready() {
    auto status = get_status();
    publish_status(status);
    if (m_status_sink) {
        m_status_sink(status);
    }
}

void charge_bridge::handle_tick() {
    auto status = get_status();
    publish_status(status);
    // The tick sink is wired up only for the interactive terminal UI, so its live readouts and
    // telemetry charts refresh every tick. In log mode it stays unset to keep the log output unchanged.
    if (m_tick_sink) {
        m_tick_sink(status);
    }
}

void charge_bridge::publish_status(utilities::chargebridge_status const& status) {
    if (not m_config.telemetry.has_value()) {
        return;
    }

    bool result = true;
    auto publish = [this](std::string_view component, std::string_view item, bool status) {
        std::stringstream topic;
        topic << m_config.telemetry->telemetry_topic << "/" << m_config.cb_name << "/" << component << "/" << item;
        std::string_view payload = status ? "true" : "false";
        m_mqtt->publish(topic.str(), payload);
    };

    publish("chargebridge", "connected", status.connected);
    if (status.discovered.has_value()) {
        auto discovered = status.discovered.value();
        publish("chargebridge", "discovered", discovered);
        result = result && discovered;
    }

    auto publish_status = [publish](std::string_view component, bool status) { publish(component, "status", status); };

    if (status.can0.has_value()) {
        auto available = status.can0.value();
        result = result && available;
        publish_status("can_0", available);
    }
    if (status.serial1.has_value()) {
        auto available = status.serial1.value();
        result = result && available;
        publish_status("serial_1", available);
    }
    if (status.serial2.has_value()) {
        auto available = status.serial2.value();
        result = result && available;
        publish_status("serial_2", available);
    }
    if (status.serial3.has_value()) {
        auto available = status.serial3.value();
        result = result && available;
        publish_status("serial_3", available);
    }
    if (status.bsp.has_value()) {
        auto available = status.bsp.value();
        result = result && available;
        publish_status("bsp", available);
    }
    if (status.plc.has_value()) {
        auto available = status.plc.value();
        result = result && available;
        publish_status("plc", available);
    }
    if (status.heartbeat.has_value()) {
        auto available = status.heartbeat.value();
        result = result && available;
        publish_status("heartbeat", available);
    }
    if (status.io.has_value()) {
        auto available = status.io.value();
        result = result && available;
        publish_status("io", available);
    }
    publish_status("chargebridge", result);
}

bool charge_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = register_internal_events(handler) && result;
    result = register_manage_events(handler) && result;

    return result;
}
bool charge_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = unregister_internal_events(handler) && result;
    result = unregister_manage_events(handler) && result;

    return result;
}
bool charge_bridge::register_manage_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result =
        handler.register_event_handler(&m_1s_tick, everest::lib::util::bind_obj(&charge_bridge::handle_tick, this)) &&
        result;

    // The readiness notifier drives the status report (status UI line + the telemetry status publish),
    // so it must be registered regardless of telemetry. publish_status() itself no-ops when telemetry
    // is disabled, and the MQTT fd below is only registered when the client exists.
    result = handler.register_event_handler(&m_ready_notify,
                                            everest::lib::util::bind_obj(&charge_bridge::handle_ready, this)) &&
             result;

    if (m_mqtt) {
        result = handler.register_event_handler(m_mqtt.get()) && result;
    }

    return result;
}
bool charge_bridge::unregister_manage_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_1s_tick) && result;
    result = handler.unregister_event_handler(&m_ready_notify) && result;
    if (m_mqtt) {
        result = handler.unregister_event_handler(m_mqtt.get()) && result;
    }

    return result;
}

bool charge_bridge::register_internal_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    if (m_can_0_client) {
        result = handler.register_event_handler(m_can_0_client.get()) && result;
    }
    if (m_pty_1) {
        result = handler.register_event_handler(m_pty_1.get()) && result;
    }
    if (m_pty_2) {
        result = handler.register_event_handler(m_pty_2.get()) && result;
    }
    if (m_pty_3) {
        result = handler.register_event_handler(m_pty_3.get()) && result;
    }
    if (m_bsp) {
        result = handler.register_event_handler(m_bsp.get()) && result;
    }
    if (m_plc) {
        result = handler.register_event_handler(m_plc.get()) && result;
    }
    if (m_heartbeat) {
        result = handler.register_event_handler(m_heartbeat.get()) && result;
    }
    if (m_io) {
        result = handler.register_event_handler(m_io.get()) && result;
    }

    if (m_discovery) {
        result = handler.register_event_handler(m_discovery.get()) && result;
    }

    return result;
}
bool charge_bridge::unregister_internal_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;

    if (m_can_0_client) {
        result = handler.unregister_event_handler(m_can_0_client.get()) && result;
    }
    if (m_pty_1) {
        result = handler.unregister_event_handler(m_pty_1.get()) && result;
    }
    if (m_pty_2) {
        result = handler.unregister_event_handler(m_pty_2.get()) && result;
    }
    if (m_pty_3) {
        result = handler.unregister_event_handler(m_pty_3.get()) && result;
    }
    if (m_bsp) {
        result = handler.unregister_event_handler(m_bsp.get()) && result;
    }
    if (m_plc) {
        result = handler.unregister_event_handler(m_plc.get()) && result;
    }
    if (m_heartbeat) {
        result = handler.unregister_event_handler(m_heartbeat.get()) && result;
    }
    if (m_io) {
        result = handler.unregister_event_handler(m_io.get()) && result;
    }
    if (m_discovery) {
        result = handler.unregister_event_handler(m_discovery.get()) && result;
    }

    return result;
}

void charge_bridge::print_config() {
    print_charge_bridge_config(m_config);
}

void print_charge_bridge_config(charge_bridge_config const& c) {
    using namespace utilities;
    std::cout << "ChargeBridge: " << c.cb_name << std::endl;
    std::cout << " * remote:    " << c.cb_remote << std::endl;
    if (c.serial1) {
        std::cout << " * serial 1:  " << c.serial1->serial_device;
        if (c.heartbeat.has_value() && CB_NUMBER_OF_UARTS >= 1) {
            std::cout << " " << to_string(c.heartbeat->cb_config.uarts[0]);
        }
        std::cout << std::endl;
    }
    if (c.serial2) {
        std::cout << " * serial 2:  " << c.serial2->serial_device;
        if (c.heartbeat.has_value() && CB_NUMBER_OF_UARTS >= 2) {
            std::cout << " " << to_string(c.heartbeat->cb_config.uarts[1]);
        }
        std::cout << std::endl;
    }
    if (c.serial3) {
        std::cout << " * serial 3:  " << c.serial3->serial_device;
        if (c.heartbeat.has_value() && CB_NUMBER_OF_UARTS >= 3) {
            std::cout << " " << to_string(c.heartbeat->cb_config.uarts[2]);
        }
        std::cout << std::endl;
    }
    if (c.can0) {
        std::cout << " * can 0:     " << c.can0->can_device;
        if (c.heartbeat.has_value()) {
            std::cout << " " << to_string(c.heartbeat->cb_config.can.baudrate) << "bps" << std::endl;
        }
    }
    if (c.plc) {
        std::cout << " * plc:       " << c.plc->plc_tap << std::flush;
        std::cout << " " << format_host_port(c.cb_remote, c.plc->cb_port);
        std::cout << " adress " << c.plc->plc_ip;
        std::cout << " netmask " << c.plc->plc_netmaks;
        std::cout << " MTU " << c.plc->plc_mtu << std::endl;
    }
    if (c.bsp) {
        if (c.bsp->api.evse.enabled) {
            std::cout << " * evse_bsp:  ";
        } else if (c.bsp->api.ev.enabled) {
            std::cout << " * ev_bsp:    ";
        }
        std::cout << format_host_port(c.bsp->cb_remote, c.bsp->cb_port);
        std::cout << " module " << c.bsp->api.evse.module_id;
        std::cout << " MQTT " << c.bsp->api.mqtt_remote << ":" << c.bsp->api.mqtt_port;
        if (not c.bsp->api.mqtt_bind.empty()) {
            std::cout << " on " << c.bsp->api.mqtt_bind;
        }
        std::cout << " ping " << c.bsp->api.mqtt_ping_interval_ms << "ms";
        if (c.bsp->api.ovm.enabled) {
            std::cout << " OVM module " << c.bsp->api.ovm.module_id;
        }
        std::cout << std::endl;
    }
    if (c.heartbeat) {
        std::cout << " * heartbeat: " << format_host_port(c.cb_remote, c.cb_port);
        std::cout << " heartbeat interval " << c.heartbeat->interval_s << "s" << std::endl;
    }
    if (c.io) {
        std::cout << " * io:        " << format_host_port(c.cb_remote, c.cb_port);
        std::cout << " MQTT " << c.io->mqtt_remote << ":" << c.io->mqtt_port;
        if (not c.io->mqtt_bind.empty()) {
            std::cout << " on " << c.io->mqtt_bind;
        }
        std::cout << " send interval " << c.io->interval_s << "s" << std::endl;
    }

    std::cout << "\n" << std::endl;
}

} // namespace charge_bridge
