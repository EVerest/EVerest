// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2 {

namespace dt = message_2::datatypes;

// Facts established during a session and consulted by later states. They cannot be handed on through
// a constructor: most of their readers have several predecessors, only one of which produced the
// value -- PowerDelivery is reached from four states, only one of which ran a ChargeParameterDiscovery,
// and [V2G2-673] validates the ChargingProfile against *the latest* one. So they stay on the Context
// but read-only, changed only through named setters that each have exactly one calling state.
struct SessionParameters {
    // SessionSetup: an EV rejoining a paused session [V2G2-463].
    bool session_resumed{false};

    bool contract_selected{false};
    bool cert_install_selected{false};
    bool cert_update_selected{false};

    // Empty on an ExternalPayment (EIM) session, which has no contract.
    std::vector<uint8_t> contract_leaf_der{};
    std::string contract_emaid{};
    std::string contract_chain_pem{};

    // Re-established on every renegotiation, hence "the latest" of [V2G2-673]. The energy transfer mode
    // is not kept: the flow it selects is carried by the state types instead.
    uint8_t sa_schedule_tuple_id{1};
    dt::SAScheduleList sa_schedule_list{};

    // CHARGE_LOOP_STARTED is raised once per PowerDelivery(Start), not once per charge-loop state
    // instance -- the loop state is rebuilt around a metering receipt.
    bool charge_loop_started{false};    // DcChargeLoop, cleared by PowerDelivery(Start)
    bool receipt_received{false};       // MeteringReceipt
    bool power_delivery_started{false}; // PowerDelivery(Start); gates Renegotiate [V2G2-812]
    bool power_delivery_stopped{false}; // PowerDelivery(Stop); arms the CP State B gate [V2G2-913]
};

} // namespace iso15118::d2
