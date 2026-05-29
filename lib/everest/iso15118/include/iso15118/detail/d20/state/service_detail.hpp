// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/service_detail.hpp>

namespace iso15118::d20::state {

msg::d20::ServiceDetailResponse handle_request(const msg::d20::ServiceDetailRequest& req, d20::Session& session,
                                                 const d20::SessionConfig& config,
                                                 const std::optional<dt::ServiceParameterList>& custom_vas_parameters);

} // namespace iso15118::d20::state
