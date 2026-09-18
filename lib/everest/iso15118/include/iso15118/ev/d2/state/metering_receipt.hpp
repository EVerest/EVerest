// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::ev::d2::state {

// Plug & Charge: signs the MeterInfo of the charge-loop response that set ReceiptRequired and returns to
// the loop it came from.
struct MeteringReceipt : public StateBase {
    MeteringReceipt(Context& ctx, message_2::datatypes::MeterInfo meter_info,
                    std::optional<uint8_t> sa_schedule_tuple_id) :
        StateBase(ctx, StateID::MeteringReceipt),
        m_meter_info(std::move(meter_info)),
        m_sa_schedule_tuple_id(sa_schedule_tuple_id) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    message_2::datatypes::MeterInfo m_meter_info;
    std::optional<uint8_t> m_sa_schedule_tuple_id;
};

} // namespace iso15118::ev::d2::state
