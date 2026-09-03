// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "McsEvDataLink.hpp"

namespace module {

void McsEvDataLink::init() {
    invoke_init(*p_main);
}

void McsEvDataLink::ready() {
    invoke_ready(*p_main);
}

void McsEvDataLink::shutdown() {
    invoke_shutdown(*p_main);
}

} // namespace module
