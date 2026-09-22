// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// Isolation test poll: resend CableCheckReq (EVReady=true) until EVSEProcessing=Finished, then
// advance to PreCharge. The Session's ongoing guard bounds both the poll and the CP-state wait.
struct CableCheck : public StateBase {
    // Only begin() makes one, so whether the first request went out is decided where it is sent.
    class Entry {
        explicit Entry(bool request_sent_) : request_sent(request_sent_) {
        }
        bool request_sent;
        friend CableCheck;
    };

    CableCheck(Context& ctx, Entry entry) : StateBase(ctx, StateID::CableCheck), request_sent(entry.request_sent) {
    }

    // Sends the first CableCheckReq and creates the state awaiting it. [V2G-DC-547]: while CP is not yet in
    // state C or D, it creates the state holding that request for the CpState event instead.
    static Result begin(Context& ctx);

    void enter() final;
    Result feed(Event) final;

private:
    bool request_sent;
};

} // namespace iso15118::ev::din::state
