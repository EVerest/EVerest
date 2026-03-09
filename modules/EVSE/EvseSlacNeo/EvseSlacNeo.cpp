// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2022 Pionix GmbH and Contributors to EVerest
#include "EvseSlacNeo.hpp"

namespace module {

void EvseSlacNeo::init() {
    invoke_init(*p_main);
}

void EvseSlacNeo::ready() {
    invoke_ready(*p_main);
}

} // namespace module
