// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2022 Pionix GmbH and Contributors to EVerest
#include "EvseSlacNeo.hpp"

#include "main/slacImpl.hpp"

namespace module {

EvseSlacNeo::~EvseSlacNeo() {
    shutdown();
}

void EvseSlacNeo::shutdown() {
    // Transitional shim until generated implementation base classes expose a real shutdown virtual. Keep the cast
    // local so the generated module surface can be replaced cleanly when that hook exists.
    if (auto* main_impl = dynamic_cast<main::slacImpl*>(p_main.get())) {
        main_impl->shutdown();
    }
}

void EvseSlacNeo::init() {
    invoke_init(*p_main);
}

void EvseSlacNeo::ready() {
    invoke_ready(*p_main);
}

} // namespace module
