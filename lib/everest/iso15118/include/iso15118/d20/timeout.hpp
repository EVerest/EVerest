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
    // Bounds the wait of a WeldingDetectionRes for present values received after its request.
    PRESENT_VALUES,
};

constexpr uint8_t TIMEOUT_TYPE_SIZE = 8;

static_assert(TIMEOUT_TYPE_SIZE == to_underlying_value(TimeoutType::PRESENT_VALUES) + 1,
              "TIMEOUT_TYPE_SIZE should be in sync with the TimeoutType enum definition");

constexpr auto TIMEOUT_ONGOING = 1000 * 55;
constexpr auto TIMEOUT_SEQUENCE = 1000 * 60;
// EvseV2G handle_stop_charging grants the same 10 s graceful-shutdown window.
constexpr auto TIMEOUT_STOP_CHARGING_GUARD = 1000 * 10;
// Short on purpose: the physical shutdown is enforced over the control pilot (IEC 61851-1/-23), not
// over V2G ([V2G2-880] NOTE 1, [V2G-DC-638] NOTE 2), so this only bounds how long the reason stays
// deliverable.
constexpr auto TIMEOUT_EMERGENCY_SHUTDOWN_GUARD = 1000 * 2;
// How long PowerDeliveryRes(stop) waits for DcOutputDisabled. ISO 15118-2: within
// V2G_SECC_Msg_Performance_Time(PowerDeliveryRes) of 4.5 s (Table 109). DIN SPEC 70121: within
// V2G_SECC_Msg_Performance_Time(PowerDeliveryRes) of 1.5 s (Table 75), 100 ms kept for polling and sending.
// EvseV2G waits as long (V2G_DC_OUTPUT_DISABLED_TIMEOUT, V2G_DC_OUTPUT_DISABLED_TIMEOUT_DIN).
constexpr auto TIMEOUT_DC_OUTPUT_DISABLED_ISO2 = 1000 * 3;
constexpr auto TIMEOUT_DC_OUTPUT_DISABLED_DIN = 1400;
// How long a WeldingDetectionRes waits for present values received after its request, so that the
// EVSEPresentVoltage is measured after the request and not before the PowerDeliveryRes [IEC 61851-23:2023
// CC.6.3, CC.7.6.17]. V2G_SECC_Msg_Performance_Time(WeldingDetectionRes) is 1.5 s in ISO 15118-2 (Table 109),
// DIN SPEC 70121 and ISO 15118-20 (DC_WeldingDetectionRes), 100 ms kept for polling and sending.
// EvseV2G waits as long (V2G_PRESENT_VALUES_TIMEOUT).
constexpr auto TIMEOUT_PRESENT_VALUES = 1400;
// How many present values updates a WeldingDetectionRes waits for. The first update received after the
// request can carry a measurement taken just before it (a polled meter delivers a sample up to one poll
// interval after taking it); the second one was taken after the first, so after the request. At the
// timeout the last values are sent whatever arrived. EvseV2G counts the same (V2G_PRESENT_VALUES_NEEDED).
constexpr uint8_t PRESENT_VALUES_NEEDED = 2;

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
