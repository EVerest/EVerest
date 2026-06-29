// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_slacImpl.hpp"

#include <memory>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/logging.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/slac_event.hpp>
#include <fmt/core.h>

#include "fsm_controller.hpp"

namespace module {
namespace main {

namespace {
constexpr char kModuleLogPrefix[] = "EvSlacNeo: ";
} // namespace

ev_slacImpl::ev_slacImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvSlacNeo>& mod, Conf& config) :
    ev_slacImplBase(ev, "main"), mod(mod), config(config) {
}

ev_slacImpl::~ev_slacImpl() {
    shutdown();
}

void ev_slacImpl::shutdown() {
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->shutting_down = true;
        local_fsm_ctrl = lifecycle->fsm_ctrl;
        lifecycle->fsm_ctrl = nullptr;
        lifecycle->slac_io_ready = false;
        lifecycle->slac_fsm_started = false;
    }
    if (local_fsm_ctrl) {
        local_fsm_ctrl->stop();
    }
    lifecycle_state.notify_all();
    online.store(false);
    exit_event.notify();
    if (worker.joinable()) {
        worker.join();
    }
    fsm_ctrl.reset();
    slac_io.reset();
    fsm_ctx.reset();
}

void ev_slacImpl::init() {
    worker = std::thread(&ev_slacImpl::run, this);
}

void ev_slacImpl::ready() {
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->ready_requested) {
            return;
        }
        lifecycle->ready_requested = true;
    }
    lifecycle_state.notify_all();
}

void ev_slacImpl::run() {
    struct FsmCtrlLifecycleClearer {
        ev_slacImpl& owner;
        FSMController* active_controller{nullptr};

        explicit FsmCtrlLifecycleClearer(ev_slacImpl& owner) : owner(owner) {
        }
        void arm(FSMController* controller) {
            active_controller = controller;
        }
        ~FsmCtrlLifecycleClearer() {
            if (active_controller == nullptr) {
                return;
            }
            auto lifecycle = owner.lifecycle_state.handle();
            if (lifecycle->fsm_ctrl == active_controller) {
                lifecycle->fsm_ctrl = nullptr;
            }
        }
    } fsm_ctrl_clearer(*this);

    if (!wait_for_ready_or_shutdown()) {
        return;
    }

    if (!initialize_slac_io()) {
        return;
    }

    configure_callbacks();
    configure_fsm_context();
    if (!create_fsm_controller()) {
        return;
    }
    fsm_ctrl_clearer.arm(fsm_ctrl.get());

    configure_slac_io_callbacks();
    run_blocking_event_loop();
}

bool ev_slacImpl::wait_for_ready_or_shutdown() {
    auto lifecycle = lifecycle_state.handle();
    lifecycle.wait([&] { return lifecycle->ready_requested || lifecycle->shutting_down; });
    return !lifecycle->shutting_down;
}

bool ev_slacImpl::initialize_slac_io() {
    try {
        slac_io = std::make_unique<everest::lib::slac::SlacEvent>(config.device);
    } catch (const std::exception& e) {
        mark_worker_offline(fmt::format("Failed to initialize SLAC I/O on device '{}': {}", config.device, e.what()));
        return false;
    } catch (...) {
        mark_worker_offline(fmt::format("Failed to initialize SLAC I/O on device '{}': unknown error", config.device));
        return false;
    }
    return true;
}

void ev_slacImpl::configure_callbacks() {
    callbacks.send_raw_slac = [this](slac::messages::HomeplugMessage& msg) -> bool {
        {
            auto lifecycle = lifecycle_state.handle();
            if (lifecycle->shutting_down) {
                EVLOG_warning << kModuleLogPrefix << "SLAC I/O is shutting down. Dropping outgoing message.";
                return false;
            }
            if (!lifecycle->slac_io_ready) {
                EVLOG_warning << kModuleLogPrefix << "SLAC I/O is not ready. Dropping outgoing message.";
                return false;
            }
        }
        if (not slac_io) {
            EVLOG_warning << kModuleLogPrefix << "SLAC I/O is unavailable. Dropping outgoing message.";
            return false;
        }
        if (not slac_io->send(msg)) {
            EVLOG_warning << kModuleLogPrefix << "SLAC I/O failed to send Homeplug frame.";
            return false;
        }
        return true;
    };

    callbacks.signal_dlink_ready = [this](bool value) { publish_dlink_ready(value); };

    callbacks.signal_state = [this](const std::string& value) {
        try {
            publish_state(types::slac::string_to_state(value));
        } catch (const std::exception& e) {
            EVLOG_error << kModuleLogPrefix
                        << fmt::format("Tried to publish unknown SLAC state '{}'. Error: {}", value, e.what());
        }
    };

    callbacks.log_debug = [](const std::string& text) { EVLOG_debug << kModuleLogPrefix << text; };
    callbacks.log_info = [](const std::string& text) { EVLOG_info << kModuleLogPrefix << text; };
    callbacks.log_warn = [](const std::string& text) { EVLOG_warning << kModuleLogPrefix << text; };
    callbacks.log_error = [](const std::string& text) { EVLOG_error << kModuleLogPrefix << text; };
}

void ev_slacImpl::configure_fsm_context() {
    fsm_ctx = std::make_unique<slac_fsm::ev::Context>(callbacks, slac_io->get_mac_addr());

    if (config.set_key_timeout_ms > 0) {
        fsm_ctx->slac_config.set_key_timeout_ms = config.set_key_timeout_ms;
    } else {
        EVLOG_warning << kModuleLogPrefix << "Invalid set_key_timeout_ms value '" << config.set_key_timeout_ms
                      << "'; clamping set_key_timeout_ms to 1 ms";
        fsm_ctx->slac_config.set_key_timeout_ms = 1;
    }
}

bool ev_slacImpl::create_fsm_controller() {
    fsm_ctrl = std::make_unique<FSMController>(*fsm_ctx);
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return false;
        }
        lifecycle->fsm_ctrl = fsm_ctrl.get();
    }
    return true;
}

void ev_slacImpl::configure_slac_io_callbacks() {
    slac_io->set_callback([this](slac::messages::HomeplugMessage const& msg) {
        if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
            local_fsm_ctrl->signal_new_slac_message(msg);
            return;
        }
        EVLOG_warning << kModuleLogPrefix
                      << "SLAC callback received while controller or PLC I/O is not available. Dropping message.";
    });
    slac_io->set_error_callback([this](auto on_error, auto const& detail) { handle_slac_io_error(on_error, detail); });
    slac_io->set_ready_callback([this]() { handle_slac_io_ready(); });
}

void ev_slacImpl::run_blocking_event_loop() {
    everest::lib::io::event::fd_event_handler event_handler;
    auto registrations_ok = true;
    if (!event_handler.register_event_handler(slac_io.get())) {
        EVLOG_error << kModuleLogPrefix << "Failed to register SLAC I/O event handler.";
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(fsm_ctrl.get())) {
        EVLOG_error << kModuleLogPrefix << "Failed to register SLAC FSM event handler.";
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(&exit_event, [](auto&) {})) {
        EVLOG_error << kModuleLogPrefix << "Failed to register exit event handler.";
        registrations_ok = false;
    }
    if (!registrations_ok) {
        mark_worker_offline("Aborting SLAC startup due to event handler registration failure.");
        return;
    }

    try {
        event_handler.run(online);
    } catch (const std::exception& e) {
        mark_worker_offline(fmt::format("SLAC worker stopped unexpectedly: {}", e.what()));
    } catch (...) {
        mark_worker_offline("SLAC worker stopped unexpectedly: unknown error");
    }
}

void ev_slacImpl::handle_slac_io_ready() {
    FSMController* local_fsm_ctrl{nullptr};
    bool should_start_fsm{false};
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return;
        }
        lifecycle->slac_io_ready = true;
        local_fsm_ctrl = lifecycle->fsm_ctrl;
        if (local_fsm_ctrl && !lifecycle->slac_fsm_started) {
            lifecycle->slac_fsm_started = true;
            should_start_fsm = true;
        }
    }

    clear_communication_fault();

    if (should_start_fsm) {
        EVLOG_info << kModuleLogPrefix << "SLAC I/O is ready. Starting the SLAC state machine.";
        local_fsm_ctrl->init();
    } else if (!local_fsm_ctrl) {
        EVLOG_warning << kModuleLogPrefix << "SLAC I/O ready callback received without an active controller.";
    }
}

void ev_slacImpl::handle_slac_io_error(bool on_error, const std::string& detail) {
    if (on_error) {
        if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
            local_fsm_ctrl->stop();
        }
        auto const detail_message = detail.empty() ? "unknown error" : detail;
        auto const fault_message =
            fmt::format("SLAC PLC communication unavailable on device {}: {}", config.device, detail_message);
        EVLOG_error << kModuleLogPrefix << "SLAC I/O is in error. Waiting for hardware recovery: " << detail_message;
        raise_communication_fault(fault_message);
    } else {
        EVLOG_info << kModuleLogPrefix << "SLAC I/O error cleared.";
        clear_communication_fault();
    }
}

FSMController* ev_slacImpl::get_available_fsm_controller() {
    auto lifecycle = lifecycle_state.handle();
    if (lifecycle->shutting_down || !lifecycle->slac_io_ready) {
        return nullptr;
    }
    return lifecycle->fsm_ctrl;
}

void ev_slacImpl::raise_communication_fault(const std::string& message) {
    bool should_raise{false};
    bool should_replace{false};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->slac_io_ready = false;
        lifecycle->slac_fsm_started = false;
        if (!lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = true;
            should_raise = true;
        } else if (lifecycle->communication_fault_message != message) {
            should_replace = true;
        }
        lifecycle->communication_fault_message = message;
    }

    if (should_replace && error_manager) {
        clear_error("generic/CommunicationFault");
    }

    if ((should_raise || should_replace) && error_factory && error_manager) {
        raise_error(error_factory->create_error("generic/CommunicationFault", "", message));
    }
}

void ev_slacImpl::clear_communication_fault() {
    bool should_clear{false};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->slac_io_ready = true;
        if (lifecycle->communication_fault_raised) {
            lifecycle->communication_fault_raised = false;
            lifecycle->communication_fault_message.clear();
            should_clear = true;
        }
    }

    if (should_clear && error_manager) {
        clear_error("generic/CommunicationFault");
    }
}

void ev_slacImpl::mark_worker_offline(const std::string& reason) {
    EVLOG_error << kModuleLogPrefix << reason;
    online.store(false);
    FSMController* local_fsm_ctrl{nullptr};
    bool should_raise_fault{false};
    {
        auto lifecycle = lifecycle_state.handle();
        should_raise_fault = !lifecycle->shutting_down;
        local_fsm_ctrl = lifecycle->fsm_ctrl;
        lifecycle->slac_io_ready = false;
        lifecycle->slac_fsm_started = false;
        lifecycle->fsm_ctrl = nullptr;
    }

    if (local_fsm_ctrl) {
        local_fsm_ctrl->stop();
    }

    if (should_raise_fault) {
        raise_communication_fault(reason);
    }
}

void ev_slacImpl::handle_reset() {
    if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
        local_fsm_ctrl->signal_reset();
    } else {
        EVLOG_warning << kModuleLogPrefix
                      << "Ignoring handle_reset because SLAC controller or PLC I/O is not available.";
    }
}

bool ev_slacImpl::handle_trigger_matching() {
    if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
        local_fsm_ctrl->signal_trigger_matching();
        return true;
    }
    EVLOG_warning << kModuleLogPrefix
                  << "Ignoring handle_trigger_matching because SLAC controller or PLC I/O is not available.";
    return true;
}

} // namespace main
} // namespace module
