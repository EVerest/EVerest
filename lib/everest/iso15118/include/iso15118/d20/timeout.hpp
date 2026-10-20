// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
    // Bounds how long a WeldingDetectionRes waits for values of the welding detection voltage source
    // received after its request.
    WELDING_VOLTAGE,
};

constexpr uint8_t TIMEOUT_TYPE_SIZE = 8;

static_assert(TIMEOUT_TYPE_SIZE == to_underlying_value(TimeoutType::WELDING_VOLTAGE) + 1,
              "TIMEOUT_TYPE_SIZE should be in sync with the TimeoutType enum definition");

constexpr auto TIMEOUT_ONGOING = 1000 * 55;
constexpr auto TIMEOUT_SEQUENCE = 1000 * 60;
// EvseV2G handle_stop_charging grants the same 10 s graceful-shutdown window.
constexpr auto TIMEOUT_STOP_CHARGING_GUARD = 1000 * 10;
// Short on purpose: the physical shutdown is enforced over the control pilot (IEC 61851-1/-23), not
// over V2G ([V2G2-880] NOTE 1, [V2G-DC-638] NOTE 2), so this only bounds how long the reason stays
// deliverable.
constexpr auto TIMEOUT_EMERGENCY_SHUTDOWN_GUARD = 1000 * 2;
// How long a WeldingDetectionRes waits for marked values of the welding detection voltage source received
// after its request (IEC 61851-23:2023 CC.6.3, Table CC.9 t105 to t106): within the 1,5 s
// V2G_SECC_Msg_Performance_Time(WeldingDetectionRes) of ISO 15118-2 (Table 109), DIN 70121 and ISO 15118-20
// (Table 215), 100 ms kept for sending; runs concurrently with the CP State B wait. EvseV2G waits as long.
constexpr auto TIMEOUT_WELDING_VOLTAGE = 1400;
// How long PowerDeliveryRes(Stop) waits for the contactor to be reported open [IEC 61851-23:2023 Table CC.9
// t103 before t104]: within V2G_SECC_Msg_Performance_Time(PowerDeliveryRes), 4,5 s in ISO 15118-2 (Table 109),
// 1,5 s in DIN 70121 (Table 75) and ISO 15118-20 (Table 215), 100 ms kept for sending. EvseV2G waits as long.
constexpr auto TIMEOUT_CONTACTOR_OPEN_ISO2 = 3000;
constexpr auto TIMEOUT_CONTACTOR_OPEN = 1400;

class Timeouts {
public:
    explicit Timeouts() = default;
    ~Timeouts() = default;

    void start_timeout(TimeoutType type, uint32_t timeout_ms);
    void stop_timeout(TimeoutType type);
    void reset_timeout(TimeoutType type);
    std::vector<TimeoutType> check() const;

private:
    std::array<std::optional<Timeout>, TIMEOUT_TYPE_SIZE> timeouts;
};

} // namespace iso15118::d20
