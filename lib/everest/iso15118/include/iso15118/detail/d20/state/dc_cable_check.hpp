// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/session.hpp>
#include <iso15118/message/d20/dc_cable_check.hpp>

namespace iso15118::d20::state {

msg::d20::DC_CableCheckResponse handle_request(const msg::d20::DC_CableCheckRequest& req,
                                                 const d20::Session& session, bool cable_check_done);

} // namespace iso15118::d20::state
