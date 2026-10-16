// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/message_permission.hpp>

#include <everest/logging.hpp>

#include <ocpp/common/utils.hpp>

namespace ocpp {
namespace v2 {

std::set<MessageType> parse_message_permissions(const std::string& csv) {
    std::set<MessageType> permissions;

    for (const auto& name : ocpp::split_string(csv, ',', true)) {
        if (name.empty()) {
            continue;
        }
        try {
            permissions.insert(conversions::string_to_messagetype(name));
        } catch (const StringToEnumException&) {
            EVLOG_warning << "Ignoring unknown OCPP message name '" << name
                          << "' in a connection's allowed message list";
        } catch (const std::exception& e) {
            EVLOG_warning << "Ignoring OCPP message name '" << name
                          << "' in a connection's allowed message list: " << e.what();
        }
    }

    return permissions;
}

} // namespace v2
} // namespace ocpp
