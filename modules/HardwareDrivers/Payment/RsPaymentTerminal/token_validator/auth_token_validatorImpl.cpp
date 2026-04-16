// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "auth_token_validatorImpl.hpp"

namespace module {
namespace token_validator {

void auth_token_validatorImpl::init() {
}

void auth_token_validatorImpl::ready() {
}

types::authorization::ValidationResult
auth_token_validatorImpl::handle_validate_token(types::authorization::ProvidedIdToken& provided_token) {
    // your code for cmd validate_token goes here
    return {};
}

} // namespace token_validator
} // namespace module
