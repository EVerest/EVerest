// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <iso15118/ev/ac_phase_split.hpp>

namespace iso15118::ev {

namespace dt = message_20::datatypes;

AcPhaseLimits split_ac_limit(float total, uint8_t phase_count, dt::AcConnector connector) {
    // No AC connector offers more than three lines, and a zero count would divide by zero.
    const auto lines = std::clamp<unsigned>(phase_count, 1u, 3u);
    const auto per_line = total / static_cast<float>(lines);

    if (connector != dt::AcConnector::ThreePhase) {
        return {per_line, std::nullopt, std::nullopt};
    }

    if (lines == 3u) {
        return {per_line, per_line, per_line};
    }

    // A single-line EV on a three-phase connector puts everything on L1. The zero peers make the
    // base element read as L1 rather than as a sum.
    return {total, 0.0f, 0.0f};
}

namespace {

void assign(std::optional<dt::RationalNumber>& out, const std::optional<float>& value) {
    out = value.has_value() ? std::make_optional(dt::from_float(*value)) : std::nullopt;
}

} // namespace

void emit_ac_limit(float total, uint8_t phase_count, dt::AcConnector connector, dt::RationalNumber& base,
                   std::optional<dt::RationalNumber>& l2, std::optional<dt::RationalNumber>& l3) {
    const auto split = split_ac_limit(total, phase_count, connector);
    base = dt::from_float(split.base);
    assign(l2, split.l2);
    assign(l3, split.l3);
}

void emit_ac_present(float aggregate, uint8_t phase_count, dt::AcConnector connector, dt::RationalNumber& base,
                     std::optional<dt::RationalNumber>& l2, std::optional<dt::RationalNumber>& l3) {
    // A measurement is one aggregate reading: on a single-phase connector the EV draws all of it
    // on the one line, so it is never divided there.
    if (connector != dt::AcConnector::ThreePhase) {
        base = dt::from_float(aggregate);
        l2.reset();
        l3.reset();
        return;
    }
    emit_ac_limit(aggregate, phase_count, connector, base, l2, l3);
}

} // namespace iso15118::ev
