// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/authorization.hpp>

namespace iso15118::d20::state {

// Outcome of the PnC checks done before the response is built: the ResponseCode to report and whether
// the SECC is done with this request ([V2G20-1977..1979], [V2G20-2226..2233]).
struct PncOutcome {
    message_20::datatypes::ResponseCode code{message_20::datatypes::ResponseCode::OK};
    message_20::datatypes::Processing processing{message_20::datatypes::Processing::Ongoing};
};

message_20::AuthorizationResponse handle_request(const message_20::AuthorizationRequest& req,
                                                 const d20::Session& session,
                                                 const message_20::datatypes::AuthStatus& authorization_status,
                                                 bool timeout_reached,
                                                 const std::optional<PncOutcome>& pnc = std::nullopt);

// A backend rejection of a contract certificate mapped onto the -20 WARNING codes ([V2G20-2210..2217]).
message_20::datatypes::ResponseCode pnc_rejection_code(const AuthorizationResponse& response);

} // namespace iso15118::d20::state
