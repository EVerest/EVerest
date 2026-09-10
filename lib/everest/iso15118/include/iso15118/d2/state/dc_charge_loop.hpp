// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <tuple>

#include "../states.hpp"

namespace iso15118::d2::state {

struct DcChargeLoop : public StateBase {
    DcChargeLoop(Context& ctx) : StateBase(ctx, StateID::DcChargeLoop) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    Result process_request(const message_2::CurrentDemandRequest& req);
};

} // namespace iso15118::d2::state
