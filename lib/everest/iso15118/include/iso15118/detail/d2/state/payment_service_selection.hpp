// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/service_discovery.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// resumed_payment_option: set on a resumed (OK_OldSessionJoined) session; only the option offered in
// the resumed ServiceDiscoveryRes is accepted, anything else is FAILED_PaymentSelectionInvalid
// [V2G2-741]/[V2G2-465].
message_2::PaymentServiceSelectionResponse
handle_request(const message_2::PaymentServiceSelectionRequest& req, const dt::SessionId& session_id,
               uint16_t charge_service_id, bool eim_allowed, bool contract_allowed, bool cert_service_offered = false,
               const std::optional<dt::PaymentOption>& resumed_payment_option = std::nullopt,
               const dt::ServiceList& offered_vas_services = {});

} // namespace iso15118::d2::state
