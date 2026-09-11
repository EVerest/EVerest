// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <string>

namespace everest::lib::slac::utils {

void generate_nmk_hs(std::uint8_t nmk_hs[slac::defs::NMK_LEN], const char* plain_password, int password_len);
void generate_nid_from_nmk(std::uint8_t nid[slac::defs::NID_LEN], const std::uint8_t nmk[slac::defs::NMK_LEN]);
std::string device_info(messages::qualcomm::op_attr_cnf const& mgs);
std::string device_info(messages::lumissil::nscm_get_version_cnf const& msg);

} // namespace everest::lib::slac::utils
