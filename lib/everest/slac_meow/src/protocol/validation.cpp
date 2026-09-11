// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/protocol/validation.hpp>

#include <everest/slac/protocol/defs.hpp>

namespace everest::slac::protocol {

bool validate_message(messages::cm_slac_parm_req const& msg) {
    if (msg.application_type not_eq defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq defs::COMMON_SECURITY_TYPE) {
        return false;
    }

    return true;
}

} // namespace everest::slac::protocol
