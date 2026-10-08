// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <EebusCallbacks.hpp>

namespace module {

bool EebusCallbacks::all_callbacks_valid() const {
    return update_limits_callback != nullptr;
}

} // namespace module
