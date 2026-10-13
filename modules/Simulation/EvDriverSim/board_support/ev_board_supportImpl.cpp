// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_board_supportImpl.hpp"

namespace module {
namespace board_support {

void ev_board_supportImpl::init() {
}

void ev_board_supportImpl::ready() {
}

void ev_board_supportImpl::shutdown() {
}

void ev_board_supportImpl::handle_enable(bool& value) {
    mod->enable(value);
}

void ev_board_supportImpl::handle_set_cp_state(types::ev_board_support::EvCpState& cp_state) {
    mod->set_cp(cp_state);
}

void ev_board_supportImpl::handle_allow_power_on(bool& value) {
    mod->r_ev_board_support->call_allow_power_on(value);
}

void ev_board_supportImpl::handle_diode_fail(bool& value) {
    mod->diode_fail(value);
}

void ev_board_supportImpl::handle_set_ac_max_current(double& current) {
    mod->r_ev_board_support->call_set_ac_max_current(current);
}

void ev_board_supportImpl::handle_set_three_phases(bool& three_phases) {
    mod->r_ev_board_support->call_set_three_phases(three_phases);
}

void ev_board_supportImpl::handle_set_rcd_error(double& rcd_current_mA) {
    mod->set_rcd_error(rcd_current_mA);
}

} // namespace board_support
} // namespace module
