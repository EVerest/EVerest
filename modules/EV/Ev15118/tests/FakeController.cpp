// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "fake_controller.hpp"

#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/transport/connection.hpp>
#include <iso15118/ev/transport/sdp_client.hpp>

namespace fake_controller {
namespace {

struct Script {
    std::mutex mutex;
    LoopBody loop_body;
    std::optional<iso15118::ev::config::PausedSession> paused;
    bool throw_on_construct{false};
    bool interface_present{true};
    std::function<void()> on_construction_failure_reported;
    std::vector<Construction> constructions;
    ChannelCalls channel_calls;
    std::size_t stop_requests{0};
    std::size_t pause_requests{0};
    std::size_t terminations{0};
    // Set by Controller::loop() for the duration of the loop body.
    const iso15118::ev::Feedback* feedback{nullptr};
};

Script& script() {
    static Script instance;
    return instance;
}

class ConstructionFailure : public std::runtime_error {
public:
    explicit ConstructionFailure(std::function<void()> on_reported) :
        std::runtime_error("fake controller construction failure"), on_reported(std::move(on_reported)) {
    }

    const char* what() const noexcept override {
        if (auto hook = std::exchange(on_reported, nullptr)) {
            hook();
        }
        return std::runtime_error::what();
    }

private:
    mutable std::function<void()> on_reported;
};

} // namespace

void reset() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    s.loop_body = nullptr;
    s.paused.reset();
    s.throw_on_construct = false;
    s.interface_present = true;
    s.on_construction_failure_reported = nullptr;
    s.constructions.clear();
    s.channel_calls = {};
    s.stop_requests = 0;
    s.pause_requests = 0;
    s.terminations = 0;
}

void set_loop_body(LoopBody body) {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    s.loop_body = std::move(body);
}

void set_paused_session(std::optional<iso15118::ev::config::PausedSession> paused) {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    s.paused = std::move(paused);
}

void set_throw_on_construct(bool fail, std::function<void()> on_reported) {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    s.throw_on_construct = fail;
    s.on_construction_failure_reported = std::move(on_reported);
}

void set_interface_present(bool present) {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    s.interface_present = present;
}

namespace {

const iso15118::ev::Feedback* running_feedback() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    return s.feedback;
}

} // namespace

void install_contract(const std::string& chain_pem, const std::string& key_pem, const std::string& emaid) {
    if (const auto* feedback = running_feedback()) {
        feedback->pnc_contract_installed(chain_pem, key_pem, emaid);
    }
}

void offer_ac_evse_max_current(float current) {
    if (const auto* feedback = running_feedback()) {
        feedback->ac_evse_max_current(current);
    }
}

void fail_welding_detection(float last_voltage_v, int cycles) {
    if (const auto* feedback = running_feedback()) {
        feedback->welding_detection_failed(last_voltage_v, cycles);
    }
}

void receive_der_control(const iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode& control) {
    if (const auto* feedback = running_feedback()) {
        feedback->der_control(control);
    }
}

std::vector<Construction> constructions() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    return s.constructions;
}

ChannelCalls channel_calls() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    return s.channel_calls;
}

std::size_t stop_requests() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    return s.stop_requests;
}

std::size_t pause_requests() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    return s.pause_requests;
}

std::size_t terminations() {
    auto& s = script();
    std::lock_guard lock(s.mutex);
    return s.terminations;
}

} // namespace fake_controller

namespace iso15118::ev {

bool resolve_interface(std::string&) {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    return s.interface_present;
}

Controller::Controller(config::EvConfig config_, feedback::Callbacks callbacks_,
                       params::DcChargeParams initial_dc_params, params::AcChargeParams initial_ac_params) :
    config(std::move(config_)),
    feedback(std::move(callbacks_)),
    dc_params(std::move(initial_dc_params)),
    ac_params(std::move(initial_ac_params)) {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    if (s.throw_on_construct) {
        throw fake_controller::ConstructionFailure(std::exchange(s.on_construction_failure_reported, nullptr));
    }
    s.constructions.push_back({config.resume, config.params.pnc, config.supported_protocols,
                               config.params.energy_request, config, *dc_params.handle(), *ac_params.handle()});
}

Controller::~Controller() = default;

void Controller::loop() {
    auto& s = fake_controller::script();
    fake_controller::LoopBody body;
    {
        std::lock_guard lock(s.mutex);
        body = s.loop_body;
        s.feedback = &feedback;
    }
    if (body) {
        body();
    }
    std::lock_guard lock(s.mutex);
    s.feedback = nullptr;
}

void Controller::request_stop() {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    ++s.stop_requests;
}

void Controller::request_pause() {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    ++s.pause_requests;
}

void Controller::terminate() {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    ++s.terminations;
}

void Controller::shutdown() {
}

namespace {

// Appends `value` to one of the recorded channels under the script mutex.
template <typename T> void record(std::vector<T> fake_controller::ChannelCalls::*channel, T value) {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    (s.channel_calls.*channel).push_back(std::move(value));
}

} // namespace

void Controller::set_cp_state(bool c_or_d) {
    record(&fake_controller::ChannelCalls::cp_states, c_or_d);
}

void Controller::update_present_soc(double present_soc) {
    record(&fake_controller::ChannelCalls::present_soc, present_soc);
}

void Controller::update_present_voltage(float present_voltage) {
    record(&fake_controller::ChannelCalls::present_voltage, present_voltage);
}

void Controller::update_present_active_power(float present_active_power) {
    record(&fake_controller::ChannelCalls::present_active_power, present_active_power);
}

void Controller::update_present_frequency(float present_frequency) {
    record(&fake_controller::ChannelCalls::present_frequency, present_frequency);
}

void Controller::update_der_alarm_status(std::uint32_t der_alarm_status) {
    record(&fake_controller::ChannelCalls::der_alarm_status, der_alarm_status);
}

void Controller::update_dc_params(const params::DcChargeParams& params) {
    record(&fake_controller::ChannelCalls::dc_params, params);
}

std::optional<config::PausedSession> Controller::paused_session() const {
    auto& s = fake_controller::script();
    std::lock_guard lock(s.mutex);
    return s.paused;
}

} // namespace iso15118::ev
