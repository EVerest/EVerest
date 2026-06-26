// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

// Shared helpers for the reactor-pump EV session integration tests
// (session_pump, integration_walk). These drive an ev::Session by framing and
// injecting V2GTP bytes and pumping a real fd_event_handler reactor. This is the
// reactor-pump concern; the FSM-unit fixture (fsm/helper.hpp) is separate.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <arpa/inet.h>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

namespace iso15118::ev::test {

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

// Pump the reactor (driving the Session's timers) until a predicate holds or a
// budget elapses. Returns the final predicate value.
template <typename Predicate>
bool pump_until(everest::lib::io::event::fd_event_handler& reactor, Predicate predicate,
                std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (not predicate() and std::chrono::steady_clock::now() < deadline) {
        reactor.poll(std::chrono::milliseconds{1});
        reactor.run_actions();
    }
    return predicate();
}

} // namespace iso15118::ev::test
