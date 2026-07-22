// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "EvSlacNeo.hpp"

#include "main/ev_slacImpl.hpp"

namespace module {

EvSlacNeo::~EvSlacNeo() {
    shutdown();
}

void EvSlacNeo::shutdown() {
    // Transitional shim until generated implementation base classes expose a real shutdown virtual. Keep the cast
    // local so the generated module surface can be replaced cleanly when that hook exists.
    if (auto* main_impl = dynamic_cast<main::ev_slacImpl*>(p_main.get())) {
        main_impl->shutdown();
    }
}

void EvSlacNeo::init() {
    invoke_init(*p_main);
}

void EvSlacNeo::ready() {
    invoke_ready(*p_main);
}

} // namespace module
