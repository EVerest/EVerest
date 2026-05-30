// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>

namespace everest::lib::io::can {

/**
 * @brief Userspace description of a SocketCAN \c CAN_RAW_FILTER rule.
 *
 * Rules are applied on the raw socket via \c setsockopt(SOL_CAN_RAW, CAN_RAW_FILTER).
 * A frame is delivered when it matches at least one rule (OR). Use \p invert on a rule
 * to drop matching frames instead (\c CAN_INV_FILTER).
 *
 * \p can_id and \p can_mask are the 29-bit (EFF) or 11-bit identifier bits without
 * \c CAN_EFF_FLAG / \c CAN_RTR_FLAG / \c CAN_ERR_FLAG. When \p extended is true,
 * \c CAN_EFF_FLAG is added to both fields when installing the kernel filter.
 */
struct can_recv_filter {
    uint32_t can_id{0};
    uint32_t can_mask{0};
    bool extended{true};
    bool invert{false};

    /**
     * @brief Drop frames whose identifier matches \p id under \p mask.
     * @param[in] id Plain CAN identifier bits (no SocketCAN flags).
     * @param[in] mask Bits to compare; set bits are significant.
     * @param[in] extended If true, match extended (29-bit) frames only.
     */
    static can_recv_filter reject_match(uint32_t id, uint32_t mask, bool extended = true);
};

} // namespace everest::lib::io::can
