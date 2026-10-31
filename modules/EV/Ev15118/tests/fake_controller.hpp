// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>

// FakeController.cpp defines every iso15118::ev::Controller member the module calls, so the
// test binary links it instead of the library's controller and no session touches a network.
namespace fake_controller {

struct Construction {
    std::optional<iso15118::ev::config::PausedSession> resume;
    iso15118::ev::d2::PnCConfig pnc;
    std::vector<iso15118::ProtocolId> protocols;
    iso15118::ev::params::EnergyRequest energy_request;
    iso15118::ev::config::EvConfig config;
    iso15118::ev::params::DcChargeParams dc_params;
    iso15118::ev::params::AcChargeParams ac_params;
};

// What the module sent through the Controllers' parameter channels, in order, across sessions.
struct ChannelCalls {
    std::vector<bool> cp_states;
    std::vector<double> present_soc;
    std::vector<float> present_voltage;
    std::vector<float> present_active_power;
    std::vector<float> present_frequency;
    std::vector<std::uint32_t> der_alarm_status;
    std::vector<iso15118::ev::params::DcChargeParams> dc_params;
};

// Runs inside Controller::loop() on the module's session worker; loop() returns when it does.
using LoopBody = std::function<void()>;

// Everything below is guarded by one mutex; the accessors copy under it.
void reset();
void set_loop_body(LoopBody body);
void set_paused_session(std::optional<iso15118::ev::config::PausedSession> paused);
// While failing, on_reported runs once when the module reads the failure's message, after the
// construction has unwound.
void set_throw_on_construct(bool fail, std::function<void()> on_reported = nullptr);
// Whether resolve_interface finds the HLC interface; true after reset().
void set_interface_present(bool present);

// From a loop body: reports a completed CertificateInstallation through the running Controller's
// feedback, as the -2 engine does.
void install_contract(const std::string& chain_pem, const std::string& key_pem, const std::string& emaid);
// From a loop body: reports the EVSE's AC max current offer, as the -2 engine does.
void offer_ac_evse_max_current(float current);
// From a loop body: reports a failed welding detection, as the engines do.
void fail_welding_detection(float last_voltage_v, int cycles);
// From a loop body: reports an AC_DER_IEC Dynamic charge-loop control block, as the -20 engine does.
void receive_der_control(const iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode& control);

std::vector<Construction> constructions();
ChannelCalls channel_calls();
std::size_t stop_requests();
std::size_t pause_requests();
std::size_t terminations();

} // namespace fake_controller
