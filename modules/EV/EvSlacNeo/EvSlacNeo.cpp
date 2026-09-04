// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "EvSlacNeo.hpp"

namespace module {

void EvSlacNeo::init() {
    invoke_init(*p_main);
}

void EvSlacNeo::ready() {
    invoke_ready(*p_main);
}

void EvSlacNeo::shutdown() {
    invoke_shutdown(*p_main);
}

} // namespace module
