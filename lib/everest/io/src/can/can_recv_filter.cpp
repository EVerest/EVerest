// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/can/can_recv_filter.hpp>

namespace everest::lib::io::can {

can_recv_filter can_recv_filter::reject_match(uint32_t id, uint32_t mask, bool extended) {
    return can_recv_filter{id, mask, extended, true};
}

} // namespace everest::lib::io::can
