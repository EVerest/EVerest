// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/session.hpp>
#include <iso15118/message/d20/power_delivery.hpp>

namespace iso15118::d20::state {

msg::d20::PowerDeliveryResponse handle_request(const msg::d20::PowerDeliveryRequest& req,
                                                 const d20::Session& session, bool contactor_error);

} // namespace iso15118::d20::state
