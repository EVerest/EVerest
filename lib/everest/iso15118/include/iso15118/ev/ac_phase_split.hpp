// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev {

/**
 * How ISO 15118-20 reads an AC power element depends on the connector and on whether the L2 and
 * L3 elements accompany it:
 *
 * - [V2G20-1815] SinglePhase: only the base element, never L2 or L3.
 * - [V2G20-1817] ThreePhase, base alone: the sum of all three lines, evenly distributed.
 * - [V2G20-1818] ThreePhase, base with L2 or L3: the L1 value.
 * - [V2G20-1820] A symmetric EV states its limits as that sum, so it sends the base alone.
 *
 * A single-line EV on a three-phase connector is the one case that needs L2 and L3: zero on both
 * makes the base read as L1 rather than as a sum.
 */
struct AcPhaseLimits {
    float base{0.0f};
    std::optional<float> l2;
    std::optional<float> l3;
};

/**
 * Split an advertised AC total across the connector the EV selected.
 *
 * \param total       the EV's advertised limit, as a total across its own lines
 * \param phase_count the EV's own line count, 1 or 3
 * \param connector   the AC connector of the parameter set the EV selected
 */
AcPhaseLimits split_ac_limit(float total, uint8_t phase_count, message_20::datatypes::AcConnector connector);

namespace detail {

inline void assign_line(std::optional<message_20::datatypes::RationalNumber>& out, const std::optional<float>& value) {
    out = value.has_value() ? std::make_optional(message_20::datatypes::from_float(*value)) : std::nullopt;
}

} // namespace detail

/**
 * Split an advertised AC total into a message's base, L2 and L3 elements.
 *
 * \tparam Base RationalNumber, or std::optional of it where the schema makes the base optional.
 */
template <typename Base>
void emit_ac_limit(float total, uint8_t phase_count, message_20::datatypes::AcConnector connector, Base& base,
                   std::optional<message_20::datatypes::RationalNumber>& l2,
                   std::optional<message_20::datatypes::RationalNumber>& l3) {
    const auto split = split_ac_limit(total, phase_count, connector);
    base = message_20::datatypes::from_float(split.base);
    detail::assign_line(l2, split.l2);
    detail::assign_line(l3, split.l3);
}

/**
 * A ratio applies to every line and is not divided. On ThreePhase, L2 and L3 carry it so the base
 * is L1 [V2G20-1818], not a sum [V2G20-1817].
 */
template <typename Base>
void emit_ac_ratio(float value, message_20::datatypes::AcConnector connector, Base& base,
                   std::optional<message_20::datatypes::RationalNumber>& l2,
                   std::optional<message_20::datatypes::RationalNumber>& l3) {
    const auto line =
        connector == message_20::datatypes::AcConnector::ThreePhase ? std::make_optional(value) : std::nullopt;
    base = message_20::datatypes::from_float(value);
    detail::assign_line(l2, line);
    detail::assign_line(l3, line);
}

/**
 * Write one aggregate measurement into a message's base, L2 and L3 elements. A measurement is
 * what the EV draws, so a single-phase connector carries all of it on its one line.
 */
template <typename Base>
void emit_ac_present(float aggregate, uint8_t phase_count, message_20::datatypes::AcConnector connector, Base& base,
                     std::optional<message_20::datatypes::RationalNumber>& l2,
                     std::optional<message_20::datatypes::RationalNumber>& l3) {
    if (connector != message_20::datatypes::AcConnector::ThreePhase) {
        base = message_20::datatypes::from_float(aggregate);
        l2.reset();
        l3.reset();
        return;
    }
    emit_ac_limit(aggregate, phase_count, connector, base, l2, l3);
}

} // namespace iso15118::ev
