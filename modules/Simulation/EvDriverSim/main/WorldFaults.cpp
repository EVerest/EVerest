// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "WorldFaults.hpp"
#include <cmath>

namespace module {
WorldFaults::Out WorldFaults::on_car_set_rcd_error(double current_mA) {
    m_car_rcd_mA = current_mA;
    Out out;
    if (active() != API_driver::WorldFault::RcdError) {
        out.rcd_mA = current_mA;
    }
    return out;
}
WorldFaults::Out WorldFaults::on_car_diode_fail(bool value) {
    m_car_diode_fail = value;
    Out out;
    if (active() != API_driver::WorldFault::DiodeFail) {
        out.diode_fail = value;
    }
    return out;
}
void WorldFaults::unplug() {
    if (active() == API_driver::WorldFault::CpErrorE) {
        m_active.reset();
    }
}
types::ev_board_support::EvCpState WorldFaults::on_car_set_cp(types::ev_board_support::EvCpState cp) {
    m_car_cp = cp;
    return active() == API_driver::WorldFault::CpErrorE ? types::ev_board_support::EvCpState::E : cp;
}
WorldFaults::Out WorldFaults::inject(const API_driver::InjectFaultParams& params, bool plugged) {
    Out out;
    if (params.type == API_driver::WorldFault::CpErrorE && !plugged) {
        out.rejection = "unplugged";
        return out;
    }
    if (m_active) {
        out.rejection = "a fault is already active";
        return out;
    }
    if (params.type == API_driver::WorldFault::RcdError &&
        (!params.rcd_mA || !std::isfinite(*params.rcd_mA) || *params.rcd_mA < 0)) {
        out.rejection = "RcdError requires a finite nonnegative rcd_mA";
        return out;
    }
    m_active = params;
    return reapply();
}
WorldFaults::Out WorldFaults::reapply() const {
    Out out;
    if (!m_active) {
        return out;
    }
    switch (m_active->type) {
    case API_driver::WorldFault::DiodeFail:
        out.diode_fail = true;
        break;
    case API_driver::WorldFault::RcdError:
        out.rcd_mA = m_active->rcd_mA;
        break;
    case API_driver::WorldFault::CpErrorE:
        out.to_board_cp = types::ev_board_support::EvCpState::E;
        break;
    }
    return out;
}
WorldFaults::Out WorldFaults::clear() {
    Out out;
    if (!m_active) {
        return out;
    }
    switch (m_active->type) {
    case API_driver::WorldFault::DiodeFail:
        out.diode_fail = m_car_diode_fail;
        break;
    case API_driver::WorldFault::RcdError:
        out.rcd_mA = m_car_rcd_mA;
        break;
    case API_driver::WorldFault::CpErrorE:
        out.to_board_cp = m_car_cp;
        break;
    }
    m_active.reset();
    return out;
}
std::optional<API_driver::WorldFault> WorldFaults::active() const {
    return m_active ? std::make_optional(m_active->type) : std::nullopt;
}
} // namespace module
