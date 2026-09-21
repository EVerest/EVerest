// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

// 23 -> 19 bytes, because SafetyErrorFlags went from 8 to 4 when its bitfields were trimmed from
// 33 to 32 bits (see cb_common.h). Nothing was added or removed; every field after error_flags
// simply moved down by four.
//
// Both layouts written out in full, because the previous "11+4+4+2" shorthand hid which fields the
// leading term covered and was misread more than once:
//
//   before (23)                          after (19)
//     0  reset_reason      1               0  reset_reason      1
//     1  cp_state          1               1  cp_state          1
//     2  relay_state       1               2  relay_state       1
//     3  error_flags       8               3  error_flags       4
//    11  pp_state_type1    1               7  pp_state_type1    1
//    12  pp_state_type2    1               8  pp_state_type2    1
//    13  lock_state        1               9  lock_state        1
//    14  hv_mV             4              10  hv_mV             4
//    18  stop_charging     1              14  stop_charging     1
//    19  cp_duty_cycle     2              15  cp_duty_cycle     2
//    21  ce_state          1              17  ce_state          1
//    22  id_state          1              18  id_state          1
//
// Asserted as a sum and as a literal, so a field change fails the first and a padding change the
// second.
CB_STATIC_ASSERT(sizeof(struct evse_bsp_cb_to_host)==
                     (1+1+1) + 4 + (1+1+1) + 4 + 1 + 2 + (1+1),
                 "Wrong evse_bsp_cb_to_host size!!!");
CB_STATIC_ASSERT(sizeof(struct evse_bsp_cb_to_host)== 19, "Wrong evse_bsp_cb_to_host size!!!");
CB_STATIC_ASSERT(sizeof(SafetyErrorFlags)== 4, "SafetyErrorFlags must be exactly 32 bits wide!");
