// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/monitor_subscription.hpp>

#include <everest/logging.hpp>

#include <ocpp/common/utils.hpp>

namespace ocpp {
namespace v2 {

std::set<std::int32_t> parse_monitor_id_list(const std::string& value) {
    std::set<std::int32_t> ids;

    for (const auto& entry : ocpp::split_string(value, ',', true)) {
        if (entry.empty()) {
            continue;
        }
        try {
            std::size_t consumed = 0;
            const auto id = std::stoi(entry, &consumed);
            if (consumed != entry.size()) {
                throw std::invalid_argument("trailing characters");
            }
            ids.insert(id);
        } catch (const std::exception&) {
            EVLOG_warning << "Ignoring '" << entry << "' in a SubscribedMonitors list: not a monitor id";
        }
    }

    return ids;
}

std::string to_monitor_id_list(const std::set<std::int32_t>& ids) {
    std::string value;
    for (const auto id : ids) {
        if (!value.empty()) {
            value += ",";
        }
        value += std::to_string(id);
    }
    return value;
}

bool is_hardwired_notification(const EventData& event) {
    return event.eventNotificationType == EventNotificationEnum::HardWiredNotification or
           event.eventNotificationType == EventNotificationEnum::HardWiredMonitor;
}

} // namespace v2
} // namespace ocpp
