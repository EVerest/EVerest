// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "chargepoint_stub.hpp"
#include "ocpp/v2/messages/DataTransfer.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include <everest/logging.hpp>

#include <exception>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <type_traits>

namespace {
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_COMPONENT_NAME = "ISO15118Ctrlr";
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME = "CentralContractValidationAllowed";
constexpr const auto CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_COMPONENT_NAME = "ISO15118Ctrlr";
constexpr const auto CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME = "ContractCertificateInstallationEnabled";
constexpr const auto EV_CONNECTION_TIMEOUT_COMPONENT_NAME = "TxCtrlr";
constexpr const auto EV_CONNECTION_TIMEOUT_VAR_NAME = "EVConnectionTimeOut";
constexpr const auto MASTER_PASS_GROUP_ID_COMPONENT_NAME = "AuthCtrlr";
constexpr const auto MASTER_PASS_GROUP_ID_VAR_NAME = "MasterPassGroupId";
constexpr const auto PNC_ENABLED_COMPONENT_NAME = "ISO15118Ctrlr";
constexpr const auto PNC_ENABLED_VAR_NAME = "PnCEnabled";
constexpr const auto SETPOINT_PRIORITY_COMPONENT_NAME = "SmartChargingCtrlr";
constexpr const auto SETPOINT_PRIORITY_VAR_NAME = "SetpointPriority";
constexpr const auto TX_START_POINT_COMPONENT_NAME = "SmartChargingCtrlr";
constexpr const auto TX_START_POINT_VAR_NAME = "TxStartPoint";
constexpr const auto TX_STOP_POINT_COMPONENT_NAME = "SmartChargingCtrlr";
constexpr const auto TX_STOP_POINT_VAR_NAME = "TxStopPoint";

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
                                              ocpp::v2::AttributeEnum attribute_enum) {
    return store_get<bool>(simple_store, component_id, variable_id, attribute_enum);
}

std::optional<std::int32_t> ChargePointStub::get_int32(const ocpp::v2::Component& component_id,
                                                       const ocpp::v2::Variable& variable_id,
                                                       ocpp::v2::AttributeEnum attribute_enum) {
    return store_get<std::int32_t>(simple_store, component_id, variable_id, attribute_enum);
}

std::optional<std::string> ChargePointStub::get_string(const ocpp::v2::Component& component_id,
                                                       const ocpp::v2::Variable& variable_id,
                                                       ocpp::v2::AttributeEnum attribute_enum) {
    return store_get<std::string>(simple_store, component_id, variable_id, attribute_enum);
}

std::optional<bool> ChargePointStub::get_central_contract_validation_allowed() {
    return get_bool({CENTRAL_CONTRACT_VALIDATION_ALLOWED_COMPONENT_NAME},
                    {CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
}
std::optional<bool> ChargePointStub::get_contract_certificate_installation_enabled() {
    return get_bool({CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_COMPONENT_NAME},
                    {CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
}
std::optional<bool> ChargePointStub::get_pnc_enabled() {
    return get_bool({PNC_ENABLED_COMPONENT_NAME}, {PNC_ENABLED_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::int32_t> ChargePointStub::get_ev_connection_timeout() {
    return get_int32({EV_CONNECTION_TIMEOUT_COMPONENT_NAME}, {EV_CONNECTION_TIMEOUT_VAR_NAME},
                     ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointStub::get_setpoint_priority() {
    return get_string({SETPOINT_PRIORITY_COMPONENT_NAME}, {SETPOINT_PRIORITY_VAR_NAME},
                      ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointStub::get_master_pass_group_id() {
    return get_string({MASTER_PASS_GROUP_ID_COMPONENT_NAME}, {MASTER_PASS_GROUP_ID_VAR_NAME},
                      ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointStub::get_tx_start_point() {
    return get_string({TX_START_POINT_COMPONENT_NAME}, {TX_START_POINT_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointStub::get_tx_stop_point() {
    return get_string({TX_STOP_POINT_COMPONENT_NAME}, {TX_STOP_POINT_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
}

} // namespace stubs

namespace {

template <typename T> struct is_vector : std::false_type {};
template <typename... Ts> struct is_vector<std::vector<Ts...>> : std::true_type {};

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

namespace ocpp {
namespace v2 {

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

bool operator==(const GetLogResponse& lhs, const GetLogResponse& rhs) {
    return (lhs.status == rhs.status) && optional_equal(lhs.statusInfo, rhs.statusInfo) &&
           optional_equal(lhs.filename, rhs.filename) && optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const GetVariableData& lhs, const GetVariableData& rhs) {
    return (lhs.component == rhs.component) && (lhs.variable == rhs.variable) &&
           optional_equal(lhs.attributeType, rhs.attributeType) && optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const IdToken& lhs, const IdToken& rhs) {
    return (lhs.idToken == rhs.idToken) && (lhs.type == rhs.type) &&
           optional_equal(lhs.additionalInfo, rhs.additionalInfo) && optional_equal(lhs.customData, rhs.customData);
}

bool operator==(const MeterValue& lhs, const MeterValue& rhs) {
    return (lhs.sampledValue == rhs.sampledValue) && (lhs.timestamp == rhs.timestamp) &&
           optional_equal(lhs.customData, rhs.customData);
}
bool operator==(const SampledValue& lhs, const SampledValue& rhs) {
    return (lhs.value == rhs.value) && optional_equal(lhs.measurand, rhs.measurand) &&
           optional_equal(lhs.context, rhs.context) && optional_equal(lhs.phase, rhs.phase) &&
           optional_equal(lhs.location, rhs.location) && optional_equal(lhs.signedMeterValue, rhs.signedMeterValue) &&
           optional_equal(lhs.unitOfMeasure, rhs.unitOfMeasure) && optional_equal(lhs.customData, rhs.customData);
}
bool operator==(const SignedMeterValue& lhs, const SignedMeterValue& rhs) {
    return (lhs.signedMeterData == rhs.signedMeterData) && (lhs.encodingMethod == rhs.encodingMethod) &&
           optional_equal(lhs.signingMethod, rhs.signingMethod) && optional_equal(lhs.publicKey, rhs.publicKey) &&
           optional_equal(lhs.customData, rhs.customData);
}
bool operator==(const StatusInfo& lhs, const StatusInfo& rhs) {
    return (lhs.reasonCode == rhs.reasonCode) && optional_equal(lhs.additionalInfo, rhs.additionalInfo) &&
           optional_equal(lhs.customData, rhs.customData);
}
bool operator==(const UnitOfMeasure& lhs, const UnitOfMeasure& rhs) {
    return optional_equal(lhs.unit, rhs.unit) && optional_equal(lhs.multiplier, rhs.multiplier) &&
           optional_equal(lhs.customData, rhs.customData);
}
bool operator==(const UpdateFirmwareResponse& lhs, const UpdateFirmwareResponse& rhs) {
    return (lhs.status == rhs.status) && optional_equal(lhs.statusInfo, rhs.statusInfo) &&
           optional_equal(lhs.customData, rhs.customData);
}
} // namespace v2
bool operator==(const DisplayMessage& lhs, const DisplayMessage& rhs) {
    return optional_equal(lhs.id, rhs.id) && optional_equal(lhs.priority, rhs.priority) &&
           optional_equal(lhs.state, rhs.state) && optional_equal(lhs.timestamp_from, rhs.timestamp_from) &&
           optional_equal(lhs.timestamp_to, rhs.timestamp_to) && optional_equal(lhs.identifier_id, rhs.identifier_id) &&
           optional_equal(lhs.identifier_type, rhs.identifier_type) && (lhs.message == rhs.message) &&
           optional_equal(lhs.qr_code, rhs.qr_code);
}

bool operator==(const DisplayMessageContent& lhs, const DisplayMessageContent& rhs) {
    return (lhs.message == rhs.message) && optional_equal(lhs.language, rhs.language) &&
           optional_equal(lhs.message_format, rhs.message_format);
}
} // namespace ocpp

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
