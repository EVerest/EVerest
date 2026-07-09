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
        std::call_once(directive_seam_log_flag, [] {
            EVLOG_info << "grid_support directives accepted but not yet applied to the EV: the DER "
                          "control-function relay is not implemented; session limits derive from ac_limits.";
        });
    }
    mod->set_active_der_directives(directives);
    types::grid_support::SetDirectivesResponse response;
    response.accepted = true;
    return response;
}

} // namespace grid_support
} // namespace module
