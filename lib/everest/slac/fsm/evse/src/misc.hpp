// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_MISC_HPP
#define EVSE_SLAC_MISC_HPP

#include <cstdint>
#include <string>

std::string format_nmk(const uint8_t* nmk);

std::string format_mac_addr(const uint8_t* mac);

std::string format_run_id(const uint8_t* run_id);

std::string format_mmtype(const uint16_t mmtype);

// Fills the given buffer (slac::defs::NMK_LEN bytes) with a freshly generated random NMK.
void generate_nmk(uint8_t* out);

#endif // EVSE_SLAC_MISC_HPP
