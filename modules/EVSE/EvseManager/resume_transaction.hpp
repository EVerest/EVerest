// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>

#include <date/tz.h>
#include <nlohmann/json.hpp>

#include <generated/types/authorization.hpp>
#include <generated/types/powermeter.hpp>
#include <generated/types/units_signed.hpp>

namespace module {

/// \brief Situation that ended the previous run while a transaction was active
enum class ResumeSituation {
    PowerLoss, ///< boot ID changed, no clean shutdown
    Crash,     ///< same boot ID, no clean shutdown
    Restart,   ///< same boot ID, clean shutdown
    Reboot,    ///< boot ID changed, clean shutdown
    Unknown,   ///< a boot ID is missing
};

using ResumeSituations = std::set<ResumeSituation>;

std::string resume_situation_to_string(ResumeSituation situation);

/// \brief Parse the comma-separated config value resume_transaction_on_startup
/// \returns the situations in which a transaction is resumed, empty for "Never", std::nullopt if the value is invalid
std::optional<ResumeSituations> parse_resume_situations(const std::string& value);

/// \brief Transaction record kept in persistent storage while a transaction is active
struct StoredTransaction {
    std::string session_uuid;
    std::optional<types::authorization::ProvidedIdToken> id_token;
    std::optional<types::units_signed::SignedMeterValue> start_signed_meter_value;
    std::optional<types::powermeter::Powermeter> start_meter_value;
    std::optional<std::string> transaction_started_at;
    std::optional<std::string> last_alive;
    std::optional<std::string> boot_id;
    bool clean_shutdown{false};
    /// set when an OCPP reset that keeps the transaction is about to restart the system
    bool resume_after_reset{false};
    /// with resume_after_reset: the resumed transaction stays paused by the EVSE
    bool energy_transfer_paused{false};
};

void to_json(nlohmann::json& j, const StoredTransaction& t);
void from_json(const nlohmann::json& j, StoredTransaction& t);

ResumeSituation detect_resume_situation(const StoredTransaction& stored,
                                        const std::optional<std::string>& current_boot_id);

struct ResumeLimits {
    ResumeSituations situations;
    /// zero disables the limit
    std::chrono::seconds max_outage{0};
    bool valid_configuration{true};
};

struct ResumeDecision {
    bool resume{false};
    std::string reason;
};

ResumeDecision decide_resume(const StoredTransaction& stored, const std::optional<std::string>& current_boot_id,
                             const ResumeLimits& limits, std::chrono::time_point<date::utc_clock> now);

/// \brief Boot ID of the running kernel, std::nullopt if it cannot be read
std::optional<std::string> read_boot_id();

} // namespace module
