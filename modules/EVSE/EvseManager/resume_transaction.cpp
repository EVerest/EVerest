// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "resume_transaction.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <utils/date.hpp>

namespace module {

namespace {

constexpr auto NEVER = "Never";
constexpr auto ALWAYS = "Always";

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::optional<ResumeSituation> situation_from_string(const std::string& value) {
    for (const auto situation :
         {ResumeSituation::PowerLoss, ResumeSituation::Crash, ResumeSituation::Restart, ResumeSituation::Reboot}) {
        if (value == resume_situation_to_string(situation)) {
            return situation;
        }
    }
    return std::nullopt;
}

template <typename T> void optional_from_json(const nlohmann::json& j, const char* key, std::optional<T>& value) {
    if (j.contains(key) and not j.at(key).is_null()) {
        value = j.at(key).get<T>();
    }
}

} // namespace

std::string resume_situation_to_string(ResumeSituation situation) {
    switch (situation) {
    case ResumeSituation::PowerLoss:
        return "PowerLoss";
    case ResumeSituation::Crash:
        return "Crash";
    case ResumeSituation::Restart:
        return "Restart";
    case ResumeSituation::Reboot:
        return "Reboot";
    case ResumeSituation::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

std::optional<ResumeSituations> parse_resume_situations(const std::string& value) {
    std::vector<std::string> entries;
    std::stringstream stream(value);
    std::string entry;
    while (std::getline(stream, entry, ',')) {
        entries.push_back(trim(entry));
    }
    if (not value.empty() and value.back() == ',') {
        entries.emplace_back();
    }
    if (entries.empty()) {
        return std::nullopt;
    }

    if (entries.size() == 1 and entries.front() == NEVER) {
        return ResumeSituations{};
    }
    if (entries.size() == 1 and entries.front() == ALWAYS) {
        return ResumeSituations{ResumeSituation::PowerLoss, ResumeSituation::Crash, ResumeSituation::Restart,
                                ResumeSituation::Reboot, ResumeSituation::Unknown};
    }

    ResumeSituations situations;
    for (const auto& name : entries) {
        const auto situation = situation_from_string(name);
        if (not situation.has_value()) {
            return std::nullopt;
        }
        situations.insert(situation.value());
    }
    return situations;
}

void to_json(nlohmann::json& j, const StoredTransaction& t) {
    j = nlohmann::json{
        {"session_uuid", t.session_uuid},
        {"clean_shutdown", t.clean_shutdown},
    };
    if (t.resume_after_reset) {
        j["resume_after_reset"] = true;
        j["energy_transfer_paused"] = t.energy_transfer_paused;
    }
    if (t.id_token.has_value()) {
        j["id_token"] = t.id_token.value();
    }
    if (t.start_signed_meter_value.has_value()) {
        j["start_signed_meter_value"] = t.start_signed_meter_value.value();
    }
    if (t.start_meter_value.has_value()) {
        j["start_meter_value"] = t.start_meter_value.value();
    }
    if (t.transaction_started_at.has_value()) {
        j["transaction_started_at"] = t.transaction_started_at.value();
    }
    if (t.last_alive.has_value()) {
        j["last_alive"] = t.last_alive.value();
    }
    if (t.boot_id.has_value()) {
        j["boot_id"] = t.boot_id.value();
    }
}

void from_json(const nlohmann::json& j, StoredTransaction& t) {
    t.session_uuid = j.at("session_uuid").get<std::string>();
    t.clean_shutdown = j.value("clean_shutdown", false);
    t.resume_after_reset = j.value("resume_after_reset", false);
    t.energy_transfer_paused = j.value("energy_transfer_paused", false);
    optional_from_json(j, "id_token", t.id_token);
    optional_from_json(j, "start_signed_meter_value", t.start_signed_meter_value);
    optional_from_json(j, "start_meter_value", t.start_meter_value);
    optional_from_json(j, "transaction_started_at", t.transaction_started_at);
    optional_from_json(j, "last_alive", t.last_alive);
    optional_from_json(j, "boot_id", t.boot_id);
}

ResumeSituation detect_resume_situation(const StoredTransaction& stored,
                                        const std::optional<std::string>& current_boot_id) {
    if (not stored.boot_id.has_value() or not current_boot_id.has_value()) {
        return ResumeSituation::Unknown;
    }
    const bool same_boot = stored.boot_id.value() == current_boot_id.value();
    if (same_boot) {
        return stored.clean_shutdown ? ResumeSituation::Restart : ResumeSituation::Crash;
    }
    return stored.clean_shutdown ? ResumeSituation::Reboot : ResumeSituation::PowerLoss;
}

ResumeDecision decide_resume(const StoredTransaction& stored, const std::optional<std::string>& current_boot_id,
                             const ResumeLimits& limits, std::chrono::time_point<date::utc_clock> now) {
    if (stored.resume_after_reset) {
        // OCPP 2.1 B13.FR.23: resumed even when the reset took longer than TxResumptionTimeout
        if (not stored.id_token.has_value()) {
            return {false, "the stored transaction has no id token"};
        }
        return {true, "reset with resumption"};
    }
    if (not limits.valid_configuration) {
        return {false, "resume_transaction_on_startup is invalid"};
    }
    if (limits.situations.empty()) {
        return {false, "resume_transaction_on_startup is Never"};
    }
    if (not stored.id_token.has_value()) {
        return {false, "the stored transaction has no id token"};
    }

    const auto situation = detect_resume_situation(stored, current_boot_id);
    if (limits.situations.count(situation) == 0) {
        return {false, "situation " + resume_situation_to_string(situation) +
                           " is not listed in resume_transaction_on_startup"};
    }

    if (limits.max_outage.count() > 0) {
        if (not stored.last_alive.has_value()) {
            return {false, "the outage duration is unknown"};
        }
        const auto last_alive = Everest::Date::try_from_rfc3339(stored.last_alive.value());
        if (not last_alive.has_value()) {
            return {false, "the outage duration is unknown"};
        }
        if (now < last_alive.value()) {
            return {false, "the system clock is before the last-alive time " + stored.last_alive.value() +
                               "; without a real-time clock set resume_transaction_max_outage_s to 0"};
        }
        const auto outage = std::chrono::duration_cast<std::chrono::seconds>(now - last_alive.value());
        if (outage > limits.max_outage) {
            return {false, "the outage of " + std::to_string(outage.count()) + " s exceeds " +
                               std::to_string(limits.max_outage.count()) + " s"};
        }
    }

    return {true, "situation " + resume_situation_to_string(situation)};
}

std::optional<std::string> read_boot_id() {
    std::ifstream file("/proc/sys/kernel/random/boot_id");
    std::string boot_id;
    if (not std::getline(file, boot_id)) {
        return std::nullopt;
    }
    boot_id = trim(boot_id);
    if (boot_id.empty()) {
        return std::nullopt;
    }
    return boot_id;
}

} // namespace module
