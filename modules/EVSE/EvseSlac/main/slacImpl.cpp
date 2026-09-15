// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "slacImpl.hpp"

#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include <everest_api_types/telemetry/codec.hpp>
#include <everest_api_types/telemetry/json_codec.hpp>
#include <fmt/core.h>

#include "everest/logging.hpp"
#include "slac_io.hpp"

namespace module {
namespace main {

namespace {
namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;

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

// The framework rejects a value outside the manifest's enum before init() runs; reaching the throw
// means the manifest and this table disagree, which must stop the module rather than pick a default.
template <typename E>
E parse_enum(char const* option, std::string const& value, std::initializer_list<std::pair<char const*, E>> table) {
    for (auto const& [name, e] : table) {
        if (value == name) {
            return e;
        }
    }
    throw std::invalid_argument(fmt::format("EvseSlac: config option {} has unsupported value '{}'", option, value));
}
} // namespace

// --- FrameworkSink ------------------------------------------------------------------------------

void slacImpl::FrameworkSink::publish_state(everest::lib::slac::D3State state) {
    owner.publish_state(to_interface_state(state));
}

void slacImpl::FrameworkSink::publish_dlink_ready(bool ready) {
    owner.publish_dlink_ready(ready);
}

void slacImpl::FrameworkSink::publish_ev_mac_address(std::string const& mac) {
    owner.publish_ev_mac_address(mac);
}

void slacImpl::FrameworkSink::request_error_routine() {
    owner.publish_request_error_routine(nullptr);
}

void slacImpl::FrameworkSink::raise_fault(std::string const& type, std::string const& sub_type,
                                          std::string const& message) {
    if (owner.error_factory && owner.error_manager) {
        owner.raise_error(owner.error_factory->create_error(type, sub_type, message));
    }
}

void slacImpl::FrameworkSink::clear_fault(std::string const& type) {
    if (owner.error_manager) {
        owner.clear_error(type);
    }
}

void slacImpl::FrameworkSink::publish_telemetry(std::string const& block, std::string const& key,
                                                std::string const& value) {
    if (block == "generic" && key == "status") {
        telemetry_generic[block][key] = to_telemetry_json<api_telemetry::SlacStatus>(value);
    } else if (block == "FSM" && key == "state") {
        telemetry_generic[block][key] = to_telemetry_json<api_telemetry::SlacFsmState>(value);
    } else {
        telemetry_generic[block][key] = value;
    }
    owner.mod->telemetry.publish("Slac", block, telemetry_generic[block]);
}

void slacImpl::FrameworkSink::log(LogLevel level, std::string const& text) {
    switch (level) {
    case LogLevel::Debug:
        EVLOG_debug << text;
        break;
    case LogLevel::Info:
        EVLOG_info << text;
        break;
    case LogLevel::Warning:
        EVLOG_warning << text;
        break;
    case LogLevel::Error:
        EVLOG_error << text;
        break;
    }
}

// --- configuration ------------------------------------------------------------------------------

SlacRuntimeConfig slacImpl::make_runtime_config() const {
    using namespace std::chrono;
    using everest::lib::slac::fsm::evse::NmkGenerationMode;
    using everest::lib::slac::fsm::evse::SetKeyCnfSuccessMode;
    using everest::lib::slac::fsm::evse::SetKeyHandlingMode;

    SlacRuntimeConfig rc;
    rc.device = config.device;
    rc.startup_delay = milliseconds{config.startup_delay_ms};
    rc.publish_mac_on_first_parm_req = config.publish_mac_on_first_parm_req;
    rc.publish_mac_on_match_cnf = config.publish_mac_on_match_cnf;
    rc.telemetry_enabled = mod->info.telemetry_enabled;
    rc.initiate_amp_map = config.initiate_amp_map;
    rc.amp_map_file = config.amp_map_file;

    // Ranges and enum values are validated against the manifest by the framework before init();
    // nothing here needs a fallback.
    auto& s = rc.slac;
    s.set_key_timeout = milliseconds{config.set_key_timeout_ms};
    s.set_key_max_attempts = config.set_key_max_attempts;
    s.set_key_handling_mode =
        parse_enum<SetKeyHandlingMode>("set_key_handling_mode", config.set_key_handling_mode,
                                       {{"retry_confirmed", SetKeyHandlingMode::retry_confirmed},
                                        {"legacy_single_attempt", SetKeyHandlingMode::legacy_single_attempt}});
    s.set_key_cnf_success_mode =
        parse_enum<SetKeyCnfSuccessMode>("set_key_cnf_success_mode", config.set_key_cnf_success_mode,
                                         {{"modem_compat_0x01", SetKeyCnfSuccessMode::modem_compat_0x01},
                                          {"hpgp_standard_0x00", SetKeyCnfSuccessMode::hpgp_standard_0x00},
                                          {"accept_0x00_or_0x01", SetKeyCnfSuccessMode::accept_0x00_or_0x01}});
    s.nmk_generation_mode = parse_enum<NmkGenerationMode>("nmk_generation_mode", config.nmk_generation_mode,
                                                          {{"legacy_printable", NmkGenerationMode::legacy_printable},
                                                           {"full_byte_range", NmkGenerationMode::full_byte_range}});
    s.slac_init_timeout = milliseconds{config.slac_init_timeout_ms};
    s.max_matching_sessions = config.max_matching_sessions;
    s.ac_mode_five_percent = config.ac_mode_five_percent;
    s.sounding_atten_adjustment = config.sounding_attenuation_adjustment;
    s.chip_reset.enabled = config.do_chip_reset;
    s.chip_reset.delay = milliseconds{config.chip_reset_delay_ms};
    s.chip_reset.timeout = milliseconds{config.chip_reset_timeout_ms};
    s.link_status.do_detect = config.link_status_detection;
    s.link_status.retry = milliseconds{config.link_status_retry_ms};
    s.link_status.timeout = milliseconds{config.link_status_timeout_ms};
    s.link_status.poll_in_matched_state = milliseconds{config.link_status_poll_in_matched_state_ms};
    s.link_status.debounce_count = config.link_status_debounce_count;
    s.link_status.debug_simulate_failed_matching = config.debug_simulate_failed_matching;
    s.reset_instead_of_fail = config.reset_instead_of_fail;
    s.print_state_transitions = config.print_state_transitions;
    s.regenerate_key_on_reset = !config.hack_disable_regenerate_key_on_reset;
    return rc;
}

// --- lifecycle ----------------------------------------------------------------------------------

slacImpl::~slacImpl() {
    shutdown();
}

void slacImpl::init() {
    sink = std::make_unique<FrameworkSink>(*this);
    runtime = std::make_unique<SlacRuntime>(make_runtime_config(), make_plc_socket_io, *sink);
    runtime->init();
}

void slacImpl::ready() {
    if (runtime) {
        runtime->ready();
    }
}

void slacImpl::shutdown() {
    if (runtime) {
        runtime->shutdown();
    }
}

// --- interface commands -------------------------------------------------------------------------

void slacImpl::handle_reset(bool& enable) {
    if (runtime) {
        (void)runtime->reset(enable);
    }
}

void slacImpl::handle_enter_bcd() {
    if (runtime) {
        (void)runtime->enter_bcd();
    }
}

void slacImpl::handle_leave_bcd() {
    if (runtime) {
        (void)runtime->leave_bcd();
    }
}

void slacImpl::handle_count_bc(int& count) {
    if (runtime) {
        runtime->count_bc(count);
    }
}

void slacImpl::handle_dlink_terminate() {
    if (runtime) {
        (void)runtime->dlink_terminate();
    }
}

void slacImpl::handle_dlink_error() {
    if (runtime) {
        (void)runtime->dlink_error();
    }
}

void slacImpl::handle_dlink_pause() {
    if (runtime) {
        runtime->dlink_pause();
    }
}

} // namespace main
} // namespace module
