// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

// Shared helpers for the reactor-driven EV session integration tests
// (session_reactor, integration_walk). These drive an ev::Session by framing and
// injecting V2GTP bytes and running a real fd_event_handler reactor. This is the
// reactor-integration concern; the FSM-unit fixture (fsm/helper.hpp) is separate.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
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
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

namespace iso15118::ev::test {

using namespace std::chrono_literals;

// The single -20 DC entry an ev::Session advertises by default (mirrors the
// EvConfig default); Session no longer defaults this, so ctor sites pass it here.
inline std::vector<message_20::SupportedAppProtocol> default_advertised_app_protocols() {
    return {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};
}

// The single -20 AC entry an AC-configured ev::Session advertises.
inline std::vector<message_20::SupportedAppProtocol> default_advertised_ac_app_protocols() {
    return {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}};
}

// Frame a payload with the 8-byte V2GTP header, mirroring the framing the
// Session itself uses (V2GTP20_WriteHeader + appended payload).
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

// Owns everything a reactor session test needs: the reactor, the captured outbound
// frames, the feedback flags, the logger, the DC-params channel and the ev::Session.
// Replaces the ~20-line construction block the session tests otherwise copy-paste. The
// wired callbacks and outbound seam capture `this` and read the mutable config members
// live, so tests set `refuse_send` / `timed_out_throws` after construction and the
// behavior takes effect when the Session next reaches the seam.
class SessionFixture {
public:
    explicit SessionFixture(
        message_20::datatypes::Identifier evcc_id = "EVTESTID01", SessionTiming timing = SessionTiming{5ms, 100ms},
        DcChargeParams params = default_params(),
        std::vector<message_20::SupportedAppProtocol> protocols = default_advertised_app_protocols(),
        message_20::datatypes::ServiceCategory energy_service = message_20::datatypes::ServiceCategory::DC,
        AcChargeParams ac_seed = AcChargeParams{}) :
        dc_params(std::move(params)),
        ac_params(std::move(ac_seed)),
        session(make_callbacks(), make_send(), logger, reactor, timing, std::move(evcc_id), std::move(protocols),
                &dc_params, &ac_params, energy_service) {
        // A no-op session log sink so a state's enter() logging never throws bad_function_call.
        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
    }

    ~SessionFixture() {
        session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
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
    bool ac_target_power = false;

    // Outbound seam observation / control.
    int send_attempts = 0;
    bool refuse_send = false;

    // When set, the timed_out callback throws after bumping its count (pins one-shot delivery).
    bool timed_out_throws = false;

private:
    static DcChargeParams default_params() {
        // A realistic precharge target so DC_PreCharge completes on an in-tolerance
        // present voltage rather than a degenerate 0 V match.
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
        cb.ac_target_power = [this](const message_20::datatypes::Dynamic_AC_CLResControlMode&) {
            ac_target_power = true;
        };
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

    session::SessionLogger logger{nullptr};
    everest::lib::util::monitor<DcChargeParams> dc_params;
    everest::lib::util::monitor<AcChargeParams> ac_params;

public:
    Session session;
};

// One walk step: inject a serialized response frame, run the reactor until the Session emits one more
// request, and assert that request decodes as ExpectedReq. Returns the decoded request
// for field-level assertions. `step` labels the step so a failure inside the shared
// REQUIREs still localizes.
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

} // namespace iso15118::ev::test
