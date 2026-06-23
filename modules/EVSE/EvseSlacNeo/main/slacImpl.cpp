// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2023 Pionix GmbH and Contributors to EVerest

#include "slacImpl.hpp"

#include <algorithm>
#include <chrono>

#include <everest/slac/slac_event.hpp>
#include <everest_api_types/telemetry/codec.hpp>
#include <everest_api_types/telemetry/json_codec.hpp>
#include <fmt/core.h>

#include "everest/io/event/fd_event_handler.hpp"
#include "everest/logging.hpp"
#include "fsm_controller.hpp"

namespace module {
namespace main {

namespace {
namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;

template <typename T> nlohmann::json to_telemetry_json(std::string const& value) {
    return api_telemetry::deserialize<T>(value);
}
} // namespace

slacImpl::slacImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvseSlacNeo>& mod, Conf& config) :
    slacImplBase(ev, "main"), mod(mod), config(config) {
}

slacImpl::~slacImpl() {
    shutdown();
}

void slacImpl::shutdown() {
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->shutting_down = true;
        local_fsm_ctrl = lifecycle->fsm_ctrl;
        lifecycle->fsm_ctrl = nullptr;
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

void slacImpl::init() {
    // setup evse fsm thread
    worker = std::thread(&slacImpl::run, this);
}

void slacImpl::ready() {
    // let the waiting run thread go
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->ready_requested) {
            return;
        }
        lifecycle->ready_requested = true;
    }
    lifecycle_state.notify_all();
}

void slacImpl::run() {
    struct FsmCtrlLifecycleClearer {
        slacImpl& owner;
        FSMController* active_controller{nullptr};

        explicit FsmCtrlLifecycleClearer(slacImpl& owner) : owner(owner) {
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

    if (!wait_for_startup_delay_or_shutdown()) {
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

bool slacImpl::wait_for_ready_or_shutdown() {
    auto lifecycle = lifecycle_state.handle();
    lifecycle.wait([&] { return lifecycle->ready_requested || lifecycle->shutting_down; });
    return !lifecycle->shutting_down;
}

bool slacImpl::wait_for_startup_delay_or_shutdown() {
    if (config.startup_delay_ms > 0) {
        EVLOG_info << "Delaying SLAC startup by " << config.startup_delay_ms << "ms";
        {
            auto lifecycle = lifecycle_state.handle();
            if (lifecycle.wait_for([&] { return lifecycle->shutting_down; },
                                   std::chrono::milliseconds(config.startup_delay_ms))) {
                return false;
            }
        }
        EVLOG_info << "Continuing with SLAC initialization";
    }

    auto lifecycle = lifecycle_state.handle();
    return !lifecycle->shutting_down;
}

bool slacImpl::initialize_slac_io() {
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

void slacImpl::configure_callbacks() {
    callbacks.send_raw_slac = [this](slac::messages::HomeplugMessage& msg) -> bool {
        {
            auto lifecycle = lifecycle_state.handle();
            if (lifecycle->shutting_down) {
                EVLOG_warning << "SLAC I/O is shutting down. Dropping outgoing message.";
                return false;
            }
            if (!lifecycle->slac_io_ready) {
                EVLOG_warning << "SLAC I/O is not ready. Dropping outgoing message.";
                return false;
            }
        }
        if (not slac_io) {
            EVLOG_warning << "SLAC I/O is unavailable. Dropping outgoing message.";
            return false;
        }
        if (not slac_io->send(msg)) {
            EVLOG_warning << "SLAC I/O failed to send Homeplug frame.";
            return false;
        }
        return true;
    };

    callbacks.signal_dlink_ready = [this](bool value) { publish_dlink_ready(value); };

    callbacks.signal_state = [this](const std::string& value) {
        try {
            publish_state(types::slac::string_to_state(value));
        } catch (const std::exception& e) {
            EVLOG_error << fmt::format("Tried to publish unknown SLAC state '{}'. Error: {}", value, e.what());
        }
    };

    callbacks.signal_error_routine_request = [this]() { publish_request_error_routine(nullptr); };

    callbacks.log_debug = [](const std::string& text) {
        EVLOG_debug << text << std::endl;
        ;
    };
    callbacks.log_info = [](const std::string& text) {
        EVLOG_info << text << std::endl;
        ;
    };
    callbacks.log_warn = [](const std::string& text) {
        EVLOG_warning << text << std::endl;
        ;
    };
    callbacks.log_error = [](const std::string& text) {
        EVLOG_error << text << std::endl;
        ;
    };

    callbacks.pub_telemetry = [this](const std::string& block, const std::string& key, const std::string& value) {
        if (mod->info.telemetry_enabled) {
            if (block == "generic" && key == "status") {
                telemetry_generic[block][key] = to_telemetry_json<api_telemetry::SlacStatus>(value);
            } else if (block == "FSM" && key == "state") {
                telemetry_generic[block][key] = to_telemetry_json<api_telemetry::SlacFsmState>(value);
            } else {
                telemetry_generic[block][key] = value;
            }
            mod->telemetry.publish("Slac", block, telemetry_generic[block]);
        }
    };

    if (config.publish_mac_on_first_parm_req) {
        callbacks.signal_ev_mac_address_parm_req = [this](const std::string& mac) { publish_ev_mac_address(mac); };
    }

    if (config.publish_mac_on_match_cnf) {
        callbacks.signal_ev_mac_address_match_cnf = [this](const std::string& mac) { publish_ev_mac_address(mac); };
    }
}

void slacImpl::configure_fsm_context() {
    fsm_ctx = std::make_unique<slac::fsm::evse::Context>(callbacks);
    if (config.set_key_timeout_ms > 0) {
        fsm_ctx->slac_config.set_key_timeout_ms = config.set_key_timeout_ms;
    } else {
        EVLOG_warning << "Invalid set_key_timeout_ms value '" << config.set_key_timeout_ms
                      << "'; clamping set_key_timeout_ms to 1 ms";
        fsm_ctx->slac_config.set_key_timeout_ms = 1;
    }
    fsm_ctx->slac_config.set_key_max_attempts = std::max(1, config.set_key_max_attempts);
    if (config.set_key_handling_mode.empty() || config.set_key_handling_mode == "retry_confirmed") {
        fsm_ctx->slac_config.set_key_handling_mode = everest::lib::slac::fsm::evse::SetKeyHandlingMode::retry_confirmed;
    } else if (config.set_key_handling_mode == "legacy_single_attempt") {
        fsm_ctx->slac_config.set_key_handling_mode = everest::lib::slac::fsm::evse::SetKeyHandlingMode::legacy_single_attempt;
    } else {
        EVLOG_warning << "Invalid set_key_handling_mode '" << config.set_key_handling_mode
                      << "'. Expected 'legacy_single_attempt' or 'retry_confirmed'. Falling back to "
                      << "retry_confirmed";
        fsm_ctx->slac_config.set_key_handling_mode = everest::lib::slac::fsm::evse::SetKeyHandlingMode::retry_confirmed;
    }

    if (config.set_key_cnf_success_mode.empty() || config.set_key_cnf_success_mode == "modem_compat_0x01") {
        fsm_ctx->slac_config.set_key_cnf_success_mode =
            everest::lib::slac::fsm::evse::SetKeyCnfSuccessMode::modem_compat_0x01;
    } else if (config.set_key_cnf_success_mode == "hpgp_standard_0x00") {
        fsm_ctx->slac_config.set_key_cnf_success_mode =
            everest::lib::slac::fsm::evse::SetKeyCnfSuccessMode::hpgp_standard_0x00;
    } else if (config.set_key_cnf_success_mode == "accept_0x00_or_0x01") {
        fsm_ctx->slac_config.set_key_cnf_success_mode =
            everest::lib::slac::fsm::evse::SetKeyCnfSuccessMode::accept_0x00_or_0x01;
    } else {
        EVLOG_warning << "Invalid set_key_cnf_success_mode '" << config.set_key_cnf_success_mode
                      << "'. Expected 'modem_compat_0x01', 'hpgp_standard_0x00', or 'accept_0x00_or_0x01'. "
                      << "Falling back to modem_compat_0x01";
        fsm_ctx->slac_config.set_key_cnf_success_mode =
            everest::lib::slac::fsm::evse::SetKeyCnfSuccessMode::modem_compat_0x01;
    }

    if (config.nmk_generation_mode.empty() || config.nmk_generation_mode == "legacy_printable") {
        fsm_ctx->slac_config.nmk_generation_mode =
            everest::lib::slac::fsm::evse::NmkGenerationMode::legacy_printable;
    } else if (config.nmk_generation_mode == "full_byte_range") {
        fsm_ctx->slac_config.nmk_generation_mode = everest::lib::slac::fsm::evse::NmkGenerationMode::full_byte_range;
    } else {
        EVLOG_warning << "Invalid nmk_generation_mode '" << config.nmk_generation_mode
                      << "'. Expected 'full_byte_range' or 'legacy_printable'. Falling back to "
                      << "legacy_printable";
        fsm_ctx->slac_config.nmk_generation_mode =
            everest::lib::slac::fsm::evse::NmkGenerationMode::legacy_printable;
    }

    fsm_ctx->slac_config.slac_init_timeout_ms = config.slac_init_timeout_ms;
    fsm_ctx->slac_config.max_matching_sessions = std::max(1, config.max_matching_sessions);
    if (config.max_matching_sessions > 16) {
        EVLOG_warning << "High max_matching_sessions value '" << config.max_matching_sessions
                      << "' configured; this can create excessive SLAC processing load";
    }
    fsm_ctx->slac_config.ac_mode_five_percent = config.ac_mode_five_percent;
    fsm_ctx->slac_config.sounding_atten_adjustment = config.sounding_attenuation_adjustment;

    fsm_ctx->slac_config.chip_reset.enabled = config.do_chip_reset;
    fsm_ctx->slac_config.chip_reset.delay_ms = config.chip_reset_delay_ms;
    fsm_ctx->slac_config.chip_reset.timeout_ms = config.chip_reset_timeout_ms;

    fsm_ctx->slac_config.link_status.do_detect = config.link_status_detection;
    fsm_ctx->slac_config.link_status.retry_ms = config.link_status_retry_ms;
    fsm_ctx->slac_config.link_status.timeout_ms = config.link_status_timeout_ms;
    fsm_ctx->slac_config.link_status.debug_simulate_failed_matching = config.debug_simulate_failed_matching;

    fsm_ctx->slac_config.reset_instead_of_fail = config.reset_instead_of_fail;

    fsm_ctx->slac_config.print_state_transitions = config.print_state_transitions;
    fsm_ctx->slac_config.provide_telemetry = mod->info.telemetry_enabled;

    fsm_ctx->slac_config.regenerate_key_on_reset = !config.hack_disable_regenerate_key_on_reset;

    fsm_ctx->slac_config.generate_nmk();

    std::copy_n(slac_io->get_mac_addr(), fsm_ctx->evse_mac.size(), fsm_ctx->evse_mac.begin());
}

bool slacImpl::create_fsm_controller() {
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

void slacImpl::configure_slac_io_callbacks() {
    // Qualcomm PLC chip emits VS_ATTENUATION_CHARACTERISTICS (vendor MMTYPE 0xA14E) as
    // unsolicited broadcasts during sounding from a sibling MAC. FSM does not handle this
    // MMTYPE and logs "Received non-expected SLAC message of type 0xA14E" per frame, which
    // adds RX/log load. Drop it pre-FSM. Other MMTYPEs (incl. CM_SET_KEY.CNF, CM_ATTEN_PROFILE.IND)
    // pass through unchanged.
    slac_io->set_callback([this](slac::messages::HomeplugMessage const& msg) {
        if (msg.get_mmtype() == everest::lib::slac::defs::qualcomm::MMTYPE_QCA_VS_ATTENUATION_CHARACTERISTICS) {
            return;
        }

        auto* local_fsm_ctrl = get_available_fsm_controller();
        if (not local_fsm_ctrl) {
            EVLOG_warning << "SLAC callback received while controller or PLC I/O is not available. Dropping message.";
            return;
        }
        local_fsm_ctrl->signal_new_slac_message(msg);
    });
    slac_io->set_error_callback([this](auto on_error, auto const& detail) {
        handle_slac_io_error(on_error, detail);
    });
    slac_io->set_ready_callback([this]() { handle_slac_io_ready(); });
}

void slacImpl::run_blocking_event_loop() {
    everest::lib::io::event::fd_event_handler event_handler;
    auto registrations_ok = true;
    if (!event_handler.register_event_handler(slac_io.get())) {
        EVLOG_error << "Failed to register SLAC IO event handler.";
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(fsm_ctrl.get())) {
        EVLOG_error << "Failed to register FSM controller event handler.";
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(&exit_event, [](auto&) {})) {
        EVLOG_error << "Failed to register exit event handler.";
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

void slacImpl::handle_slac_io_ready() {
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return;
        }
        lifecycle->slac_io_ready = true;
        local_fsm_ctrl = lifecycle->fsm_ctrl;
    }

    clear_communication_fault();

    if (local_fsm_ctrl) {
        EVLOG_info << "SLAC I/O is ready. Starting the SLAC state machine.";
        local_fsm_ctrl->init();
    } else {
        EVLOG_warning << "SLAC I/O ready callback received without an active controller. Start dropped.";
    }
}

void slacImpl::handle_slac_io_error(bool on_error, const std::string& detail) {
    if (on_error) {
        if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
            local_fsm_ctrl->stop();
        }
        auto const detail_message = detail.empty() ? "unknown error" : detail;
        auto const fault_message = fmt::format("SLAC PLC communication unavailable on device {}: {}", config.device, detail_message);
        EVLOG_error << "SLAC I/O is in error. Waiting for hardware recovery: " << detail_message;
        raise_communication_fault(fault_message);
    } else {
        EVLOG_info << "SLAC I/O error cleared.";
        clear_communication_fault();
    }
}

FSMController* slacImpl::get_available_fsm_controller() {
    auto lifecycle = lifecycle_state.handle();
    if (lifecycle->shutting_down || !lifecycle->slac_io_ready) {
        return nullptr;
    }
    return lifecycle->fsm_ctrl;
}

void slacImpl::raise_communication_fault(const std::string& message) {
    bool should_raise{false};
    bool should_replace{false};
    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->slac_io_ready = false;
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

void slacImpl::clear_communication_fault() {
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

void slacImpl::mark_worker_offline(const std::string& reason) {
    EVLOG_error << reason;
    online.store(false);
    FSMController* local_fsm_ctrl{nullptr};
    bool should_raise_fault{false};
    {
        auto lifecycle = lifecycle_state.handle();
        should_raise_fault = !lifecycle->shutting_down;
        local_fsm_ctrl = lifecycle->fsm_ctrl;
        lifecycle->slac_io_ready = false;
        lifecycle->fsm_ctrl = nullptr;
    }

    if (local_fsm_ctrl) {
        local_fsm_ctrl->stop();
    }

    if (should_raise_fault) {
        raise_communication_fault(reason);
    }
}

void slacImpl::handle_reset(bool& enable) {
    // FIXME (aw): the enable could be used for power saving etc, but it is not implemented yet
    // CC: as power saving is not implemented, we actually don't need to reset at beginning of session (enable=true): At
    // start of everest it is being reset once and then it is enough to reset at the end of each session. This saves
    // some hundreds of msecs at the beginning of the charging session as we do not need to set up keys. Then
    // EvseManager can switch on 5% PWM basically immediately as SLAC is already ready.
    if (!enable) {
        if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
            local_fsm_ctrl->signal_reset();
        } else {
            EVLOG_warning << "Ignoring handle_reset because SLAC controller or PLC I/O is not available.";
        }
    }
}

void slacImpl::handle_enter_bcd() {
    if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
        local_fsm_ctrl->signal_enter_bcd();
    } else {
        EVLOG_warning << "Ignoring handle_enter_bcd because SLAC controller or PLC I/O is not available.";
    }
}

void slacImpl::handle_leave_bcd() {
    if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
        local_fsm_ctrl->signal_leave_bcd();
    } else {
        EVLOG_warning << "Ignoring handle_leave_bcd because SLAC controller or PLC I/O is not available.";
    }
}

void slacImpl::handle_dlink_terminate() {
    // With receiving a D-LINK_TERMINATE.request from HLE, the communication node
    // shall leave the logical network within TP_match_leave. All parameters related
    // to the current link shall be set to the default value and shall change to the status "Unmatched".
    EVLOG_info << "D-LINK_TERMINATE.request received, leaving network.";
    if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
        local_fsm_ctrl->signal_reset();
    } else {
        EVLOG_warning << "Ignoring handle_dlink_terminate because SLAC controller or PLC I/O is not available.";
    }
}

void slacImpl::handle_dlink_error() {
    // The D-LINK_ERROR.request requests lower layers to terminate the data link and restart the matching
    // process by a control pilot transition through state E (on EVSE side this should be state F though)
    // CP signal is handled by EvseManager, so we just need to reset the SLAC state machine here.
    // DLINK_ERROR will be send from HLC layers when they detect that the connection is dead.
    EVLOG_warning << "D-LINK_ERROR.request received";
    if (auto* local_fsm_ctrl = get_available_fsm_controller()) {
        local_fsm_ctrl->signal_reset();
    } else {
        EVLOG_warning << "Ignoring handle_dlink_error because SLAC controller or PLC I/O is not available.";
    }
}

void slacImpl::handle_dlink_pause() {
    // The D-LINK_PAUSE.request requests lower layers to enter a power saving mode. While being in this
    // mode, the state will be kept to "Matched".
    // So we don't need to do anything here as we do not support low power mode to power down the PLC modem.
    // This is optional in ISO15118-3.
    EVLOG_info << "D-LINK_PAUSE.request received. Staying in MATCHED, PLC chip stays powered on (low power mode "
                  "optional in -3)";
};

} // namespace main
} // namespace module
