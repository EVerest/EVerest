// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

// Shared helpers for the reactor-driven EV session integration tests: drive an
// ev::Session by framing/injecting V2GTP bytes over a real fd_event_handler
// reactor. Separate from the FSM-unit fixture (fsm/helper.hpp).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/util/async/monitor.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>

namespace iso15118::ev::test {

using namespace std::chrono_literals;

// The single -20 DC entry an ev::Session advertises; Session no longer defaults
// this itself, so ctor sites pass it here.
inline std::vector<message_20::SupportedAppProtocol> default_advertised_app_protocols() {
    return {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};
}

// The single -20 AC entry an AC-configured ev::Session advertises.
inline std::vector<message_20::SupportedAppProtocol> default_advertised_ac_app_protocols() {
    return {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}};
}

// DER control functions the fixture supports by default, mirroring the module
// manifest defaults.
inline DerControlFunctions default_der_control_functions() {
    DerControlFunctions functions{};
    functions.dso_q_setpoint_provision = true;
    functions.dso_cos_phi_setpoint_provision = true;
    return functions;
}

// Frame a payload with the 8-byte V2GTP header, mirroring Session's own framing.
inline std::vector<uint8_t> frame_payload(io::v2gtp::PayloadType payload_type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame(io::SdpPacket::V2GTP_HEADER_SIZE + payload.size());
    V2GTP20_WriteHeader(frame.data(), static_cast<uint32_t>(payload.size()), static_cast<uint16_t>(payload_type));
    std::copy(payload.begin(), payload.end(), frame.begin() + io::SdpPacket::V2GTP_HEADER_SIZE);
    return frame;
}

template <typename Msg> std::vector<uint8_t> serialize_msg(const Msg& msg) {
    uint8_t buffer[1024];
    io::StreamOutputView out({buffer, sizeof(buffer)});
    const auto size = message_20::serialize(msg, out);
    return std::vector<uint8_t>(buffer, buffer + size);
}

inline io::v2gtp::PayloadType header_payload_type(const std::vector<uint8_t>& frame) {
    uint16_t tmp;
    std::memcpy(&tmp, frame.data() + 2, sizeof(tmp));
    return static_cast<io::v2gtp::PayloadType>(ntohs(tmp));
}

inline message_20::Variant decode_frame(const std::vector<uint8_t>& frame) {
    const auto payload_type = header_payload_type(frame);
    uint32_t len_be;
    std::memcpy(&len_be, frame.data() + 4, sizeof(len_be));
    const auto payload_len = ntohl(len_be);
    return message_20::Variant{payload_type,
                               io::StreamInputView{frame.data() + io::SdpPacket::V2GTP_HEADER_SIZE, payload_len}};
}

// Run the reactor (driving the Session's timers) until a predicate holds or a
// budget elapses. Returns the final predicate value.
template <typename Predicate>
bool run_reactor_until(everest::lib::io::event::fd_event_handler& reactor, Predicate predicate,
                       std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (not predicate() and std::chrono::steady_clock::now() < deadline) {
        reactor.poll(std::chrono::milliseconds{1});
        reactor.run_actions();
    }
    return predicate();
}

// Poll @p work on a cadence until done or @p budget elapses; for tests observing
// a Controller's own reactor on a worker thread, which the test thread must not touch.
template <typename Work> bool poll_until(Work work, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (work()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return work();
}

// Runs Controller::loop() on a worker thread and always stops and joins it.
// Catch2 abandons a section by throwing and re-entering enclosing blocks, so a
// bare thread joined inside a section would be left joinable on the abandoned
// path, terminating the process instead of reporting the failure.
class ControllerRun {
public:
    explicit ControllerRun(Controller& controller_) :
        controller(controller_), worker([&controller_]() { controller_.loop(); }) {
    }

    ~ControllerRun() {
        controller.shutdown();
        if (worker.joinable()) {
            worker.join();
        }
    }

    ControllerRun(const ControllerRun&) = delete;
    ControllerRun& operator=(const ControllerRun&) = delete;

private:
    Controller& controller;
    std::thread worker;
};

// Owns everything a reactor session test needs, replacing the ~20-line construction
// block otherwise copy-pasted per test. Wired callbacks/seam capture `this` and read
// the mutable config members live, so tests may flip `refuse_send` /
// `timed_out_throws` after construction and have it take effect at the next seam hit.
class SessionFixture {
public:
    explicit SessionFixture(
        message_20::datatypes::Identifier evcc_id = "EVTESTID01", SessionTiming timing = SessionTiming{5ms, 100ms},
        DcChargeParams params = default_params(),
        std::vector<message_20::SupportedAppProtocol> protocols = default_advertised_app_protocols(),
        message_20::datatypes::ServiceCategory energy_service = message_20::datatypes::ServiceCategory::DC,
        AcChargeParams ac_seed = AcChargeParams{},
        DerControlFunctions der_control_functions = default_der_control_functions(),
        bool der_stop_on_unsupported_functions = true) :
        dc_params(std::move(params)),
        ac_params(std::move(ac_seed)),
        session(make_callbacks(), make_send(), reactor, timing, std::move(evcc_id), std::move(protocols), &dc_params,
                &ac_params, energy_service, der_control_functions, der_stop_on_unsupported_functions) {
    }

    everest::lib::io::event::fd_event_handler reactor;
    std::vector<std::vector<uint8_t>> captured;

    // Feedback flags, set by the wired callbacks.
    bool timed_out = false;
    int timed_out_count = 0;
    bool ev_power_ready = false;
    bool dc_power_on = false;
    bool stop_from_charger = false;
    bool ac_limits = false;
    bool ac_bpt_limits = false;
    bool dc_bpt_limits = false;
    bool ac_target_power = false;
    bool der_control = false;

    // Outbound seam observation / control.
    int send_attempts = 0;
    bool refuse_send = false;

    // When set, the timed_out callback throws after bumping its count (pins one-shot delivery).
    bool timed_out_throws = false;

private:
    static DcChargeParams default_params() {
        // Realistic precharge target so DC_PreCharge completes on an in-tolerance
        // voltage rather than a degenerate 0 V match.
        DcChargeParams p{};
        p.target_voltage = 400.0f;
        return p;
    }

    feedback::Callbacks make_callbacks() {
        feedback::Callbacks cb{};
        cb.timed_out = [this]() {
            ++timed_out_count;
            timed_out = true;
            if (timed_out_throws) {
                throw std::runtime_error("consumer timed_out callback failure");
            }
        };
        cb.ev_power_ready = [this]() { ev_power_ready = true; };
        cb.dc_power_on = [this]() { dc_power_on = true; };
        cb.stop_from_charger = [this]() { stop_from_charger = true; };
        cb.ac_limits = [this](const message_20::datatypes::AC_CPDResEnergyTransferMode&) { ac_limits = true; };
        cb.ac_bpt_limits = [this](const message_20::datatypes::BPT_AC_CPDResEnergyTransferMode&) {
            ac_bpt_limits = true;
        };
        cb.dc_bpt_limits = [this](const message_20::datatypes::BPT_DC_CPDResEnergyTransferMode&) {
            dc_bpt_limits = true;
        };
        cb.ac_target_power = [this](const message_20::datatypes::Dynamic_AC_CLResControlMode&) {
            ac_target_power = true;
        };
        cb.der_control = [this](const message_20::datatypes::DER_Dynamic_AC_CLResControlMode&) { der_control = true; };
        return cb;
    }

    Session::OutboundSend make_send() {
        return [this](std::vector<uint8_t> frame) {
            ++send_attempts;
            if (refuse_send) {
                return false;
            }
            captured.push_back(std::move(frame));
            return true;
        };
    }

    everest::lib::util::monitor<DcChargeParams> dc_params;
    everest::lib::util::monitor<AcChargeParams> ac_params;

public:
    Session session;
};

// One walk step: inject a response frame, run until the Session emits the next
// request, and assert it decodes as ExpectedReq. `step` labels failures for
// localization.
template <typename ExpectedReq, typename ResponseMsg>
ExpectedReq inject_then_expect(SessionFixture& fx, const char* step, const ResponseMsg& response,
                               io::v2gtp::PayloadType response_type) {
    INFO("walk step: " << step);
    const auto before = fx.captured.size();
    fx.session.on_bytes_received(frame_payload(response_type, serialize_msg(response)));
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() > before; }, 1s));
    auto variant = decode_frame(fx.captured.back());
    const auto* request = variant.get_if<ExpectedReq>();
    REQUIRE(request != nullptr);
    return *request;
}

// A response of type Res bound to the session and carrying \p code, with every message-specific
// field left default. Walk steps differ in those fields, not in this preamble.
template <typename Res>
Res ok_res(const message_20::datatypes::SessionId& sid,
           message_20::datatypes::ResponseCode code = message_20::datatypes::ResponseCode::OK) {
    Res res{};
    res.header.session_id = sid;
    res.response_code = code;
    return res;
}

} // namespace iso15118::ev::test
