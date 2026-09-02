// SPDX-License-Identifier: Apache-2.0
#include "Sample.hpp"

namespace module {

void Sample::init() {
    invoke_init(*p_main);
}

void Sample::ready() {
    invoke_ready(*p_main);
}

void Sample::shutdown() {
    invoke_shutdown(*p_main);
}

} // namespace module
