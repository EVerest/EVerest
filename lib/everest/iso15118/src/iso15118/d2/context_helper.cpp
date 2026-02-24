// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <ctime>

#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message/d2/session_setup.hpp>

namespace iso15118::d2 {

namespace {
template <typename Response> Response handle_sequence_error(const d2::Session& session) {
    Response res;
    setup_header(res.header, session);
    return response_with_code(res, msg::data_types::ResponseCode::FAILED_SequenceError);
}
} // namespace

bool validate_and_setup_header(msg::Header& header, const Session& cur_session,
                               const decltype(msg::Header::session_id)& req_session_id) {

    setup_header(header, cur_session);

    return (cur_session.get_id() == req_session_id);
}

void setup_header(msg::Header& header, const Session& cur_session) {
    header.session_id = cur_session.get_id();
    // TODO(SL): Adding missing notification
}

// Todo(sl): Not happy at all. Need refactoring. Only ctx.respond and Session is needed. Not the whole Context.
void send_sequence_error(const msg::Type req_type, d2::Context& ctx) {

    if (req_type == msg::Type::SessionSetupReq) {
        const auto res = handle_sequence_error<msg::SessionSetupResponse>(ctx.session);
        ctx.respond(res);
    } else {
        logf_warning("Unknown code type id: %d ", req_type);
    }
}

} // namespace iso15118::d2
