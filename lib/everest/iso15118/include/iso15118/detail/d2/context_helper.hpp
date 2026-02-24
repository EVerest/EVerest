// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/context.hpp>
#include <iso15118/d2/session.hpp>
#include <iso15118/message/d2/msg_data_types.hpp>

namespace iso15118::d2 {

// FIXME (aw): not sure about correct signature here for RVO
template <typename Response, typename ResponseCode> Response& response_with_code(Response& res, ResponseCode code) {
    // FIXME (aw): could add an static_assert here that ResponseCode is an enum?
    res.response_code = code;
    return res;
}

bool validate_and_setup_header(msg::Header&, const Session&, const decltype(msg::Header::session_id)&);

void setup_header(msg::Header&, const Session&);

void send_sequence_error(const msg::Type, Context&);

} // namespace iso15118::d2
