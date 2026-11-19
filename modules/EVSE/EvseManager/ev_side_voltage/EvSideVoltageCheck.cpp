// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "EvSideVoltageCheck.hpp"

#include <cmath>

namespace module {

EvSideVoltageCheck::EvSideVoltageCheck(Timing timing) : m_timing(timing) {
}

EvSideVoltageCheck::EvSideVoltageCheck() : EvSideVoltageCheck(Timing{}) {
}

void EvSideVoltageCheck::update(double voltage_V, Clock::time_point now) {
    m_last = Sample{voltage_V, now};
    if (voltage_V < -SAFE_VOLTAGE_V) {
        if (not m_reverse_since.has_value()) {
            m_reverse_since = now;
        }
    } else {
        m_reverse_since.reset();
    }
}

EvSideVoltageCheck::State EvSideVoltageCheck::state(Clock::time_point now) const {
    if (not m_last.has_value()) {
        return State::NoSample;
    }
    if (now - m_last->time > m_timing.max_sample_age) {
        return State::Stale;
    }
    return std::fabs(m_last->voltage_V) < SAFE_VOLTAGE_V ? State::Safe : State::Unsafe;
}

std::optional<double> EvSideVoltageCheck::last_voltage_V() const {
    if (not m_last.has_value()) {
        return std::nullopt;
    }
    return m_last->voltage_V;
}

void EvSideVoltageCheck::restart_reverse_voltage() {
    m_reverse_since.reset();
}

bool EvSideVoltageCheck::reverse_voltage() const {
    return m_reverse_since.has_value() and m_last.has_value() and
           m_last->time - *m_reverse_since >= m_timing.reverse_voltage_duration;
}

} // namespace module
