// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <string>

namespace everest::slac::utils {

void generate_nmk_hs(std::uint8_t nmk_hs[defs::NMK_LEN], const char* plain_password, int password_len);
void generate_nid_from_nmk(std::uint8_t nid[defs::NID_LEN], const std::uint8_t nmk[defs::NMK_LEN]);
std::string device_info(messages::qualcomm::op_attr_cnf const& mgs);
std::string device_info(messages::lumissil::nscm_get_version_cnf const& msg);

} // namespace everest::slac::utils
