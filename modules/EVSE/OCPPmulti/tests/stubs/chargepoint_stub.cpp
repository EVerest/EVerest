// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "chargepoint_stub.hpp"
#include "ocpp/v2/messages/DataTransfer.hpp"
#include <everest/logging.hpp>

#include <exception>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <type_traits>

namespace {

inline std::string gen_key(const std::string& component_id, const std::string& variable_id,
                           const ocpp::v2::AttributeEnum& attribute_enum) {
    return component_id + ':' + variable_id;
}

inline std::string gen_key(const std::string& component_id, const std::string& variable_id,
                           const std::string& attribute_enum) {
    return gen_key(component_id, variable_id, ocpp::v2::AttributeEnum::Actual);
}

inline std::string gen_key(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                           const ocpp::v2::AttributeEnum& attribute_enum) {
    return gen_key(static_cast<std::string>(component_id.name), static_cast<std::string>(variable_id.name),
                   attribute_enum);
}

template <typename T>
std::optional<T> store_get(stubs::SimpleStore& store, const ocpp::v2::Component& component_id,
                           const ocpp::v2::Variable& variable_id, const ocpp::v2::AttributeEnum& attribute_enum) {
    std::optional<T> result;
    const auto key = gen_key(component_id, variable_id, attribute_enum);
    auto value = store.get(key);
    std::visit(
        [&result](auto&& arg) {
            using TYPE = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, TYPE>) {
                result = arg;
            }
        },
        value);
    return result;
}

} // namespace

namespace stubs {

void SimpleStore::clear(const std::string& key) {
    m_store.erase(key);
}

bool SimpleStore::exists(const std::string& key) {
    const auto it = m_store.find(key);
    return it != m_store.end();
}

SimpleStore::entry_t SimpleStore::get(const std::string& key) {
    const auto it = m_store.find(key);
    entry_t result;
    if (it != m_store.end()) {
        result = it->second;
    } else {
        EVLOG_warning << "Store key not found: " << key;
    }
    return result;
}

void SimpleStore::put(const std::string& key, entry_t value) {
    m_store[key] = std::move(value);
}

void ChargePointStub::load_store(const std::string_view& filename) {
    using nlohmann::json;
    try {
        std::ifstream file(std::string{filename});
        const auto data = json::parse(file, nullptr, false);
        for (const auto& item : data) {
            const auto key = gen_key(item["component"].get<std::string>(), item["variable"].get<std::string>(),
                                     item["attribute"].get<std::string>());
            const auto& value = item["value"];
            if (value.is_boolean()) {
                simple_store.put(key, item["value"].get<bool>());
            } else if (value.is_number_integer()) {
                simple_store.put(key, item["value"].get<std::int32_t>());
            } else {
                simple_store.put(key, item["value"].get<std::string>());
            }
        }
    } catch (std::exception& ex) {
        EVLOG_error << "Error parsing: " << filename << ": " << ex.what();
    }
}

std::optional<bool> ChargePointStub::get_bool(const ocpp::v2::Component& component_id,
                                              const ocpp::v2::Variable& variable_id,
                                              const ocpp::v2::AttributeEnum& attribute_enum) {
    return store_get<bool>(simple_store, component_id, variable_id, attribute_enum);
}

std::optional<std::int32_t> ChargePointStub::get_int32(const ocpp::v2::Component& component_id,
                                                       const ocpp::v2::Variable& variable_id,
                                                       const ocpp::v2::AttributeEnum& attribute_enum) {
    return store_get<std::int32_t>(simple_store, component_id, variable_id, attribute_enum);
}

std::optional<std::string> ChargePointStub::get_string(const ocpp::v2::Component& component_id,
                                                       const ocpp::v2::Variable& variable_id,
                                                       const ocpp::v2::AttributeEnum& attribute_enum) {
    return store_get<std::string>(simple_store, component_id, variable_id, attribute_enum);
}

} // namespace stubs

namespace {

template <typename T> struct is_vector : std::false_type {};
template <typename... Ts> struct is_vector<std::vector<Ts...>> : std::true_type {};

// template <typename T, is_vector<T>> bool optional_equal(const std::optional<T>& lhs, const std::optional<T>& rhs) {
//     bool result{false};
//     if (lhs.has_value() && rhs.has_value()) {
//         // result = lhs.value() == rhs.value();
//     } else if (!lhs.has_value() && !rhs.has_value()) {
//         // neither have a value
//         result = true;
//     }
//     return result;
// }

template <typename T, typename = void> bool optional_equal(const std::optional<T>& lhs, const std::optional<T>& rhs) {
    bool result{false};
    if (lhs.has_value() && rhs.has_value()) {
        if constexpr (is_vector<T>::value) {
            const auto& lhs_value = lhs.value();
            const auto& rhs_value = rhs.value();
            const auto lhs_size = lhs_value.size();
            const auto rhs_size = rhs_value.size();
            if (lhs_size == rhs_size) {
                result = true;
                for (std::size_t i = 0; i < lhs_size; i++) {
                    if (lhs_value[i] == rhs_value[i]) {
                    } else {
                        result = false;
                        break;
                    }
                }
            }
        } else {
            result = lhs.value() == rhs.value();
        }
    } else if (!lhs.has_value() && !rhs.has_value()) {
        // neither have a value
        result = true;
    }
    return result;
}

} // namespace

namespace ocpp::v2 {

bool operator==(const AdditionalInfo& lhs, const AdditionalInfo& rhs) {
    return (lhs.additionalIdToken == rhs.additionalIdToken) && (lhs.type == rhs.type) &&
           optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const ChangeAvailabilityRequest& lhs, const ChangeAvailabilityRequest& rhs) {
    return (lhs.operationalStatus == rhs.operationalStatus) && optional_equal(lhs.evse, rhs.evse) &&
           optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const DataTransferRequest& lhs, const DataTransferRequest& rhs) {
    return (lhs.vendorId == rhs.vendorId) && optional_equal(lhs.messageId, rhs.messageId) &&
           optional_equal(lhs.data, rhs.data) && optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const GetVariableData& lhs, const GetVariableData& rhs) {
    return (lhs.component == rhs.component) && (lhs.variable == rhs.variable) &&
           optional_equal(lhs.attributeType, rhs.attributeType) && optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const IdToken& lhs, const IdToken& rhs) {
    return (lhs.idToken == rhs.idToken) && (lhs.type == rhs.type) &&
           optional_equal(lhs.additionalInfo, rhs.additionalInfo) && optional_equal(lhs.customData, rhs.customData);
}
} // namespace ocpp::v2

namespace types::ocpp {
std::ostream& operator<<(std::ostream& out, DataTransferStatus value) {
    try {
        out << data_transfer_status_to_string(value);
    } catch (const std::out_of_range&) {
        out << "DataTransferStatus(" << static_cast<int>(value) << ')';
    }
    return out;
}
} // namespace types::ocpp
