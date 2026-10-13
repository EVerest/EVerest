// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/common/utils.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {
namespace utils {
inline bool identifier_equals(const CiString<50>& lhs, const CiString<50>& rhs) {
    return iequals(lhs.get(), rhs.get());
}

inline bool identifier_equals(const std::optional<CiString<50>>& lhs, const std::optional<CiString<50>>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return not lhs.has_value() or identifier_equals(*lhs, *rhs);
}

inline bool identifier_less(const CiString<50>& lhs, const CiString<50>& rhs) {
    return iless(lhs.get(), rhs.get());
}

inline bool identifier_less(const std::optional<CiString<50>>& lhs, const std::optional<CiString<50>>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return not lhs.has_value();
    }
    return lhs.has_value() and identifier_less(*lhs, *rhs);
}
} // namespace utils

inline bool operator==(const EVSE& lhs, const EVSE& rhs) {
    return lhs.id == rhs.id and lhs.connectorId == rhs.connectorId;
};

inline bool operator<(const EVSE& lhs, const EVSE& rhs) {
    if (lhs.id != rhs.id) {
        return lhs.id < rhs.id;
    }
    return lhs.connectorId < rhs.connectorId;
}

inline bool operator==(const Component& lhs, const Component& rhs) {
    return utils::identifier_equals(lhs.name, rhs.name) and utils::identifier_equals(lhs.instance, rhs.instance) and
           lhs.evse == rhs.evse;
};

inline bool operator<(const Component& lhs, const Component& rhs) {
    if (!utils::identifier_equals(lhs.name, rhs.name)) {
        return utils::identifier_less(lhs.name, rhs.name);
    }
    if (!utils::identifier_equals(lhs.instance, rhs.instance)) {
        return utils::identifier_less(lhs.instance, rhs.instance);
    }
    return lhs.evse < rhs.evse;
};

inline bool operator==(const Variable& lhs, const Variable& rhs) {
    return utils::identifier_equals(lhs.name, rhs.name) and utils::identifier_equals(lhs.instance, rhs.instance);
};

inline bool operator<(const Variable& lhs, const Variable& rhs) {
    if (!utils::identifier_equals(lhs.name, rhs.name)) {
        return utils::identifier_less(lhs.name, rhs.name);
    }
    return utils::identifier_less(lhs.instance, rhs.instance);
};

inline bool operator==(const ComponentVariable& lhs, const ComponentVariable& rhs) {
    return lhs.component == rhs.component and lhs.variable == rhs.variable;
}

inline bool operator<(const ComponentVariable& lhs, const ComponentVariable& rhs) {
    if (lhs.component == rhs.component) {
        return lhs.variable < rhs.variable;
    }
    return lhs.component < rhs.component;
}

inline bool operator==(const SetVariableData& lhs, const SetVariableData& rhs) {
    return lhs.component == rhs.component and lhs.variable == rhs.variable and
           lhs.attributeValue.get() == rhs.attributeValue.get() and lhs.attributeType == rhs.attributeType;
}

inline bool operator<(const SetVariableData& lhs, const SetVariableData& rhs) {
    if (lhs.component == rhs.component) {
        return lhs.variable < rhs.variable;
    }
    return lhs.component < rhs.component;
}

} // namespace v2
} // namespace ocpp
