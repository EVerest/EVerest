// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/d20/context.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::d20 {

// FIXME (aw): not sure about correct signature here for RVO
template <typename Response, typename ResponseCode> Response& response_with_code(Response& res, ResponseCode code) {
    // FIXME (aw): could add an static_assert here that ResponseCode is an enum?
    res.response_code = code;
    return res;
}

// [V2G20-1534] SECC time at the microsecond resolution the message header is encoded in.
uint64_t now_in_secc_time();

std::optional<uint32_t> departure_time_offset(const std::optional<uint64_t>& departure_time, uint64_t header_timestamp);

bool validate_and_setup_header(message_20::Header&, const Session&, const decltype(message_20::Header::session_id)&);

void setup_header(message_20::Header&, const Session&);

void send_sequence_error(const message_20::Type, d20::Context&);

} // namespace iso15118::d20
