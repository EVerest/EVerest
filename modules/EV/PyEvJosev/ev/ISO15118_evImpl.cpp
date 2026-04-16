// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

namespace module {
namespace ev {

void ISO15118_evImpl::init() {
}

void ISO15118_evImpl::ready() {
}

bool ISO15118_evImpl::handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                            types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                            double& DepartureTime, double& EAmount) {
    // your code for cmd start_charging goes here
    return true;
}

void ISO15118_evImpl::handle_stop_charging() {
    // your code for cmd stop_charging goes here
}

void ISO15118_evImpl::handle_pause_charging() {
    // your code for cmd pause_charging goes here
}

void ISO15118_evImpl::handle_set_fault() {
    // your code for cmd set_fault goes here
}

void ISO15118_evImpl::handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) {
    // your code for cmd set_dc_params goes here
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    // your code for cmd set_bpt_dc_params goes here
}

void ISO15118_evImpl::handle_enable_sae_j2847_v2g_v2h() {
    // your code for cmd enable_sae_j2847_v2g_v2h goes here
}

void ISO15118_evImpl::handle_update_soc(double& SoC) {
    // your code for cmd update_soc goes here
}

} // namespace ev
} // namespace module
