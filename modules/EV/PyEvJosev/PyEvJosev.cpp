// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "PyEvJosev.hpp"

namespace module {

void PyEvJosev::init() {
    invoke_init(*p_ev);
}

void PyEvJosev::ready() {
    invoke_ready(*p_ev);
}

} // namespace module
