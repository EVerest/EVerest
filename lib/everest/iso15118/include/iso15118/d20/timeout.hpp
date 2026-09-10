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
    // Waiting for CP State B after the request following PowerDelivery(off) (DIN
    // [V2G-DC-988]/[V2G-DC-556], ISO-2 [V2G2-920..922]).
    CPSTATE,
    // Grace period for the EV to end the session on its own before the SECC fails every response.
    STOP_CHARGING,
    // Bounds the wait for the request that carries the emergency-shutdown FAILED response.
    EMERGENCY_SHUTDOWN,
};

constexpr uint8_t TIMEOUT_TYPE_SIZE = 7;

static_assert(TIMEOUT_TYPE_SIZE == to_underlying_value(TimeoutType::EMERGENCY_SHUTDOWN) + 1,
              "TIMEOUT_TYPE_SIZE should be in sync with the TimeoutType enum definition");

constexpr auto TIMEOUT_ONGOING = 1000 * 55;
constexpr auto TIMEOUT_SEQUENCE = 1000 * 60;
constexpr auto TIMEOUT_EIM_ONGOING = 1000 * 60 * 3;
// EvseV2G handle_stop_charging grants the same 10 s graceful-shutdown window.
constexpr auto TIMEOUT_STOP_CHARGING_GUARD = 1000 * 10;
// Short on purpose: the physical shutdown is enforced over the control pilot (IEC 61851-1/-23), not
// over V2G ([V2G2-880] NOTE 1, [V2G-DC-638] NOTE 2), so this only bounds how long the reason stays
// deliverable.
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
