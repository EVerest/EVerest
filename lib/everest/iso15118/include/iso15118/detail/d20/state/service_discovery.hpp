// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/d20/common_types.hpp>
#include <iso15118/message/d20/service_discovery.hpp>

namespace iso15118::d20::state {

msg::d20::ServiceDiscoveryResponse
handle_request(const msg::d20::ServiceDiscoveryRequest& req, d20::Session& session,
               const std::vector<msg::d20::datatypes::ServiceCategory>& energy_services,
               const std::vector<uint16_t>& vas_services,
               std::vector<msg::d20::datatypes::ServiceCategory>& ev_energy_services);

} // namespace iso15118::d20::state
