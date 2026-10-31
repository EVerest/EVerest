// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/dc_cable_check.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_CableCheck : public StateBase {
public:
    // Only begin() makes one, so whether the first request went out is decided where it is sent.
    class Entry {
        explicit Entry(bool request_sent_) : request_sent(request_sent_) {
        }
        bool request_sent;
        friend DC_CableCheck;
    };

    DC_CableCheck(Context& ctx, Entry entry) :
        StateBase(ctx, StateID::DC_CableCheck), request_sent(entry.request_sent) {
    }

    // Sends the first DC_CableCheckReq and creates the state awaiting it. [V2G20-912]: while CP is not
    // yet in state C or D, it creates the state holding that request for the CpState event instead.
    static Result begin(Context& ctx);

    static message_20::DC_CableCheckRequest make_request();

    void enter() final;
    Result feed(Event) final;

private:
    bool request_sent;
};

} // namespace iso15118::ev::d20::state
