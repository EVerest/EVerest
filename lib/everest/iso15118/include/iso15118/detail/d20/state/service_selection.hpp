// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/service_selection.hpp>

namespace iso15118::d20::state {

msg::d20::ServiceSelectionResponse handle_request(const msg::d20::ServiceSelectionRequest& req,
                                                    d20::Session& session);

} // namespace iso15118::d20::state
