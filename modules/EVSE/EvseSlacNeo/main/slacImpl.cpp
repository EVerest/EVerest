// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2023 Pionix GmbH and Contributors to EVerest

#include "slacImpl.hpp"

#include <algorithm>
#include <chrono>

#include <map>

#include <everest/slac/slac_event.hpp>
#include <everest/utils/yaml_loader.hpp>
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

// How long init() drives the event loop after opening the PLC device, waiting for the first I/O
// ready (or error) event before giving up and continuing with a CommunicationFault (see
// slacImpl::init). Bring-up normally settles within milliseconds.
constexpr std::chrono::milliseconds IO_BRING_UP_TIMEOUT{10000};

// How long shutdown() waits for the event loop running in ready() to return. It only has to
// cover one poll wake-up, so anything measured in seconds is generous; the bound exists so that
// a wedged loop degrades into a loud log instead of hanging the whole EVerest shutdown.
constexpr std::chrono::milliseconds LOOP_EXIT_TIMEOUT{5000};

template <typename T> nlohmann::json to_telemetry_json(std::string const& value) {
    return api_telemetry::deserialize<T>(value);
}

// Converts the library's framework-agnostic D3State into the generated slac interface enum.
// The mapping is total: D3State and types::slac::State share the same three matching states.
types::slac::State to_interface_state(everest::lib::slac::D3State state) {
    switch (state) {
    case everest::lib::slac::D3State::Matching:
        return types::slac::State::MATCHING;
    case everest::lib::slac::D3State::Matched:
        return types::slac::State::MATCHED;
    case everest::lib::slac::D3State::Unmatched:
        return types::slac::State::UNMATCHED;
    }
    return types::slac::State::UNMATCHED;
}

// The CM_AMP_MAP amplitude map (ISO 15118-3 A.9.6). Loaded from an operator YAML
// file so integrators can tune the transmit-power reduction per carrier for their
// hardware without touching the EVerest config. Schema:
//   carriers: <int>              # number of OFDM carriers (default 1155)
//   default_amplitude: <0..15>   # amplitude for carriers without an override
//                                # (default 15 = maximum TX, i.e. no reduction)
//   overrides: { <carrier>: <0..15>, ... }   # optional per-carrier reductions
// Returns the number of 4-bit entries and their packed bytes (2 per byte, the
// even carrier in the low nibble). Any error falls back to an all-maximum-TX map
// so the transmit-power-limitation exchange stays valid.
struct AmpMap {
    std::uint16_t len{0};
    std::vector<std::uint8_t> data{};
};

AmpMap load_amp_map(const std::string& path) {
    constexpr int DEFAULT_CARRIERS = 1155;
    constexpr int MAX_AMPLITUDE = 0x0F;
    constexpr int MAX_CARRIERS = 4096;

    int carriers = DEFAULT_CARRIERS;
    int default_amplitude = MAX_AMPLITUDE;
    std::map<int, int> overrides;

    if (not path.empty()) {
        try {
            const auto root = Everest::load_yaml(path);
            if (root.contains("carriers")) {
                carriers = root.at("carriers").get<int>();
            }
            if (root.contains("default_amplitude")) {
                default_amplitude = root.at("default_amplitude").get<int>();
            }
            if (root.contains("overrides") and root.at("overrides").is_object()) {
                for (const auto& entry : root.at("overrides").items()) {
                    overrides[std::stoi(entry.key())] = entry.value().get<int>();
                }
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Failed to load amp_map_file '" << path << "': " << e.what()
                          << "; falling back to an all-maximum-TX amplitude map";
            carriers = DEFAULT_CARRIERS;
            default_amplitude = MAX_AMPLITUDE;
            overrides.clear();
        }
    }

    carriers = std::clamp(carriers, 1, MAX_CARRIERS);
    default_amplitude = std::clamp(default_amplitude, 0, MAX_AMPLITUDE);

    AmpMap map;
    map.len = static_cast<std::uint16_t>(carriers);
    map.data.assign(static_cast<std::size_t>((carriers + 1) / 2), 0);
    for (int i = 0; i < carriers; ++i) {
        int amplitude = default_amplitude;
        const auto it = overrides.find(i);
        if (it != overrides.end()) {
            amplitude = std::clamp(it->second, 0, MAX_AMPLITUDE);
        }
        const auto nibble = static_cast<std::uint8_t>(amplitude & MAX_AMPLITUDE);
        if ((i % 2) == 0) {
            map.data[i / 2] |= nibble;
        } else {
            map.data[i / 2] |= static_cast<std::uint8_t>(nibble << 4);
        }
    }
    return map;
}
} // namespace

slacImpl::~slacImpl() {
    shutdown();
}

void slacImpl::init() {
    // Do not report readiness (init() returning -> signal_ready -> global ready) before the PLC
    // I/O is usable or its bring-up has failed: EvseManager starts issuing enter_bcd/reset right
    // after global ready and this module drops commands while !slac_io_ready, so a car already
    // plugged in at boot would silently lose its first SLAC session. EvseSlac holds back
    // readiness the same way by running startup_delay_ms and the socket open inside init(); this
    // also preserves that meaning of startup_delay_ms.
    //
    // The I/O ready event is delivered through the event loop, and there is no worker thread of
    // our own any more: the loop runs on the framework's ready thread inside ready(). So init()
    // drives the same loop itself, on the init thread, just until bring-up settled (see
    // run_bring_up_loop). On failure or timeout init() still returns: the raised
    // generic/CommunicationFault makes EvseManager set the connector inoperative instead of the
    // whole EVerest stack stalling on this module.
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
    configure_slac_io_callbacks();
    if (!register_event_handlers()) {
        return;
    }
    run_bring_up_loop();
}

void slacImpl::ready() {
    // The event loop runs on this thread. The framework spawns a dedicated thread for the global
    // ready message and joins it last during teardown, so blocking here is what that thread is
    // for. shutdown() arrives on another framework thread and makes the loop return.
    {
        auto lifecycle = lifecycle_state.handle();
        if (not lifecycle->may_enter_loop()) {
            EVLOG_info << "EvseSlacNeo: not starting the event loop (shutdown already requested)";
            return;
        }
        lifecycle->ready_entered = true;
    }
    lifecycle_state.notify_all();

    // Global ready may be the second of the two events the FSM start waits for.
    start_fsm_if_ready();

    if (online.load()) {
        run_event_loop();
    }

    {
        auto lifecycle = lifecycle_state.handle();
        // The loop is gone: handlers must not reach the controller any more.
        lifecycle->worker = nullptr;
        lifecycle->slac_io_ready = false;
        lifecycle->loop_exited = true;
    }
    // Wakes shutdown(), which blocks until loop_exited is set.
    lifecycle_state.notify_all();
}

void slacImpl::shutdown() {
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        // Idempotent: the framework hook and the destructor may both get here. A repeat call has
        // work to do only if a previous one gave up waiting on a loop that is still running.
        if (lifecycle->shutting_down and lifecycle->loop_settled()) {
            return;
        }
        lifecycle->shutting_down = true;
        // From here on command handlers drop instead of touching the controller.
        local_fsm_ctrl = lifecycle->worker;
        lifecycle->worker = nullptr;
    }
    lifecycle_state.notify_all();

    if (local_fsm_ctrl) {
        // Cross-thread: freeze the FSM. The loop-only teardown() through a reset event is not
        // available from here; the objects are destroyed below anyway.
        local_fsm_ctrl->stop();
    }
    online.store(false);
    bring_up_pending.store(false);
    exit_event.notify();

    // Wait for the loop before returning: the framework joins the thread running ready() and
    // destroys the module afterwards, and everything the loop touches lives in this object.
    auto const result = everest::lib::util::wait_for_loop_exit(lifecycle_state, LOOP_EXIT_TIMEOUT);
    if (result == everest::lib::util::LoopExitResult::TimedOut) {
        EVLOG_error << "EvseSlacNeo: the event loop did not stop within " << LOOP_EXIT_TIMEOUT.count()
                    << " ms; leaving the SLAC objects alive because the loop may still be using them";
        return;
    }

    // NotRunning also covers "init() is still in its bring-up loop on the init thread": that loop
    // observes the flags above and returns, but it may be mid-iteration right now, so wait for it
    // as well before destroying what it polls. ready() is not entered after a shutdown request.
    {
        auto lifecycle = lifecycle_state.handle();
        if (!lifecycle.wait_for([&] { return !lifecycle->bring_up_running; }, LOOP_EXIT_TIMEOUT)) {
            EVLOG_error << "EvseSlacNeo: the bring-up loop in init() did not stop within " << LOOP_EXIT_TIMEOUT.count()
                        << " ms; leaving the SLAC objects alive because the loop may still be using them";
            return;
        }
    }
    unregister_event_handlers();
    fsm_ctrl.reset();
    slac_io.reset();
    fsm_ctx.reset();
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
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': {}", config.device, e.what()));
        return false;
    } catch (...) {
        abort_event_loop(fmt::format("Failed to initialize SLAC I/O on device '{}': unknown error", config.device));
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

    callbacks.signal_state = [this](everest::lib::slac::D3State value) { publish_state(to_interface_state(value)); };

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
        fsm_ctx->slac_config.set_key_timeout = std::chrono::milliseconds{config.set_key_timeout_ms};
    } else {
        EVLOG_warning << "Invalid set_key_timeout_ms value '" << config.set_key_timeout_ms
                      << "'; clamping set_key_timeout_ms to 1 ms";
        fsm_ctx->slac_config.set_key_timeout = std::chrono::milliseconds{1};
    }
    fsm_ctx->slac_config.set_key_max_attempts = std::max(1, config.set_key_max_attempts);
    if (config.set_key_handling_mode.empty() || config.set_key_handling_mode == "retry_confirmed") {
        fsm_ctx->slac_config.set_key_handling_mode = everest::lib::slac::fsm::evse::SetKeyHandlingMode::retry_confirmed;
    } else if (config.set_key_handling_mode == "legacy_single_attempt") {
        fsm_ctx->slac_config.set_key_handling_mode =
            everest::lib::slac::fsm::evse::SetKeyHandlingMode::legacy_single_attempt;
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
        fsm_ctx->slac_config.nmk_generation_mode = everest::lib::slac::fsm::evse::NmkGenerationMode::legacy_printable;
    } else if (config.nmk_generation_mode == "full_byte_range") {
        fsm_ctx->slac_config.nmk_generation_mode = everest::lib::slac::fsm::evse::NmkGenerationMode::full_byte_range;
    } else {
        EVLOG_warning << "Invalid nmk_generation_mode '" << config.nmk_generation_mode
                      << "'. Expected 'full_byte_range' or 'legacy_printable'. Falling back to " << "legacy_printable";
        fsm_ctx->slac_config.nmk_generation_mode = everest::lib::slac::fsm::evse::NmkGenerationMode::legacy_printable;
    }

    fsm_ctx->slac_config.slac_init_timeout = std::chrono::milliseconds{config.slac_init_timeout_ms};
    fsm_ctx->slac_config.max_matching_sessions = std::max(1, config.max_matching_sessions);
    if (config.max_matching_sessions > 16) {
        EVLOG_warning << "High max_matching_sessions value '" << config.max_matching_sessions
                      << "' configured; this can create excessive SLAC processing load";
    }
    fsm_ctx->slac_config.ac_mode_five_percent = config.ac_mode_five_percent;
    fsm_ctx->slac_config.sounding_atten_adjustment = config.sounding_attenuation_adjustment;

    fsm_ctx->slac_config.chip_reset.enabled = config.do_chip_reset;
    fsm_ctx->slac_config.chip_reset.delay = std::chrono::milliseconds{config.chip_reset_delay_ms};
    fsm_ctx->slac_config.chip_reset.timeout = std::chrono::milliseconds{config.chip_reset_timeout_ms};

    fsm_ctx->slac_config.link_status.do_detect = config.link_status_detection;
    fsm_ctx->slac_config.link_status.retry = std::chrono::milliseconds{config.link_status_retry_ms};
    fsm_ctx->slac_config.link_status.timeout = std::chrono::milliseconds{config.link_status_timeout_ms};
    fsm_ctx->slac_config.link_status.poll_in_matched_state =
        std::chrono::milliseconds{std::max(10, config.link_status_poll_in_matched_state_ms)};
    fsm_ctx->slac_config.link_status.debounce_count = std::max(1, config.link_status_debounce_count);
    fsm_ctx->slac_config.link_status.debug_simulate_failed_matching = config.debug_simulate_failed_matching;

    fsm_ctx->slac_config.reset_instead_of_fail = config.reset_instead_of_fail;

    // CM_AMP_MAP transmit-power limitation (ISO 15118-3 A.9.6). The SECC always
    // responds to an incoming CM_AMP_MAP.REQ; only the SECC-initiated direction is
    // gated by initiate_amp_map and needs the amplitude map loaded from the file.
    fsm_ctx->slac_config.initiate_amp_map = config.initiate_amp_map;
    if (config.initiate_amp_map) {
        const auto amp_map = load_amp_map(config.amp_map_file);
        fsm_ctx->slac_config.amp_map_len = amp_map.len;
        fsm_ctx->slac_config.amp_map_data = amp_map.data;
        EVLOG_info << "CM_AMP_MAP initiation enabled with " << fsm_ctx->slac_config.amp_map_len << " carriers"
                   << (config.amp_map_file.empty() ? " (built-in all-maximum-TX default)"
                                                   : " from '" + config.amp_map_file + "'");
    }

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
        lifecycle->worker = fsm_ctrl.get();
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

        post_command("SLAC message", [&msg](FSMController& target) { target.signal_new_slac_message(msg); });
    });
    slac_io->set_error_callback([this](auto on_error, auto const& detail) { handle_slac_io_error(on_error, detail); });
    slac_io->set_ready_callback([this]() { handle_slac_io_ready(); });
}

bool slacImpl::register_event_handlers() {
    auto registrations_ok = true;
    if (!event_handler.register_event_handler(slac_io.get())) {
        EVLOG_error << "Failed to register SLAC IO event handler.";
        registrations_ok = false;
    }
    if (!event_handler.register_event_handler(fsm_ctrl.get())) {
        EVLOG_error << "Failed to register FSM controller event handler.";
        registrations_ok = false;
    }
    // Registered so that notifying it wakes poll(); the flags are `online` and `bring_up_pending`,
    // the event is just the knock on the door. An eventfd counts, so a notify that lands before
    // this registration is not lost - it fires on the first poll.
    if (!event_handler.register_event_handler(&exit_event, [](auto&) {})) {
        EVLOG_error << "Failed to register exit event handler.";
        registrations_ok = false;
    }
    if (!registrations_ok) {
        abort_event_loop("Aborting SLAC startup due to event handler registration failure.");
        unregister_event_handlers();
        return false;
    }
    return true;
}

void slacImpl::unregister_event_handlers() {
    // Tolerates objects that were never registered; drop the registrations while the registered
    // objects are still alive.
    if (slac_io) {
        (void)event_handler.unregister_event_handler(slac_io.get());
    }
    if (fsm_ctrl) {
        (void)event_handler.unregister_event_handler(fsm_ctrl.get());
    }
    (void)event_handler.unregister_event_handler(&exit_event);
    (void)event_handler.unregister_event_handler(&bring_up_timer);
}

void slacImpl::run_bring_up_loop() {
    // Same handler, same registrations as ready() will use; only the exit condition differs. The
    // ready callback, the error callback, the timer below and shutdown() all clear
    // bring_up_pending, and the two callbacks also wake poll() by being fd events themselves.
    bring_up_timer.set_single_shot(true);
    if (!bring_up_timer.set_timeout(IO_BRING_UP_TIMEOUT) ||
        !event_handler.register_event_handler(&bring_up_timer, [this]() {
            EVLOG_warning << "SLAC I/O bring-up timer expired.";
            bring_up_pending.store(false);
        })) {
        abort_event_loop("Failed to arm the SLAC I/O bring-up timer.");
        return;
    }

    bring_up_pending.store(true);
    {
        auto lifecycle = lifecycle_state.handle();
        // Tells shutdown() to wait for this loop before destroying anything it polls.
        lifecycle->bring_up_running = true;
        // Settled already (e.g. shutdown() raced ahead)? Then there is nothing to wait for.
        if (lifecycle->io_bring_up_settled()) {
            bring_up_pending.store(false);
        }
    }
    lifecycle_state.notify_all();

    try {
        while (online.load() && bring_up_pending.load()) {
            event_handler.poll();
            event_handler.run_actions();
        }
    } catch (const std::exception& e) {
        abort_event_loop(fmt::format("SLAC event loop stopped unexpectedly during bring-up: {}", e.what()));
    } catch (...) {
        abort_event_loop("SLAC event loop stopped unexpectedly during bring-up: unknown error");
    }

    (void)bring_up_timer.disarm();
    (void)event_handler.unregister_event_handler(&bring_up_timer);

    IoBringUpResult result{IoBringUpResult::TimedOut};
    {
        auto lifecycle = lifecycle_state.handle();
        result = bring_up_result(*lifecycle);
    }
    if (result == IoBringUpResult::TimedOut) {
        raise_communication_fault(fmt::format("SLAC I/O on device {} reported neither ready nor an error within {} ms; "
                                              "continuing startup with SLAC unavailable",
                                              config.device, IO_BRING_UP_TIMEOUT.count()));
    }

    {
        auto lifecycle = lifecycle_state.handle();
        lifecycle->bring_up_running = false;
    }
    // Wakes a shutdown() that is waiting for the bring-up loop.
    lifecycle_state.notify_all();
}

void slacImpl::run_event_loop() {
    try {
        event_handler.run(online);
    } catch (const std::exception& e) {
        abort_event_loop(fmt::format("SLAC event loop stopped unexpectedly: {}", e.what()));
    } catch (...) {
        abort_event_loop("SLAC event loop stopped unexpectedly: unknown error");
    }
}

void slacImpl::handle_slac_io_ready() {
    {
        auto lifecycle = lifecycle_state.handle();
        if (lifecycle->shutting_down) {
            return;
        }
        lifecycle->slac_io_ready = true;
    }
    // Ends the bring-up loop in init() if that is where we are.
    bring_up_pending.store(false);

    if (slac_io && fsm_ctx) {
        std::copy_n(slac_io->get_mac_addr(), fsm_ctx->evse_mac.size(), fsm_ctx->evse_mac.begin());
    }

    clear_communication_fault();
    start_fsm_if_ready();
}

void slacImpl::start_fsm_if_ready() {
    // Called from both handle_slac_io_ready() and ready(): the FSM starts on whichever of
    // {PLC I/O ready, global ready} happens second (see LifecycleStateT::fsm_start_allowed).
    FSMController* local_fsm_ctrl{nullptr};
    {
        auto lifecycle = lifecycle_state.handle();
        if (!lifecycle->fsm_start_allowed()) {
            return;
        }
        local_fsm_ctrl = lifecycle->worker;
    }

    if (local_fsm_ctrl) {
        EVLOG_info << "SLAC I/O is ready. Starting the SLAC state machine.";
        local_fsm_ctrl->init();
    } else {
        EVLOG_warning << "SLAC state machine start requested without an active controller. Start dropped.";
    }
}

void slacImpl::handle_slac_io_error(bool on_error, const std::string& detail) {
    if (on_error) {
        // Loop thread: teardown() runs the FSM's reset path in place instead of freezing it. That
        // path calls back into send_raw_slac, which takes the lifecycle monitor, so the pointer is
        // copied out and the monitor released first. Safe without post_command's guarantee because
        // shutdown() waits for this loop to exit before it destroys the controller.
        FSMController* local_fsm_ctrl{nullptr};
        {
            auto lifecycle = lifecycle_state.handle();
            if (lifecycle->slac_io_ready) {
                local_fsm_ctrl = lifecycle->live_worker();
            }
        }
        if (local_fsm_ctrl) {
            local_fsm_ctrl->teardown();
        }
        auto const detail_message = detail.empty() ? "unknown error" : detail;
        auto const fault_message =
            fmt::format("SLAC PLC communication unavailable on device {}: {}", config.device, detail_message);
        EVLOG_error << "SLAC I/O is in error. Waiting for hardware recovery: " << detail_message;
        raise_communication_fault(fault_message);
    } else {
        EVLOG_info << "SLAC I/O error cleared.";
        clear_communication_fault();
    }
}

void slacImpl::post_command(char const* command, std::function<void(FSMController&)> const& post) {
    // INVARIANT: the lifecycle monitor is held across the post, not just across the lookup.
    //
    // shutdown() waits for the event LOOP to exit, not for in-flight command handlers, and it
    // destroys the controller afterwards. A framework thread that read the pointer, released the
    // monitor and was then preempted could therefore come back and call into a destroyed
    // controller. Holding the monitor for the whole call closes that window: shutdown() cannot
    // get past its own handle() to clear the pointer and reset the controller while we are in
    // here. FSMController::signal_*() only touch atomics and event_fds, so nothing here blocks;
    // never post anything that runs FSM code (teardown()) through here, because the FSM calls
    // back into send_raw_slac, which takes this (non-recursive) monitor.
    auto lifecycle = lifecycle_state.handle();
    auto* target = lifecycle->live_worker();
    if (target == nullptr || !lifecycle->slac_io_ready) {
        EVLOG_warning << "Ignoring " << command << " because SLAC controller or PLC I/O is not available.";
        return;
    }
    post(*target);
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

    // A failed bring-up is a settled bring-up: ends the loop in init() if that is where we are.
    bring_up_pending.store(false);
    lifecycle_state.notify_all();
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
    lifecycle_state.notify_all();
}

void slacImpl::abort_event_loop(const std::string& reason) {
    EVLOG_error << reason;
    // Makes the loop (bring-up or ready) return and keeps it from being entered again.
    online.store(false);
    bring_up_pending.store(false);
    FSMController* local_fsm_ctrl{nullptr};
    bool should_raise_fault{false};
    {
        auto lifecycle = lifecycle_state.handle();
        should_raise_fault = !lifecycle->shutting_down;
        local_fsm_ctrl = lifecycle->worker;
        lifecycle->slac_io_ready = false;
        lifecycle->worker = nullptr;
    }

    if (local_fsm_ctrl) {
        // Loop thread only (all callers run on the thread driving the loop); see
        // handle_slac_io_error for why the FSM must be torn down through a reset event instead of
        // frozen with stop().
        local_fsm_ctrl->teardown();
    }

    if (should_raise_fault) {
        raise_communication_fault(reason);
    }
}

// --- interface commands -----------------------------------------------------------------------
//
// These run on framework threads. They only signal the controller's event_fds, which wake the
// event loop; the state machine is never touched from here.

void slacImpl::handle_reset(bool& enable) {
    // FIXME (aw): the enable could be used for power saving etc, but it is not implemented yet
    // CC: as power saving is not implemented, we actually don't need to reset at beginning of session (enable=true): At
    // start of everest it is being reset once and then it is enough to reset at the end of each session. This saves
    // some hundreds of msecs at the beginning of the charging session as we do not need to set up keys. Then
    // EvseManager can switch on 5% PWM basically immediately as SLAC is already ready.
    if (!enable) {
        post_command("handle_reset", [](FSMController& target) { target.signal_reset(); });
    }
}

void slacImpl::handle_enter_bcd() {
    post_command("handle_enter_bcd", [](FSMController& target) { target.signal_enter_bcd(); });
}

void slacImpl::handle_leave_bcd() {
    post_command("handle_leave_bcd", [](FSMController& target) { target.signal_leave_bcd(); });
}

void slacImpl::handle_count_bc(int& count) {
    // EvseManager pushes the running count of Control-Pilot B/C transitions here on every edge. Forward
    // it into the FSM context so the CM_VALIDATE handler can detect the number of BCB toggles the EV
    // performed. Dropping a sample is harmless: the handler reads the latest value on the next request.
    auto lifecycle = lifecycle_state.handle();
    if (auto* target = lifecycle->live_worker(); target != nullptr && lifecycle->slac_io_ready) {
        target->signal_count_bc(count);
    }
}

void slacImpl::handle_dlink_terminate() {
    // With receiving a D-LINK_TERMINATE.request from HLE, the communication node
    // shall leave the logical network within TP_match_leave. All parameters related
    // to the current link shall be set to the default value and shall change to the status "Unmatched".
    EVLOG_info << "D-LINK_TERMINATE.request received, leaving network.";
    post_command("handle_dlink_terminate", [](FSMController& target) { target.signal_reset(); });
}

void slacImpl::handle_dlink_error() {
    // The D-LINK_ERROR.request requests lower layers to terminate the data link and restart the matching
    // process by a control pilot transition through state E (on EVSE side this should be state F though)
    // CP signal is handled by EvseManager, so we just need to reset the SLAC state machine here.
    // DLINK_ERROR will be send from HLC layers when they detect that the connection is dead.
    EVLOG_warning << "D-LINK_ERROR.request received";
    post_command("handle_dlink_error", [](FSMController& target) { target.signal_reset(); });
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
