// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "grid_supportImpl.hpp"

namespace module {
namespace grid_support {

void grid_supportImpl::init() {
}

void grid_supportImpl::ready() {
}

types::grid_support::SetDirectivesResponse
grid_supportImpl::handle_set_active_directives(types::grid_support::ActiveDirectiveSet& directives) {
    if (not directives.directives.empty()) {
        std::call_once(directive_relay_log_flag, [] {
            EVLOG_info << "grid_support DER directives are relayed to the EV at the next V2G session "
                          "(next-session-dynamic).";
        });
    }
    mod->set_active_der_directives(directives);
    mod->notify_der_directives_changed();
    types::grid_support::SetDirectivesResponse response;
    response.accepted = true;
    return response;
}

} // namespace grid_support
} // namespace module
