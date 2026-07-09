// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Evse15118D20.hpp"

namespace module {

void Evse15118D20::init() {
    invoke_init(*p_charger);
    invoke_init(*p_extensions);
    invoke_init(*p_grid_support);
}

void Evse15118D20::ready() {
    invoke_ready(*p_grid_support);
    invoke_ready(*p_extensions);
    // Invoked last: the charger ready blocks on the libiso15118 controller event loop.
    invoke_ready(*p_charger);
}

void Evse15118D20::set_active_der_directives(const types::grid_support::ActiveDirectiveSet& directives) {
    *active_der_directives.handle() = directives;
}

std::optional<types::grid_support::ActiveDirectiveSet> Evse15118D20::get_active_der_directives() const {
    return *active_der_directives.handle();
}

} // namespace module
