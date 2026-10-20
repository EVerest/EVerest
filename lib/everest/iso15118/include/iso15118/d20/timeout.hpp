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
    // Bounds how long the first DC_WeldingDetectionRes waits for a value from the welding detection
    // voltage source.
    WELDING_VOLTAGE,
};

constexpr uint8_t TIMEOUT_TYPE_SIZE = 5;

static_assert(TIMEOUT_TYPE_SIZE == to_underlying_value(TimeoutType::WELDING_VOLTAGE) + 1,
              "TIMEOUT_TYPE_SIZE should be in sync with the TimeoutType enum definition");

constexpr auto TIMEOUT_ONGOING = 1000 * 55;
constexpr auto TIMEOUT_SEQUENCE = 1000 * 60;
constexpr auto TIMEOUT_EIM_ONGOING = 1000 * 60 * 3;
// How long a DC_WeldingDetectionRes waits for marked values of the welding detection voltage source received
// after its request (IEC 61851-23:2023 CC.6.3, Table CC.9 t105 to t106): within the 1,5 s
// V2G_SECC_Msg_Performance_Time(DC_WeldingDetectionRes) (ISO 15118-20 Table 215), 100 ms kept for sending.
// EvseV2G waits as long.
constexpr auto TIMEOUT_WELDING_VOLTAGE = 1400;
// How long PowerDeliveryRes(Stop) waits for the contactor to be reported open [IEC 61851-23:2023 Table CC.9
// t103 before t104]: within the 1,5 s V2G_SECC_Msg_Performance_Time(PowerDeliveryRes) (Table 215), 100 ms kept
// for sending.
constexpr auto TIMEOUT_CONTACTOR_OPEN = 1400;

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
