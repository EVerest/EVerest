// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "payment_terminalImpl.hpp"

namespace module {
namespace payment_terminal {

void payment_terminalImpl::init() {
}

void payment_terminalImpl::ready() {
}

void payment_terminalImpl::handle_enable_card_reading(
    std::vector<types::authorization::AuthorizationType>& supported_cards, int& connector_id) {
    // your code for cmd enable_card_reading goes here
}

void payment_terminalImpl::handle_allow_all_cards_for_every_connector() {
    // your code for cmd allow_all_cards_for_every_connector goes here
}

} // namespace payment_terminal
} // namespace module
