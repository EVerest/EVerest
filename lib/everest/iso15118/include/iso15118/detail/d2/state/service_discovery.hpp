// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/service_discovery.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// resumed_payment_option: set on a resumed (OK_OldSessionJoined) session; the PaymentOptionList then
// contains only the option selected in the paused session [V2G2-741].
message_2::ServiceDiscoveryResponse
handle_request(const message_2::ServiceDiscoveryRequest& req, const dt::SessionId& session_id,
               uint16_t charge_service_id,
               const everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6>& supported_modes, bool offer_eim,
               bool offer_contract, bool cert_service_offered,
               const std::optional<dt::PaymentOption>& resumed_payment_option = std::nullopt);

} // namespace iso15118::d2::state
