// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <optional>
#include <vector>

#include <iso15118/io/time.hpp>

namespace iso15118::d20 {

template <typename T> constexpr auto to_underlying_value(T t) {
    return static_cast<std::underlying_type_t<T>>(t);
}

enum class TimeoutType : uint8_t {
    SEQUENCE = 0,
    PERFORMANCE,
    ONGOING,
    CONTACTOR,
    // V2G_SECC_CPState_Detection_Timeout: waiting for CP State B after the request following
    // PowerDelivery(off) (DIN [V2G-DC-988]/[V2G-DC-556], ISO-2 [V2G2-920..922]).
    CPSTATE,
    // Guard armed when the charger requests a stop (StopCharging control event): grace period for the
    // EV to end the session on its own before the SECC fails every further response (DIN / ISO-2).
    STOP_CHARGING,
    // Guard armed when the module reports an emergency shutdown: the session ends with a FAILED
    // response to the EV's next request, and this bounds the wait for that request (DIN / ISO-2).
    EMERGENCY_SHUTDOWN,
};

constexpr uint8_t TIMEOUT_TYPE_SIZE = 7;

static_assert(TIMEOUT_TYPE_SIZE == to_underlying_value(TimeoutType::EMERGENCY_SHUTDOWN) + 1,
              "TIMEOUT_TYPE_SIZE should be in sync with the TimeoutType enum definition");

constexpr auto TIMEOUT_ONGOING = 1000 * 55;
constexpr auto TIMEOUT_SEQUENCE = 1000 * 60;
constexpr auto TIMEOUT_EIM_ONGOING = 1000 * 60 * 3;
// Grace period granted to the EV to end the session after an EVSE-initiated stop before the SECC
// enforces it with FAILED responses (EvseV2G handle_stop_charging: 10 s graceful-shutdown window).
constexpr auto TIMEOUT_STOP_CHARGING_GUARD = 1000 * 10;
// How long the SECC waits for the EV's next request to carry the FAILED response announcing an
// emergency shutdown before closing the connection anyway. Short on purpose: the physical shutdown is
// enforced over the control pilot (IEC 61851-1/-23), not over V2G ([V2G2-880] NOTE 1, [V2G-DC-638]
// NOTE 2), so this only bounds how long the reason stays deliverable.
constexpr auto TIMEOUT_EMERGENCY_SHUTDOWN_GUARD = 1000 * 2;

class Timeouts {
public:
    explicit Timeouts() = default;
    ~Timeouts() = default;

    void start_timeout(TimeoutType type, uint32_t timeout_ms);
    void stop_timeout(TimeoutType type);
    void reset_timeout(TimeoutType type);
    std::optional<std::vector<TimeoutType>> check();

private:
    std::array<std::optional<Timeout>, TIMEOUT_TYPE_SIZE> timeouts;
};

} // namespace iso15118::d20
