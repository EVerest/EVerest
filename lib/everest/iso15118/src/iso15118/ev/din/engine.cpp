// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/engine.hpp>

#include <iso15118/ev/detail/din/state/session_setup.hpp>

namespace iso15118::ev::din {

void EngineTraits::send_first_request(Context& ctx) {
    ctx.send_request(state::session_setup::create_request(ctx.params().evcc_mac));
}

} // namespace iso15118::ev::din
