// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the top-level SLAC machine (see machine.hpp).

#pragma once
#include "../common.hpp"

namespace everest::lib::slac::msm {

// Guards
struct cfg_wait_for_link {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->slac_config.link_status.do_detect;
    }
};

struct is_legacy_set_key_handling_mode {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->slac_config.set_key_handling_mode == fsm::evse::SetKeyHandlingMode::legacy_single_attempt;
    }
};

// Actions
struct on_matched_fail {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        auto& ctx = *fsm.ctx;
        ctx.log_error("Connection lost in matched state");
        ctx.signal_error_routine_request();
    }
};

} // namespace everest::lib::slac::msm
