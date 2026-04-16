// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "RsPaymentTerminal.hpp"

namespace module {

void RsPaymentTerminal::init() {
    invoke_init(*p_token_provider);
    invoke_init(*p_token_validator);
    invoke_init(*p_payment_terminal);
}

void RsPaymentTerminal::ready() {
    invoke_ready(*p_token_provider);
    invoke_ready(*p_token_validator);
    invoke_ready(*p_payment_terminal);
}

} // namespace module
