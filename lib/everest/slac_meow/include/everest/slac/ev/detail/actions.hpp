// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <random>

#include <everest/slac/ev/states.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>

// Actions with more than one caller; a state's own go in its source file.
namespace everest::slac::ev {

template <typename ByteRange> void randomize(ByteRange& bytes) {
    std::random_device rnd_dev;
    std::mt19937 rng(rnd_dev());
    std::uniform_int_distribution<int> byte_distribution(0, 0xFF);
    for (auto& octet : bytes) {
        octet = static_cast<std::uint8_t>(byte_distribution(rng));
    }
}

/// A fresh run id and the first CM_SLAC_PARM.REQ. From Reset or Idle.
void start_matching(Context& ctx);

/// Counts the attempt. From start_matching, and every WaitParmCnf retry.
void send_slac_parm_req(Context& ctx);

/// Counts the attempt. From the attenuation acknowledgement, and every WaitMatchCnf retry.
void send_slac_match_req(Context& ctx);

/// From Sounding or WaitAttenCharInd, whichever sees the CM_ATTEN_CHAR.IND.
void send_atten_char_rsp_and_match_req(Context& ctx, messages::HomeplugMessage const& frame);

} // namespace everest::slac::ev
