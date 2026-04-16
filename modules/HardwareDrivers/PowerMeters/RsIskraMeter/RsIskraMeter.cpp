// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "RsIskraMeter.hpp"

namespace module {

void RsIskraMeter::init() {
    invoke_init(*p_meter);
}

void RsIskraMeter::ready() {
    invoke_ready(*p_meter);
}

} // namespace module
