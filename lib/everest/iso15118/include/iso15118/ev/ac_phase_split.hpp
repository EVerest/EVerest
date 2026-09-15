// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev {

/**
 * How one advertised AC total is emitted across the lines of the selected connector.
 *
 * ISO 15118-20 gives the base element two different meanings depending on the connector and on
 * whether the _L2/_L3 peers are present, so the two cannot be decided independently:
 *
 * - SinglePhase: only the base element may be used, and it is that one line's value.
 * - ThreePhase without peers: the base element is the sum of all three lines, evenly distributed.
 * - ThreePhase with peers: the base element is the L1 value alone, so every line the EV draws
 *   on has to carry its own share or the advertised total is understated.
 *
 * Emitting peers is therefore the only correct way for a single-line EV plugged into a
 * three-phase connector: everything on L1, zero on L2 and L3.
 */
struct AcPhaseLimits {
    float base{0.0f};
    std::optional<float> l2;
    std::optional<float> l3;
};

/**
 * Split an advertised AC total across the connector the EV selected.
 *
 * \param total       the EV's advertised limit, as a total across its own \p phase_count lines
 * \param phase_count the EV's own line count, 1 or 3, clamped to 1..3 so 0 cannot divide by zero
 * \param connector   the AC connector of the parameter set the EV selected
 */
AcPhaseLimits split_ac_limit(float total, uint8_t phase_count, message_20::datatypes::AcConnector connector);

/**
 * Split an advertised AC total and write it into a message's base element and _L2/_L3 peers.
 */
void emit_ac_limit(float total, uint8_t phase_count, message_20::datatypes::AcConnector connector,
                   message_20::datatypes::RationalNumber& base,
                   std::optional<message_20::datatypes::RationalNumber>& l2,
                   std::optional<message_20::datatypes::RationalNumber>& l3);

/**
 * Write one aggregate measurement into a message's base element and _L2/_L3 peers.
 *
 * A measurement is one aggregate reading: on a single-phase connector the EV draws all of it on
 * the one line, so it is never divided there. On a ThreePhase connector it is divided like a limit.
 */
void emit_ac_present(float aggregate, uint8_t phase_count, message_20::datatypes::AcConnector connector,
                     message_20::datatypes::RationalNumber& base,
                     std::optional<message_20::datatypes::RationalNumber>& l2,
                     std::optional<message_20::datatypes::RationalNumber>& l3);

} // namespace iso15118::ev
