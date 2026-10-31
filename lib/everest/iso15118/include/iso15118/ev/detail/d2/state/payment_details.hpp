// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/message_2/payment_details.hpp>

namespace iso15118::ev::d2::state {

namespace payment_details {

message_2::PaymentDetailsRequest create_request(const ContractMaterial& contract);

} // namespace payment_details

} // namespace iso15118::ev::d2::state
