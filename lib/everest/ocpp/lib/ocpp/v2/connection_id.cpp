// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_id.hpp>

#include <algorithm>
#include <stdexcept>

#include <ocpp/common/utils.hpp>

namespace ocpp {
namespace v2 {

ConnectionId::ConnectionId(std::string instance) : m_instance(std::move(instance)) {
}

ConnectionId ConnectionId::primary() {
    return ConnectionId{};
}

ConnectionId ConnectionId::secondary(const std::string& instance) {
    if (!is_valid_instance_name(instance)) {
        throw std::invalid_argument("Invalid OCPP connection instance name: '" + instance + "'");
    }
    return ConnectionId{instance};
}

bool ConnectionId::is_primary() const {
    return !this->m_instance.has_value();
}

const std::optional<std::string>& ConnectionId::instance() const {
    return this->m_instance;
}

std::string ConnectionId::to_string() const {
    if (this->is_primary()) {
        return "primary";
    }
    return "OCPPCommCtrlr[" + this->m_instance.value() + "]";
}

bool ConnectionId::is_valid_instance_name(const std::string& instance) {
    if (instance.empty() || instance.size() > MAX_INSTANCE_LENGTH) {
        return false;
    }
    return std::all_of(instance.begin(), instance.end(), [](const unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == '-';
    });
}

bool operator==(const ConnectionId& lhs, const ConnectionId& rhs) {
    if (lhs.is_primary() or rhs.is_primary()) {
        return lhs.is_primary() == rhs.is_primary();
    }
    return ocpp::iequals(lhs.m_instance.value(), rhs.m_instance.value());
}

bool operator!=(const ConnectionId& lhs, const ConnectionId& rhs) {
    return !(lhs == rhs);
}

bool operator<(const ConnectionId& lhs, const ConnectionId& rhs) {
    // The primary connection sorts first; secondaries then compare by instance name.
    if (lhs.is_primary() or rhs.is_primary()) {
        return lhs.is_primary() and !rhs.is_primary();
    }
    return ocpp::iless(lhs.m_instance.value(), rhs.m_instance.value());
}

} // namespace v2
} // namespace ocpp
