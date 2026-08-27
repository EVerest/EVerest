// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/service_discovery.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::ServiceDiscoveryResponse
handle_request(const message_2::ServiceDiscoveryRequest& req, const dt::SessionId& session_id,
               uint16_t charge_service_id,
               const everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6>& supported_modes, bool pnc_enabled,
               bool cert_service_offered);

} // namespace iso15118::d2::state
