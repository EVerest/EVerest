// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "TestFrameworkTransportProvider.hpp"

namespace module {

void TestFrameworkTransportProvider::init() {
    invoke_init(*p_main);
}

void TestFrameworkTransportProvider::ready() {
    invoke_ready(*p_main);
}

} // namespace module
