// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "TestConfigObserver.hpp"

namespace module {

void TestConfigObserver::init() {
    invoke_init(*p_main);
}

void TestConfigObserver::ready() {
    invoke_ready(*p_main);
}

void TestConfigObserver::shutdown() {
    invoke_shutdown(*p_main);
}

} // namespace module
