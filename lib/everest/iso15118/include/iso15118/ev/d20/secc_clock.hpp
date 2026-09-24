// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace iso15118::ev::d20 {

// SECC time as the EV sees it, in microseconds (8.3.3.3). Results saturate at UINT64_MAX.
class SeccClock {
public:
    using SystemNow = std::uint64_t (*)();
    using SteadyNow = std::chrono::steady_clock::time_point (*)();

    struct Reference {
        std::uint64_t secc_time;
        std::chrono::steady_clock::time_point taken_at;
    };

    // Carried across a pause so a resumed session continues in SECC time. The steady time point
    // only has meaning within the process that took it.
    struct State {
        std::optional<Reference> reference{std::nullopt};
        std::optional<std::uint64_t> last_stamp{std::nullopt};
    };

    // Local UTC in microseconds.
    static std::uint64_t system_now_us();

    explicit SeccClock(SystemNow system_now = system_now_us, SteadyNow steady_now = std::chrono::steady_clock::now) :
        system_now_(system_now), steady_now_(steady_now) {
    }

    // [V2G20-1535] the local UTC guess until synchronized, SECC time after. The EV takes its system
    // clock as UTC and never sends the "zero" that [V2G20-1535] allows an EV without UTC knowledge;
    // sending it would be a useful test of a charger.
    std::uint64_t now() const;

    // [V2G20-1536] SessionSetupRes TimeStamp, taken on receipt. Elapsed time is measured on the
    // steady clock, so a local wall-clock step does not move SECC time.
    void synchronize(std::uint64_t secc_time);

    // [V2G20-1537] strictly greater than the previous stamp and the synchronized reference.
    // [V2G20-1538] only the first synchronize may pull stamps below the SessionSetupReq guess; a
    // resumed clock is already synchronized, so its stamps never go back.
    std::uint64_t stamp();

    // A Unix-epoch time in microseconds as SECC time: shifted by the SECC-minus-local-UTC offset,
    // identity before synchronize.
    std::uint64_t from_unix(std::uint64_t unix_time) const;

    State state() const {
        return state_;
    }

    void resume(const State& state) {
        state_ = state;
    }

private:
    SystemNow system_now_;
    SteadyNow steady_now_;
    State state_{};
};

} // namespace iso15118::ev::d20
