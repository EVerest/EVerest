// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/params/dc.hpp>
#include <iso15118/message/dc_pre_charge.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_PreCharge : public StateBase {
public:
    explicit DC_PreCharge(Context& ctx) : StateBase(ctx, StateID::DC_PreCharge) {
    }

    static message_20::DC_PreChargeRequest make_request(message_20::datatypes::Processing processing,
                                                        const params::DcChargeParams& params);

    Result feed(Event) final;

private:
    // Set once the closing EVProcessing=Finished request has been sent. The next
    // response is the SECC's acknowledgement of it rather than another voltage
    // reading, so it must not be re-checked against the tolerance.
    bool finished_sent{false};
};

} // namespace iso15118::ev::d20::state
