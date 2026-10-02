// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cmath>
#include <cstdint>
#include <limits>

#include <everest/database/exceptions.hpp>
#include <ocpp/common/constants.hpp>
#include <ocpp/common/utils.hpp>
#include <ocpp/common/websocket/websocket_uri.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/utils.hpp>

namespace ocpp {

namespace v2 {

namespace {
/// \brief For AlignedDataInterval, SampledDataTxUpdatedInterval, SampledDataTxEndedInterval and SecurityProfile, zero
/// is allowed. SecurityProfile had a minLimit of 1 in earlier component schemas, which persists in existing databases
/// and component configs.
bool allow_zero(const Component& component, const Variable& variable) {
    ComponentVariable component_variable;
    component_variable.component = component;
    component_variable.variable = variable;
    return component_variable == ControllerComponentVariables::AlignedDataInterval or
           component_variable == ControllerComponentVariables::SampledDataTxUpdatedInterval or
           component_variable == ControllerComponentVariables::SampledDataTxEndedInterval or
           component_variable == ControllerComponentVariables::SecurityProfile;
}

template <typename MetaData> auto* find_attribute(MetaData& meta_data, const AttributeEnum attribute_enum) {
    const auto it = std::find_if(meta_data.attributes.begin(), meta_data.attributes.end(),
                                 [attribute_enum](const VariableAttribute& attribute) {
                                     return attribute.type.value_or(AttributeEnum::Actual) == attribute_enum;
                                 });
    return it == meta_data.attributes.end() ? nullptr : &*it;
}

bool is_persistent(const VariableAttribute& attribute) {
    return attribute.persistent.value_or(true);
}

bool is_held_by_storage(const ComposedVariable& composed) {
    return composed.registration == nullptr and composed.meta_data->source.has_value();
}

/// \brief A registered attribute is the Actual attribute, not persistent and not constant unless it says otherwise
VariableAttribute normalize_registered_attribute(VariableAttribute attribute) {
    attribute.type = attribute.type.value_or(AttributeEnum::Actual);
    attribute.persistent = attribute.persistent.value_or(false);
    attribute.constant = attribute.constant.value_or(false);
    return attribute;
}

bool allow_set_read_only_value(const Component& component, const Variable& variable,
                               const AttributeEnum attribute_enum) {
    if (attribute_enum != AttributeEnum::Actual) {
        return false;
    }

    return component == ControllerComponents::AuthCacheCtrlr or component == ControllerComponents::LocalAuthListCtrlr or
           component == ControllerComponents::OCPPCommCtrlr or component == ControllerComponents::SecurityCtrlr or
           variable == EvseComponentVariables::AvailabilityState or variable == EvseComponentVariables::Power or
           variable == ConnectorComponentVariables::AvailabilityState;
}

} // namespace

bool DeviceModel::component_criteria_match(const Component& component,
                                           const std::vector<ComponentCriterionEnum>& component_criteria) {
    if (component_criteria.empty()) {
        return false;
    }
    for (const auto& criteria : component_criteria) {
        Variable variable;
        variable.name = conversions::component_criterion_enum_to_string(criteria);

        const auto response = this->request_value<bool>(component, variable, AttributeEnum::Actual);
        auto value = response.value;
        if (response.status == GetVariableStatusEnum::Accepted and value.has_value() and value.value()) {
            return true;
        }
        // also send true if the component crietria isn't part of the component except "problem"
        if (!value.has_value() and (variable.name != "Problem")) {
            return true;
        }
    }
    return false;
}

namespace {
/// @brief Iterates over the given \p component_variables and filters them according to the requirement conditions.
/// @param component_variables
/// @param component_ current component
/// @param variable_ current variable
/// @return true if the component is found according to any of the requirement conditions.
bool component_variables_match(const std::vector<ComponentVariable>& component_variables,
                               const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {

    return std::find_if(
               component_variables.begin(), component_variables.end(), [component, variable](ComponentVariable v) {
                   return (component == v.component and !v.variable.has_value()) or // if component has no variable
                          (component == v.component and v.variable.has_value() and
                           variable == v.variable.value()) or // if component has variables
                          (component == v.component and v.variable.has_value() and
                           !v.variable.value().instance.has_value() and
                           variable.name == v.variable.value().name) or // if component has no variable instances
                          (!v.component.evse.has_value() and (component.name == v.component.name) and
                           (component.instance == v.component.instance) and (variable == v.variable)); // B08.FR.23
               }) != component_variables.end();
}

std::string format_missing_required_variable(const RequiredComponentVariable& required_variable,
                                             const std::string& reason) {
    std::stringstream ss;
    ss << required_variable.component.name << "/"
       << (required_variable.variable.has_value() ? required_variable.variable.value().name : "<unnamed>") << ": "
       << reason;
    if (required_variable.component.name == ControllerComponents::OCPP16LegacyCtrlr.name) {
        ss << " (the OCPP16LegacyCtrlr component is required for every OCPP version so the device model supports "
              "switching between OCPP 1.6 and 2.x; add standardized/OCPP16LegacyCtrlr.json to the device model "
              "config directory or initialize the database with the built-in OCPP16LegacyCtrlr fallback enabled)";
    }
    return ss.str();
}
} // namespace

void DeviceModel::check_variable_has_value(const ComponentVariable& component_variable, const AttributeEnum attribute) {
    std::string value;
    if (not component_variable.variable.has_value()) {
        throw DeviceModelError("Attempted to check if a variale of component " +
                               component_variable.component.name.get() +
                               " has a value but did not provide the variable");
    }
    const auto& variable = component_variable.variable.value();
    const auto response = this->request_value_internal(component_variable.component, variable, attribute, value, true,
                                                       UnsetValue::NotSupported);

    if (response != GetVariableStatusEnum::Accepted) {
        throw DeviceModelError("Required variable " + variable.name.get() + " of component " +
                               component_variable.component.name.get() + " does not have a value in the device model");
    }
}

void DeviceModel::check_required_variable(const RequiredComponentVariable& required_variable,
                                          const std::vector<OcppProtocolVersion>& supported_versions) {
    if (supported_versions.empty()) {
        throw DeviceModelError("Could not find supported ocpp versions in the InternalCtrlr.");
    }

    bool required = false;
    for (auto& supported_version : supported_versions) {
        if (required_variable.required_for.count(supported_version) > 0) {
            required = true;
        }
    }

    // For the current supported ocpp protocol versions, this variable is not required. So skip further checks.
    if (!required) {
        return;
    }

    if (!required_variable.variable.has_value()) {
        throw DeviceModelError("Required variable does not exist.");
    }

    check_variable_has_value(required_variable);
}

void DeviceModel::check_required_variables() {
    const auto supported_versions = utils::get_ocpp_protocol_versions(
        this->get_value<std::string>(ControllerComponentVariables::SupportedOcppVersions));

    if (supported_versions.empty()) {
        throw DeviceModelError("Could not find supported OCPP versions in the InternalCtrlr.");
    }

    std::vector<std::string> missing_var_errors;

    // Check global required variables
    for (const auto& required_variable : required_variables) {
        try {
            check_required_variable(required_variable, supported_versions);
        } catch (const std::exception& e) {
            missing_var_errors.push_back(format_missing_required_variable(required_variable, e.what()));
        }
    }

    // Check controller-specific required variables (if controller is available)
    for (const auto& available_required : required_component_available_variables) {
        const std::optional<bool> available = this->get_optional_value<bool>(available_required.first);
        if (!available.value_or(false)) {
            continue;
        }

        for (const auto& required_variable : available_required.second) {
            try {
                check_required_variable(required_variable, supported_versions);
            } catch (const std::exception& e) {
                missing_var_errors.push_back(format_missing_required_variable(required_variable, e.what()));
            }
        }
    }

    // Throw collective error if any variables are missing
    if (!missing_var_errors.empty()) {
        std::ostringstream oss;
        oss << "Missing required variables:\n";
        for (const auto& err : missing_var_errors) {
            oss << " - " << err << "\n";
        }
        throw DeviceModelError(oss.str());
    }
}

namespace {
/// \brief Checks that \p value has the data type of the variable and is one of its allowed values
bool fits_data_type(const VariableCharacteristics& characteristics, const std::string& value) {
    switch (characteristics.dataType) {
    case DataEnum::string:
        return true;
    case DataEnum::decimal:
        return is_decimal_number(value);
    case DataEnum::integer:
        return is_integer(value);
    case DataEnum::dateTime:
        return is_rfc3339_datetime(value);
    case DataEnum::boolean:
        return (value == "true" or value == "false");
    case DataEnum::OptionList: {
        // OptionList: The (Actual) Variable value must be a single value from the reported (CSV) enumeration list.
        if (!characteristics.valuesList.has_value()) {
            return true;
        }
        const auto values_list = ocpp::split_string(characteristics.valuesList.value().get(), ',');
        return std::find(values_list.begin(), values_list.end(), value) != values_list.end();
    }
    case DataEnum::SequenceList:
    case DataEnum::MemberList:
        // MemberList: The (Actual) Variable value may be an (unordered) (sub-)set of the reported (CSV) valid
        // values list. SequenceList: The (Actual) Variable value may be an ordered (priority, etc) (sub-)set of the
        // reported (CSV) valid values.
        {
            if (!characteristics.valuesList.has_value()) {
                return true;
            }
            const auto values_list = ocpp::split_string(characteristics.valuesList.value().get(), ',');
            const auto value_csv = ocpp::split_string(value, ',');
            for (const auto& v : value_csv) {
                if (std::find(values_list.begin(), values_list.end(), v) == values_list.end()) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

/// \brief Checks \p value, which fits the data type of the variable, against its minLimit and maxLimit
bool within_limits(const VariableCharacteristics& characteristics, const std::string& value, bool allow_zero) {
    switch (characteristics.dataType) {
    case DataEnum::string:
        if (characteristics.minLimit.has_value() and
            value.size() < convert_to_positive_size_t(characteristics.minLimit.value())) {
            return false;
        }
        if (characteristics.maxLimit.has_value() and
            value.size() > convert_to_positive_size_t(characteristics.maxLimit.value())) {
            return false;
        }
        return true;
    case DataEnum::decimal: {
        const float f = std::stof(value);

        if (allow_zero and f == 0) {
            return true;
        }
        if (characteristics.minLimit.has_value() and f < characteristics.minLimit.value()) {
            return false;
        }
        if (characteristics.maxLimit.has_value() and f > characteristics.maxLimit.value()) {
            return false;
        }
        return true;
    }
    case DataEnum::integer: {
        const int i = std::stoi(value);

        if (allow_zero and i == 0) {
            return true;
        }
        if (characteristics.minLimit.has_value() and i < convert_to_positive_size_t(characteristics.minLimit.value())) {
            return false;
        }
        if (characteristics.maxLimit.has_value() and i > convert_to_positive_size_t(characteristics.maxLimit.value())) {
            return false;
        }
        return true;
    }
    case DataEnum::dateTime:
    case DataEnum::boolean:
    case DataEnum::OptionList:
    case DataEnum::SequenceList:
    case DataEnum::MemberList:
        return true;
    }
    return true;
}

bool validate_value(const VariableCharacteristics& characteristics, const std::string& value, bool allow_zero) {
    return fits_data_type(characteristics, value) and within_limits(characteristics, value, allow_zero);
}

/// \brief Converts a monitor \p value to a string suitable for validate_value() given the variable's \p data_type.
/// std::to_string(float) always renders six decimals (e.g. "950.000000"), which would fail integer validation even
/// for integral values, so integral values for integer-typed variables are rendered without decimals.
std::string monitor_value_to_string(const float value, const DataEnum data_type) {
    if (data_type == DataEnum::integer and std::rint(value) == value and
        std::abs(value) < static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
        return std::to_string(static_cast<std::int32_t>(value));
    }
    return std::to_string(value);
}

bool include_in_summary_inventory(const ComponentVariable& cv, const VariableAttribute& attribute) {
    if (cv == ControllerComponentVariables::ChargingStationAvailabilityState) {
        return true;
    }
    if (cv.component.name == "EVSE" and cv.variable == EvseComponentVariables::AvailabilityState) {
        return true;
    }
    if (cv.component.name == "Connector" and cv.variable == ConnectorComponentVariables::AvailabilityState) {
        return true;
    }
    if ((cv.variable == StandardizedVariables::Fallback or cv.variable == StandardizedVariables::Overload or
         cv.variable == StandardizedVariables::Problem or cv.variable == StandardizedVariables::Tripped) and
        attribute.value.value_or("") == "true") {
        return true;
    }
    return false;
}
} // namespace

GetVariableStatusEnum DeviceModel::get_variable(const Component& component_id, const Variable& variable_id,
                                                const AttributeEnum& attribute_enum, std::string& value,
                                                bool allow_write_only, UnsetValue unset_value) const {
    return this->request_value_internal(component_id, variable_id, attribute_enum, value, allow_write_only,
                                        unset_value);
}

GetVariableStatusEnum DeviceModel::request_value_internal(const Component& component_id, const Variable& variable_id,
                                                          const AttributeEnum& attribute_enum, std::string& value,
                                                          bool allow_write_only, UnsetValue unset_value) const {
    // Choke point for all value reads (get_variable and the get_*_value/request_value templates).
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    const auto component_it = this->model.find(component_id);
    if (component_it == this->model.end()) {
        EVLOG_debug << "unknown component in " << component_id.name << "." << variable_id.name;
        return GetVariableStatusEnum::UnknownComponent;
    }

    const auto& component = component_it->second;
    const auto& variable_it = component.find(variable_id);

    if (variable_it == component.end()) {
        EVLOG_debug << "unknown variable in " << component_id.name << "." << variable_id.name;
        return GetVariableStatusEnum::UnknownVariable;
    }

    const auto attribute = this->read_attribute(component_id, variable_id, variable_it->second, attribute_enum);

    if (not attribute.has_value()) {
        return GetVariableStatusEnum::NotSupportedAttributeType;
    }

    // only internal functions can access WriteOnly variables
    if (!allow_write_only and attribute->mutability == MutabilityEnum::WriteOnly) {
        return GetVariableStatusEnum::Rejected;
    }

    if (not attribute->value.has_value()) {
        if (unset_value == UnsetValue::NotSupported) {
            return GetVariableStatusEnum::NotSupportedAttributeType;
        }
        value.clear();
        return GetVariableStatusEnum::Accepted;
    }

    value = attribute->value->get();
    return GetVariableStatusEnum::Accepted;
}

std::optional<VariableAttribute> DeviceModel::read_attribute(const Component& component, const Variable& variable,
                                                             const ComposedVariable& composed,
                                                             const AttributeEnum& attribute_enum) const {
    if (is_held_by_storage(composed)) {
        return this->device_model->get_variable_attribute(component, variable, attribute_enum);
    }
    const auto* attribute = find_attribute(*composed.meta_data, attribute_enum);
    if (attribute == nullptr) {
        return std::nullopt;
    }
    return *attribute;
}

std::vector<VariableAttribute> DeviceModel::read_attributes(const Component& component, const Variable& variable,
                                                            const ComposedVariable& composed) const {
    if (is_held_by_storage(composed)) {
        return this->device_model->get_variable_attributes(component, variable);
    }
    return composed.meta_data->attributes;
}

std::optional<MutabilityEnum> DeviceModel::get_mutability(const Component& component, const Variable& variable,
                                                          const AttributeEnum& attribute_enum) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    const auto* composed = this->find_variable(component, variable);
    if (composed == nullptr) {
        return std::nullopt;
    }
    const auto* attribute = find_attribute(*composed->meta_data, attribute_enum);
    if (attribute == nullptr) {
        return std::nullopt;
    }

    return attribute->mutability;
}

SetVariableStatusEnum DeviceModel::set_value(const Component& component, const Variable& variable,
                                             const AttributeEnum& attribute_enum, const std::string& value,
                                             const std::string& source, bool allow_read_only) {
    // The local channel, which may write ReadOnly attributes, writes the component config only: a registered
    // variable is changed by its owner, through notify_value_changed or its provider, or by the CSMS
    return this->set_value(component, variable, attribute_enum, value, source, allow_read_only,
                           /*allow_registered=*/not allow_read_only);
}

SetVariableStatusEnum DeviceModel::set_value(const Component& component, const Variable& variable,
                                             const AttributeEnum& attribute_enum, const std::string& value,
                                             const std::string& source, const bool allow_read_only,
                                             const bool allow_registered) {
    std::unique_lock<std::recursive_mutex> lock(this->mutex);

    const auto component_it = this->model.find(component);
    if (component_it == this->model.end()) {
        return SetVariableStatusEnum::UnknownComponent;
    }

    const auto variable_it = component_it->second.find(variable);
    if (variable_it == component_it->second.end()) {
        return SetVariableStatusEnum::UnknownVariable;
    }
    const auto& composed = variable_it->second;
    const auto& meta_data = *composed.meta_data;

    try {
        if (!validate_value(meta_data.characteristics, value, allow_zero(component, variable))) {
            return SetVariableStatusEnum::Rejected;
        }
    } catch (const std::exception& e) {
        EVLOG_warning << "Could not validate value: " << value << " for component: " << component
                      << " and variable: " << variable;
        return SetVariableStatusEnum::Rejected;
    }

    const auto attribute = this->read_attribute(component, variable, composed, attribute_enum);

    if (not attribute.has_value()) {
        return SetVariableStatusEnum::NotSupportedAttributeType;
    }

    // If allow_read_only is false, don't allow read only
    if (!attribute->mutability.has_value() or
        ((attribute->mutability.value() == MutabilityEnum::ReadOnly) and !allow_read_only)) {
        return SetVariableStatusEnum::Rejected;
    }

    if (not allow_registered and composed.registration != nullptr) {
        return SetVariableStatusEnum::Rejected;
    }

    return this->write_value(lock, component, variable, composed, attribute_enum, value, source);
};

SetVariableStatusEnum DeviceModel::write_value(std::unique_lock<std::recursive_mutex>& lock, const Component& component,
                                               const Variable& variable, const ComposedVariable& composed,
                                               const AttributeEnum& attribute_enum, const std::string& value,
                                               const std::string& source) {
    const auto* registration = composed.registration;
    if (registration == nullptr or registration->provider == nullptr) {
        return this->apply_value(lock, component, variable, composed, attribute_enum, value, source, true);
    }
    auto& provider = *registration->provider;
    const auto owner = registration->source;
    auto call = this->provider_calls.begin(owner, component, variable);
    lock.unlock();
    return this->set_value_through_provider(provider, owner, std::move(call), component, variable, attribute_enum,
                                            value, source);
}

DeviceModel::AppliedValue DeviceModel::store_value(const Component& component, const Variable& variable,
                                                   const ComposedVariable& composed,
                                                   const AttributeEnum& attribute_enum, const std::string& value,
                                                   const std::string& source, const bool write_to_storage) {
    auto& meta_data = *composed.meta_data;
    // A variable held by the storage may have no attribute in memory; the storage copy is authoritative for it
    auto attribute = this->read_attribute(component, variable, composed, attribute_enum);
    if (not attribute.has_value()) {
        return {SetVariableStatusEnum::NotSupportedAttributeType, std::nullopt};
    }
    std::string value_previous = attribute->value.has_value() ? attribute->value->get() : std::string{};

    const auto* registration = composed.registration;
    if (write_to_storage and (is_held_by_storage(composed) or is_persistent(*attribute))) {
        if (registration == nullptr) {
            const auto result =
                this->device_model->set_variable_attribute_value(component, variable, attribute_enum, value, source);
            if (result != SetVariableStatusEnum::Accepted) {
                return {result, std::nullopt};
            }
        } else if (registration->provider == nullptr) {
            const auto result = this->device_model->set_stored_value(
                StoredVariableValue{component, variable, attribute_enum, value, source, registration->source});
            if (result != SetVariableStatusEnum::Accepted) {
                return {result, std::nullopt};
            }
        }
    }

    attribute->value = value;
    if (auto* in_memory = find_attribute(meta_data, attribute_enum); in_memory != nullptr) {
        in_memory->value = value;
    }

    // Only trigger for actual values
    if (attribute_enum != AttributeEnum::Actual or value_previous == value or this->variable_listener.empty()) {
        return {SetVariableStatusEnum::Accepted, std::nullopt};
    }
    return {SetVariableStatusEnum::Accepted,
            ValueChange{this->variable_listener, this->monitors.of(component, variable), meta_data.characteristics,
                        attribute.value(), std::move(value_previous)}};
}

void DeviceModel::notify(const ValueChange& change, const Component& component, const Variable& variable,
                         const std::string& value) {
    for (const auto& listener : change.listeners) {
        listener(change.monitors, component, variable, change.characteristics, change.attribute, change.value_previous,
                 value);
    }
}

SetVariableStatusEnum DeviceModel::apply_value(std::unique_lock<std::recursive_mutex>& lock, const Component& component,
                                               const Variable& variable, const ComposedVariable& composed,
                                               const AttributeEnum& attribute_enum, const std::string& value,
                                               const std::string& source, const bool write_to_storage) {
    const auto applied =
        this->store_value(component, variable, composed, attribute_enum, value, source, write_to_storage);
    // Listeners run without the lock: a listener taking its own lock would otherwise invert lock order against a
    // thread calling register_variable_listener while holding that lock.
    lock.unlock();
    if (applied.change.has_value()) {
        notify(applied.change.value(), component, variable, value);
    }
    return applied.status;
}

SetVariableStatusEnum DeviceModel::set_value_through_provider(VariableValueProvider& provider, const std::string& owner,
                                                              ProviderCalls::Call call, const Component& component,
                                                              const Variable& variable,
                                                              const AttributeEnum& attribute_enum,
                                                              const std::string& value, const std::string& source) {
    call.lock_variable();

    auto result = SetVariableStatusEnum::Rejected;
    try {
        result = provider.set_value(component, variable, attribute_enum, value, source);
    } catch (const std::exception& e) {
        EVLOG_error << "Provider of " << component << "." << variable << " failed to set the value: " << e.what();
    }
    if (result != SetVariableStatusEnum::Accepted and result != SetVariableStatusEnum::RebootRequired) {
        return result;
    }

    std::optional<ValueChange> change;
    {
        std::lock_guard<std::recursive_mutex> lock(this->mutex);
        const auto* composed = this->find_variable(component, variable);
        // The source may have unregistered the variable or replaced its provider while the provider was called
        const bool still_provided = composed != nullptr and composed->registration != nullptr and
                                    composed->registration->source == owner and
                                    composed->registration->provider == &provider;
        if (!still_provided) {
            EVLOG_debug << "Not applying the value of " << component << "." << variable
                        << ": its provider no longer provides it";
            return result;
        }
        change = this->store_value(component, variable, *composed, attribute_enum, value, source, false).change;
    }
    // A listener may write the variable again or unregister its source, which waits for this call
    call.finish();
    if (change.has_value()) {
        notify(change.value(), component, variable, value);
    }
    return result;
}

DeviceModel::DeviceModel(std::unique_ptr<DeviceModelStorageInterface> device_model_storage_interface) :
    device_model{std::move(device_model_storage_interface)},
    component_config{this->device_model->get_device_model()},
    monitors{*this->device_model} {
    this->monitors.load();
    this->recompose();
}

DeviceModel::~DeviceModel() = default;

void DeviceModel::recompose() {
    auto composition = compose_device_model(this->component_config, this->registrations);
    for (const auto& [component, variable] : composition.hidden) {
        const auto component_it = this->model.find(component);
        if (component_it == this->model.end() or component_it->second.count(variable) == 0) {
            continue;
        }
        EVLOG_warning << "The component config defines " << component << "." << variable
                      << ", but no EVSE or connector of that id exists: the variable is hidden";
    }
    this->model = std::move(composition.model);
}

ComposedVariable* DeviceModel::find_variable(const Component& component, const Variable& variable) {
    const auto component_it = this->model.find(component);
    if (component_it == this->model.end()) {
        return nullptr;
    }
    const auto variable_it = component_it->second.find(variable);
    return variable_it == component_it->second.end() ? nullptr : &variable_it->second;
}

const ComposedVariable* DeviceModel::find_variable(const Component& component, const Variable& variable) const {
    return const_cast<DeviceModel*>(this)->find_variable(component, variable);
}

bool DeviceModel::create_network_configuration_slot_from_default_schema(std::int32_t new_slot) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    if (this->device_model == nullptr) {
        return false;
    }
    if (!this->device_model->create_network_configuration_slot_from_default_schema(new_slot)) {
        return false;
    }
    try {
        // Only the created component is taken over: the components already in memory keep their values, which the
        // storage does not hold for attributes that are not persistent.
        for (auto& [component, variables] : this->device_model->get_device_model()) {
            this->component_config.try_emplace(component, std::move(variables));
        }
        this->recompose();
    } catch (const std::exception& e) {
        EVLOG_error << "DeviceModel::create_network_configuration_slot_from_default_schema: storage "
                       "committed slot "
                    << new_slot << " but in-memory device-model reload failed (" << e.what()
                    << "); restart required to pick up the new component";
        throw;
    }
    return true;
}

SetVariableStatusEnum DeviceModel::set_read_only_value(const Component& component, const Variable& variable,
                                                       const AttributeEnum& attribute_enum, const std::string& value,
                                                       const std::string& source) {
    std::optional<std::string> owner;
    {
        std::lock_guard<std::recursive_mutex> lock(this->mutex);
        owner = this->runtime_owner(component, variable);
        if (owner.has_value() and owner.value() != VARIABLE_SOURCE_OCPP_RUNTIME) {
            EVLOG_debug << "Not setting " << component << "." << variable << ": it is owned by source "
                        << owner.value();
            return SetVariableStatusEnum::Rejected;
        }
    }
    // The lock is released before set_value: holding the recursive mutex here would keep it owned while set_value
    // invokes the listeners.
    if (allow_set_read_only_value(component, variable, attribute_enum)) {
        return this->set_value(component, variable, attribute_enum, value, source, /*allow_read_only=*/true,
                               /*allow_registered=*/owner.has_value());
    }
    throw std::invalid_argument("Not allowed to set read only value for component " + component.name.get() +
                                " and variable " + variable.name.get());
}

SetVariableStatusEnum DeviceModel::clear_value(const Component& component, const Variable& variable,
                                               const AttributeEnum& attribute_enum, const std::string& source) {
    std::unique_lock<std::recursive_mutex> lock(this->mutex);
    const auto component_it = this->model.find(component);
    if (component_it == this->model.end()) {
        return SetVariableStatusEnum::UnknownComponent;
    }
    const auto variable_it = component_it->second.find(variable);
    if (variable_it == component_it->second.end()) {
        return SetVariableStatusEnum::UnknownVariable;
    }
    if (not this->read_attribute(component, variable, variable_it->second, attribute_enum).has_value()) {
        return SetVariableStatusEnum::NotSupportedAttributeType;
    }

    // Bypass validate_value: empty-string clears can violate length/range characteristics
    // by design (the cleared row is the sentinel for "no value").
    return this->write_value(lock, component, variable, variable_it->second, attribute_enum, "", source);
}

std::optional<std::string> DeviceModel::runtime_owner(const Component& component, const Variable& variable) const {
    const auto* composed = this->find_variable(component, variable);
    if (composed == nullptr or composed->registration == nullptr) {
        return std::nullopt;
    }
    return composed->registration->source;
}

ComponentRegistrationResult DeviceModel::register_components(const std::string& source,
                                                             const std::vector<RegisteredComponent>& components,
                                                             VariableValueProvider* provider) {
    std::unique_lock<std::recursive_mutex> lock(this->mutex);
    ComponentRegistrationResult result;
    const auto previous_it = this->registrations.find(source);
    const bool provider_replaced = previous_it != this->registrations.end() and
                                   previous_it->second.provider != nullptr and previous_it->second.provider != provider;

    Registration registration{source, provider, {}};
    for (const auto& registered_component : components) {
        const auto& component = registered_component.component;
        for (const auto& registered_variable : registered_component.variables) {
            const auto& variable = registered_variable.variable;
            if (registered_variable.attributes.empty()) {
                EVLOG_warning << "Source " << source << " registered " << component << "." << variable
                              << " without attributes: the variable is ignored";
                result.rejected.push_back({component, variable, RegistrationRejectionReason::NoAttributes});
                continue;
            }
            if (const auto owner = this->runtime_owner(component, variable); owner.has_value() and owner != source) {
                EVLOG_warning << "Source " << source << " registered " << component << "." << variable
                              << ", which source " << owner.value() << " registered already: the variable is ignored";
                result.rejected.push_back({component, variable, RegistrationRejectionReason::OwnedByOtherSource});
                continue;
            }
            std::vector<VariableAttribute> attributes;
            attributes.reserve(registered_variable.attributes.size());
            for (const auto& attribute : registered_variable.attributes) {
                attributes.push_back(normalize_registered_attribute(attribute));
            }
            registration.variables[component][variable] =
                this->build_registered_variable(source, component, registered_variable, std::move(attributes));
        }
    }

    this->registrations.insert_or_assign(source, std::move(registration));
    this->recompose();

    lock.unlock();
    if (provider_replaced) {
        this->provider_calls.wait_idle(source);
    }
    return result;
}

VariableMetaData DeviceModel::build_registered_variable(const std::string& source, const Component& component,
                                                        const RegisteredVariable& registered_variable,
                                                        std::vector<VariableAttribute> attributes) {
    const auto& variable = registered_variable.variable;
    VariableMetaData meta_data;
    meta_data.characteristics = registered_variable.characteristics;
    meta_data.source = source;
    meta_data.attributes = std::move(attributes);

    const auto config_it = this->component_config.find(component);
    if (config_it != this->component_config.end() and config_it->second.count(variable) != 0) {
        EVLOG_warning << "The component config defines " << component << "." << variable << ", which source " << source
                      << " registers: the registration overrides the component config";
    }

    return meta_data;
}

void DeviceModel::unregister_components(const std::string& source) {
    {
        std::lock_guard<std::recursive_mutex> lock(this->mutex);
        this->registrations.erase(source);
        this->recompose();
    }
    this->provider_calls.wait_idle(source);
}

SetVariableStatusEnum DeviceModel::notify_value_changed(const Component& component, const Variable& variable,
                                                        AttributeEnum attribute, const std::string& value,
                                                        const std::string& source) {
    std::unique_lock<std::recursive_mutex> lock(this->mutex);
    const auto* composed = this->find_variable(component, variable);
    if (composed == nullptr or composed->registration == nullptr or composed->registration->source != source) {
        EVLOG_debug << "Source " << source << " reported a value of " << component << "." << variable
                    << ", which it did not register";
        return SetVariableStatusEnum::Rejected;
    }
    if (!fits_data_type(composed->meta_data->characteristics, value)) {
        EVLOG_warning << "Source " << source << " reported the value \"" << value << "\" of " << component << "."
                      << variable << ", which does not fit its data type or allowed values";
        return SetVariableStatusEnum::Rejected;
    }
    return this->apply_value(lock, component, variable, *composed, attribute, value, source, true);
}

std::optional<VariableMetaData> DeviceModel::get_variable_meta_data(const Component& component,
                                                                    const Variable& variable) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    const auto* composed = this->find_variable(component, variable);
    if (composed == nullptr) {
        return std::nullopt;
    }
    auto meta_data = *composed->meta_data;
    meta_data.monitors = this->monitors.of(component, variable);
    return meta_data;
}

std::vector<ReportData> DeviceModel::get_base_report_data(const ReportBaseEnum& report_base) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    std::vector<ReportData> report_data_vec;

    for (const auto& [component, variable_map] : this->model) {
        for (const auto& [variable, composed] : variable_map) {
            const auto& variable_meta_data = *composed.meta_data;

            ReportData report_data;
            report_data.component = component;
            report_data.variable = variable;

            ComponentVariable cv;
            cv.component = component;
            cv.variable = variable;

            // iterate over possibly (Actual, Target, MinSet, MaxSet)
            for (const auto& variable_attribute : this->read_attributes(component, variable, composed)) {
                if (report_base == ReportBaseEnum::FullInventory or
                    (report_base == ReportBaseEnum::ConfigurationInventory and
                     (variable_attribute.mutability == MutabilityEnum::ReadWrite or
                      variable_attribute.mutability == MutabilityEnum::WriteOnly))) {
                    report_data.variableAttribute.push_back(variable_attribute);
                    // scrub WriteOnly value from report
                    if (variable_attribute.mutability == MutabilityEnum::WriteOnly) {
                        report_data.variableAttribute.back().value.reset();
                    }
                    report_data.variableCharacteristics = variable_meta_data.characteristics;
                } else if (report_base == ReportBaseEnum::SummaryInventory) {
                    if (include_in_summary_inventory(cv, variable_attribute)) {
                        report_data.variableAttribute.push_back(variable_attribute);
                    }
                }
            }
            if (!report_data.variableAttribute.empty()) {
                report_data_vec.push_back(report_data);
            }
        }
    }
    return report_data_vec;
}

std::vector<ReportData>
DeviceModel::get_custom_report_data(const std::optional<std::vector<ComponentVariable>>& component_variables,
                                    const std::optional<std::vector<ComponentCriterionEnum>>& component_criteria) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    std::vector<ReportData> report_data_vec;

    for (const auto& [component, variable_map] : this->model) {
        if (!component_criteria.has_value() or component_criteria_match(component, component_criteria.value())) {

            for (const auto& [variable, composed] : variable_map) {
                const auto& variable_meta_data = *composed.meta_data;
                if (!component_variables.has_value() or
                    component_variables_match(component_variables.value(), component, variable)) {
                    ReportData report_data;
                    report_data.component = component;
                    report_data.variable = variable;

                    for (const auto& variable_attribute : this->read_attributes(component, variable, composed)) {
                        report_data.variableAttribute.push_back(variable_attribute);
                        report_data.variableCharacteristics = variable_meta_data.characteristics;
                    }

                    if (!report_data.variableAttribute.empty()) {
                        report_data_vec.push_back(report_data);
                    }
                }
            }
        }
    }
    return report_data_vec;
}

void DeviceModel::check_integrity(const std::map<std::int32_t, std::int32_t>& evse_connector_structure) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    EVLOG_debug << "Checking integrity of device model in storage";
    try {
        this->check_required_variables();
        this->device_model->check_integrity();

        std::int32_t nr_evse_components = 0;
        std::map<std::int32_t, std::int32_t> evse_id_nr_connector_components;

        for (const auto& [component, variable_map] : this->model) {
            if (component.name == "EVSE") {
                nr_evse_components++;
            } else if (component.name == "Connector") {
                if (not component.evse.has_value()) {
                    throw DeviceModelError("EVSE component did not contain evse member");
                }
                const auto& evse = component.evse.value();
                if (evse_id_nr_connector_components.count(evse.id) != 0) {
                    evse_id_nr_connector_components[evse.id] += 1;
                } else {
                    evse_id_nr_connector_components[evse.id] = 1;
                }
            }
        }

        // check if number of EVSE in the device model matches the configured number
        if (nr_evse_components != evse_connector_structure.size()) {
            throw DeviceModelError("Number of EVSE configured in device model is incompatible with number of "
                                   "configured EVSEs of the ChargePoint");
        }

        for (const auto [evse_id, nr_of_connectors] : evse_connector_structure) {
            // check if number of Cpnnectors for this EVSE in the device model matches the configured number
            if (evse_id_nr_connector_components[evse_id] != nr_of_connectors) {
                throw DeviceModelError("Number of Connectors configured in device model is incompatible with number "
                                       "of configured Connectors of the ChargePoint");
            }

            // check if all relevant EVSE and Connector components can be found
            EVSE evse;
            evse.id = evse_id;
            Component evse_component;
            evse_component.name = "EVSE";
            evse_component.evse = evse;
            if (this->model.count(evse_component) == 0) {
                throw DeviceModelError("Could not find required EVSE component in device model");
            }

            for (const auto& required_variable : required_evse_variables) {
                const auto& variable = EvseComponentVariables::get_component_variable(evse_id, required_variable);
                check_variable_has_value(variable);
            }

            const auto& variable =
                EvseComponentVariables::get_component_variable(evse_id, EvseComponentVariables::Power);
            const auto* power = this->find_variable(evse_component, EvseComponentVariables::Power);
            if (power == nullptr) {
                throw DeviceModelError("Could not find required 'Power' variable in EVSE component in device model");
            }

            if (!power->meta_data->characteristics.maxLimit.has_value()) {
                throw DeviceModelError("maxLimit of 'Power' not set");
            }

            Component v2x_component;
            v2x_component.name = "V2XChargingCtrlr";
            v2x_component.evse = evse;
            if ((this->model.count(v2x_component) != 0) and
                std::any_of(evse_connector_structure.begin(), evse_connector_structure.end(),
                            [this](const auto& entry) {
                                const auto& [evse, connectors] = entry;
                                return get_optional_value<bool>(V2xComponentVariables::get_component_variable(
                                                                    evse, V2xComponentVariables::Available))
                                    .value_or(false);
                            })) {
                for (const auto& required_variable : required_v2x_variables) {
                    const auto& variable = V2xComponentVariables::get_component_variable(evse_id, required_variable);
                    check_variable_has_value(variable);
                }
            }

            for (size_t connector_id = 1; connector_id <= nr_of_connectors; connector_id++) {
                evse_component.name = "Connector";
                evse_component.evse.value().connectorId = connector_id;
                if (this->model.count(evse_component) == 0) {
                    throw DeviceModelError("Could not find required Connector component in device model");
                }

                for (const auto& required_variable : required_connector_variables) {
                    const auto& variable = ConnectorComponentVariables::get_component_variable(
                        evse_id, clamp_to<std::int32_t>(connector_id), required_variable);
                    check_variable_has_value(variable);
                }
            }
        }
    } catch (const DeviceModelError& e) {
        EVLOG_error << "Integrity check in Device Model failed: " << e.what();
        throw e;
    }
}

bool DeviceModel::update_monitor_reference(std::int32_t monitor_id, const std::string& reference_value) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    const auto* monitor = this->monitors.find(monitor_id);
    if (monitor == nullptr or monitor->monitor.monitor.type != MonitorEnum::Delta or
        this->find_variable(monitor->component, monitor->variable) == nullptr) {
        EVLOG_warning << "Could not find delta monitor with ID: " << monitor_id << ". Reference value not updated!";
        return false;
    }

    try {
        if (this->monitors.update_reference(monitor_id, reference_value)) {
            return true;
        }
        EVLOG_warning << "Could not update in DB delta monitor with ID: " << monitor_id
                      << ". Reference value not updated!";
    } catch (const everest::db::Exception& e) {
        EVLOG_error << "Exception while updating delta monitor reference with ID: " << monitor_id;
        throw DeviceModelError(e.what());
    }
    return false;
}

std::vector<SetMonitoringResult> DeviceModel::set_monitors(const std::vector<SetMonitoringData>& requests,
                                                           const VariableMonitorType type) {
    std::unique_lock<std::recursive_mutex> lock(this->mutex);
    if (requests.empty()) {
        return {};
    }

    std::vector<SetMonitoringResult> set_monitors_res;
    // Listener notifications deferred until the lock is released: see set_value.
    std::vector<std::function<void()>> pending_notifications;

    for (auto& request : requests) {
        SetMonitoringResult result;

        // Set the com/var based on the request since it's required in a response
        // even if it is 'UnknownComoponent/Variable'
        result.component = request.component;
        result.variable = request.variable;
        result.severity = request.severity;
        result.type = request.type;
        result.id = request.id;

        // N04.FR.16 - If we find a monitor with this ID, and there's a comp/var mismatch, send a rejected result
        // N04.FR.13 - If we receive an ID but we can't find a monitor with this ID send a rejected result
        const bool request_has_id = request.id.has_value();
        if (request_has_id) {
            const auto* existing = this->monitors.find(request.id.value());
            const bool id_found =
                existing != nullptr and this->find_variable(existing->component, existing->variable) != nullptr;
            if (!id_found or !(existing->component == request.component and existing->variable == request.variable)) {
                result.status = SetMonitoringStatusEnum::Rejected;
                set_monitors_res.push_back(result);
                continue;
            }
        }

        const auto component_it = this->model.find(request.component);
        if (component_it == this->model.end()) {
            result.status = SetMonitoringStatusEnum::UnknownComponent;
            set_monitors_res.push_back(result);
            continue;
        }

        const auto variable_it = component_it->second.find(request.variable);
        if (variable_it == component_it->second.end()) {
            result.status = SetMonitoringStatusEnum::UnknownVariable;
            set_monitors_res.push_back(result);
            continue;
        }
        const auto& composed = variable_it->second;
        const auto& meta_data = *composed.meta_data;

        // Validate the data we want to set based on the characteristics and
        // see if it is out of range or out of the variable list
        const auto& characteristics = meta_data.characteristics;
        bool valid_value = true;

        if (characteristics.supportsMonitoring) {
            EVLOG_debug << "Validating monitor request of type: [" << request << "] and characteristics: ["
                        << characteristics << "]"
                        << " and value: " << request.value;

            // If the monitor is of type 'delta' (or periodic) and it is of a non-numeric type
            // the value is ignored since all values will be reported (excepting negative values)
            if (request.type == MonitorEnum::Delta && std::signbit(request.value)) {
                // N04.FR.14
                valid_value = false;
            } else if (request.type == MonitorEnum::Delta &&
                       (characteristics.dataType != DataEnum::decimal &&
                        characteristics.dataType != DataEnum::integer)) { // NOLINT(bugprone-branch-clone): readability
                valid_value = true;
            } else if (request.type == MonitorEnum::Periodic || request.type == MonitorEnum::PeriodicClockAligned) {
                valid_value = true;
            } else {
                try {
                    valid_value = validate_value(characteristics,
                                                 monitor_value_to_string(request.value, characteristics.dataType),
                                                 allow_zero(request.component, request.variable));
                } catch (const std::exception& e) {
                    EVLOG_warning << "Could not validate monitor value: " << request.value
                                  << " for component: " << request.component << " and variable: " << request.variable;
                    valid_value = false;
                }
            }
        } else {
            valid_value = false;
        }

        // A delta monitor needs a value to take its reference from
        if (valid_value and request.type == MonitorEnum::Delta) {
            const auto actual =
                this->read_attribute(request.component, request.variable, composed, AttributeEnum::Actual);
            valid_value = actual.has_value() and actual->value.has_value();
        }

        if (!valid_value) {
            result.status = SetMonitoringStatusEnum::Rejected;
            set_monitors_res.push_back(result);
            continue;
        }

        // 3.77 Duplicate - A monitor already exists for the given type/severity combination.
        // Only test for duplicates if we do not receive an explicit monitor ID
        if (!request_has_id) {
            const auto existing = this->monitors.of(request.component, request.variable);
            const bool duplicate = std::any_of(existing.begin(), existing.end(), [&request](const auto& entry) {
                return entry.second.monitor.type == request.type and entry.second.monitor.severity == request.severity;
            });
            if (duplicate) {
                result.status = SetMonitoringStatusEnum::Duplicate;
                set_monitors_res.push_back(result);
                continue;
            }
        }

        const auto attribute =
            this->read_attribute(request.component, request.variable, composed, AttributeEnum::Actual);
        static const std::string empty_value{};
        const std::string current_value =
            (attribute.has_value() and attribute->value.has_value()) ? attribute->value->get() : empty_value;

        try {
            const auto monitor_meta = this->monitors.set(request, type, current_value);
            if (!monitor_meta.has_value()) {
                result.status = SetMonitoringStatusEnum::Rejected;
                set_monitors_res.push_back(result);
                continue;
            }

            // N07.FR.11
            // In case of an existing monitor update
            if (request_has_id && monitor_update_listener) {
                if (attribute.has_value()) {
                    pending_notifications.push_back(
                        [listener = monitor_update_listener, monitor_meta = monitor_meta.value(),
                         component = request.component, variable = request.variable,
                         characteristics = meta_data.characteristics, attribute = attribute.value(), current_value]() {
                            listener(monitor_meta, component, variable, characteristics, attribute, current_value);
                        });
                } else {
                    EVLOG_warning << "Could not notify monitor update listener, missing variable attribute: "
                                  << request.variable;
                }
            }

            result.id = monitor_meta->monitor.id;
            result.status = SetMonitoringStatusEnum::Accepted;
        } catch (const everest::db::Exception& e) {
            EVLOG_error << "Set monitors failed:" << e.what();
            throw DeviceModelError(e.what());
        }

        set_monitors_res.push_back(result);
    }

    lock.unlock();
    for (const auto& notify : pending_notifications) {
        notify();
    }

    return set_monitors_res;
}

std::vector<VariableMonitoringPeriodic> DeviceModel::get_periodic_monitors() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    std::vector<VariableMonitoringPeriodic> periodics;

    for (const auto& [component, variable_map] : this->model) {
        for (const auto& [variable, composed] : variable_map) {
            std::vector<VariableMonitoringMeta> monitors;

            for (const auto& [id, monitor_meta] : this->monitors.of(component, variable)) {
                if (monitor_meta.monitor.type == MonitorEnum::Periodic ||
                    monitor_meta.monitor.type == MonitorEnum::PeriodicClockAligned) {
                    monitors.push_back(monitor_meta);
                }
            }

            if (!monitors.empty()) {
                periodics.push_back({component, variable, monitors});
            }
        }
    }

    return periodics;
}

std::vector<MonitoringData> DeviceModel::get_monitors(const std::vector<MonitoringCriterionEnum>& criteria,
                                                      const std::vector<ComponentVariable>& component_variables) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    std::vector<MonitoringData> get_monitors_res{};

    const auto add_monitors_of = [this, &criteria, &get_monitors_res](const Component& component,
                                                                      const Variable& variable) {
        MonitoringData monitor_data;
        monitor_data.component = component;
        monitor_data.variable = variable;
        for (const auto& [id, monitor_meta] : this->monitors.of(component, variable)) {
            // Also handles the case when the criteria is empty, since in that case N02.FR.11 applies (all monitors
            // pass)
            if (ocpp::v2::utils::filter_criteria_monitor(criteria, monitor_meta)) {
                monitor_data.variableMonitoring.push_back(monitor_meta.monitor);
            }
        }
        if (!monitor_data.variableMonitoring.empty()) {
            get_monitors_res.push_back(std::move(monitor_data));
        }
    };

    // N02.FR.11 - if criteria and component_variables are empty, return all existing monitors
    if (component_variables.empty()) {
        for (const auto& [component, variable_map] : this->model) {
            for (const auto& [variable, composed] : variable_map) {
                add_monitors_of(component, variable);
            }
        }
        return get_monitors_res;
    }

    for (const auto& component_variable : component_variables) {
        const auto component_it = this->model.find(component_variable.component);
        // Case not handled by spec, skipping
        if (component_it == this->model.end()) {
            continue;
        }

        // N02.FR.16 - if variable is missing, report all existing variables inside that component
        if (!component_variable.variable.has_value()) {
            for (const auto& [variable, composed] : component_it->second) {
                add_monitors_of(component_variable.component, variable);
            }
            continue;
        }

        // Case not handled by spec, skipping
        if (component_it->second.count(component_variable.variable.value()) == 0) {
            continue;
        }
        add_monitors_of(component_variable.component, component_variable.variable.value());
    }

    return get_monitors_res;
}

std::vector<ClearMonitoringResult> DeviceModel::clear_monitors(const std::vector<int>& request_ids,
                                                               bool allow_protected) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    if (request_ids.empty()) {
        return {};
    }

    std::vector<ClearMonitoringResult> clear_monitors_vec;

    for (auto& id : request_ids) {
        ClearMonitoringResult clear_monitor_res;
        clear_monitor_res.id = id;

        try {
            clear_monitor_res.status = this->monitors.clear(id, allow_protected);
        } catch (const everest::db::Exception& e) {
            EVLOG_error << "Clear monitors failed:" << e.what();
            throw DeviceModelError(e.what());
        }

        clear_monitors_vec.push_back(clear_monitor_res);
    }

    return clear_monitors_vec;
}

std::int32_t DeviceModel::clear_custom_monitors() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    try {
        return this->monitors.clear_custom();
    } catch (const everest::db::Exception& e) {
        EVLOG_error << "Clear custom monitors failed:" << e.what();
        throw DeviceModelError(e.what());
    }
}

// ConnectivityManagerConfiguration implementations

std::string DeviceModel::get_network_configuration_priority() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    return get_optional_value<std::string>(ControllerComponentVariables::NetworkConfigurationPriority).value_or("");
}

// The write-path wrappers below take no lock: the setters they delegate to lock themselves, and holding
// the recursive mutex here would keep it owned while the setters invoke the listeners.

void DeviceModel::set_network_configuration_priority(const std::string& priority, const std::string& source) {
    if (!ControllerComponentVariables::NetworkConfigurationPriority.variable.has_value()) {
        EVLOG_warning << "NetworkConfigurationPriority variable is not defined in the device model, cannot set value";
        return;
    }
    set_value(ControllerComponentVariables::NetworkConfigurationPriority.component,
              ControllerComponentVariables::NetworkConfigurationPriority.variable.value(), AttributeEnum::Actual,
              priority, source);
}

std::optional<ocpp::v2::NetworkConnectionProfile> DeviceModel::read_network_connection_profile(int32_t slot) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    return NetworkConfigurationComponentVariables::read_profile_from_device_model(*this, slot);
}

bool DeviceModel::write_network_connection_profile(int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                                                   const std::string& source) {
    return NetworkConfigurationComponentVariables::write_profile_to_device_model(*this, slot, profile, source);
}

void DeviceModel::clear_network_connection_profile(int32_t slot) {
    NetworkConfigurationComponentVariables::clear_slot_in_device_model(*this, slot);
}

bool DeviceModel::get_allow_security_level_zero_connections() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    return get_optional_value<bool>(ControllerComponentVariables::AllowSecurityLevelZeroConnections).value_or(false);
}

int32_t DeviceModel::get_security_profile() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    return get_value<int>(ControllerComponentVariables::SecurityProfile);
}

std::optional<int32_t> DeviceModel::get_network_config_timeout() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    return get_optional_value<int>(ControllerComponentVariables::NetworkConfigTimeout);
}

std::string DeviceModel::resolve_identity(int32_t slot) {
    std::string identity = get_value<std::string>(ControllerComponentVariables::SecurityCtrlrIdentity);
    const auto slot_identity_cv = NetworkConfigurationComponentVariables::get_component_variable(
        slot, NetworkConfigurationComponentVariables::Identity);
    if (const auto slot_identity = get_optional_value<std::string>(slot_identity_cv);
        slot_identity.has_value() && !slot_identity->empty()) {
        identity = *slot_identity;
        EVLOG_debug << "Using per-slot Identity for slot " << slot;
    }
    return identity;
}

std::optional<std::string> DeviceModel::resolve_basic_auth_password(int32_t slot) {
    const auto slot_pwd_cv = NetworkConfigurationComponentVariables::get_component_variable(
        slot, NetworkConfigurationComponentVariables::BasicAuthPassword);
    if (const auto slot_pwd = get_optional_value<std::string>(slot_pwd_cv);
        slot_pwd.has_value() && !slot_pwd->empty()) {
        EVLOG_debug << "Using per-slot BasicAuthPassword for slot " << slot;
        return *slot_pwd;
    }
    return get_optional_value<std::string>(ControllerComponentVariables::BasicAuthPassword);
}

std::optional<WebsocketConnectionOptions> DeviceModel::get_websocket_connection_options(int32_t slot) {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);
    const auto charge_point_id = get_value<std::string>(ControllerComponentVariables::SecurityCtrlrIdentity);
    if (charge_point_id.find(':') != std::string::npos) {
        EVLOG_AND_THROW(std::runtime_error("ChargePointId must not contain ':'"));
    }

    const auto profile_opt = read_network_connection_profile(slot);
    if (!profile_opt.has_value()) {
        EVLOG_critical << "Could not retrieve NetworkProfile of configurationSlot: " << slot;
        return std::nullopt;
    }
    const auto& profile = *profile_opt;

    const std::string identity = resolve_identity(slot);
    const std::optional<std::string> basic_auth_password = resolve_basic_auth_password(slot);

    try {
        auto uri = Uri::parse_and_validate(profile.ocppCsmsUrl.get(), identity, profile.securityProfile);
        const auto ocpp_versions = utils::get_ocpp_protocol_versions(
            get_value<std::string>(ControllerComponentVariables::SupportedOcppVersions));

        WebsocketConnectionOptions opts{
            ocpp_versions,
            uri,
            profile.securityProfile,
            basic_auth_password,
            std::chrono::seconds(std::max(profile.messageTimeout, 1)),
            get_value<int>(ControllerComponentVariables::RetryBackOffRandomRange),
            get_value<int>(ControllerComponentVariables::RetryBackOffRepeatTimes),
            get_value<int>(ControllerComponentVariables::RetryBackOffWaitMinimum),
            get_value<int>(ControllerComponentVariables::NetworkProfileConnectionAttempts),
            get_value<std::string>(ControllerComponentVariables::SupportedCiphers12),
            get_value<std::string>(ControllerComponentVariables::SupportedCiphers13),
            get_value<int>(ControllerComponentVariables::WebSocketPingInterval),
            get_optional_value<std::string>(ControllerComponentVariables::WebsocketPingPayload).value_or("payload"),
            get_optional_value<int>(ControllerComponentVariables::WebsocketPongTimeout)
                .value_or(DEFAULT_WEBSOCKET_PONG_TIMEOUT_S),
            get_optional_value<bool>(ControllerComponentVariables::UseSslDefaultVerifyPaths).value_or(true),
            get_optional_value<bool>(ControllerComponentVariables::AdditionalRootCertificateCheck).value_or(false),
            std::nullopt, // hostName
            get_optional_value<bool>(ControllerComponentVariables::VerifyCsmsCommonName).value_or(true),
            get_optional_value<bool>(ControllerComponentVariables::UseTPM).value_or(false),
            get_optional_value<bool>(ControllerComponentVariables::VerifyCsmsAllowWildcards).value_or(false),
            get_optional_value<std::string>(ControllerComponentVariables::IFace),
            get_optional_value<bool>(ControllerComponentVariables::EnableTLSKeylog).value_or(false),
            get_optional_value<std::string>(ControllerComponentVariables::TLSKeylogFile)};
        return opts;
    } catch (const std::invalid_argument& e) {
        EVLOG_error << "Could not configure the connection options for slot " << slot << ": " << e.what();
        return std::nullopt;
    }
}

void DeviceModel::set_active_security_profile(int32_t security_profile, const std::string& source) {
    const auto& cv = ControllerComponentVariables::SecurityProfile;
    if (!cv.variable.has_value()) {
        return;
    }
    const auto result = set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                            std::to_string(security_profile), source);
    if (result != SetVariableStatusEnum::Accepted) {
        EVLOG_warning << "Could not set SecurityCtrlr.SecurityProfile to " << security_profile << ": " << result;
    }
}

void DeviceModel::set_active_message_timeout(std::int32_t message_timeout_s, const std::string& source) {
    const auto& cv = ControllerComponentVariables::MessageTimeout;
    if (cv.variable.has_value()) {
        set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual, std::to_string(message_timeout_s),
                            source);
    }
}

void DeviceModel::set_active_network_profile_slot(int32_t slot, const std::string& source) {
    const auto& cv = ControllerComponentVariables::ActiveNetworkProfile;
    if (cv.variable.has_value()) {
        set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual, std::to_string(slot), source);
    }
}

std::optional<std::int32_t> DeviceModel::get_active_network_profile_slot() {
    return this->get_optional_value<std::int32_t>(ControllerComponentVariables::ActiveNetworkProfile);
}

void DeviceModel::set_per_slot_ocpp_version(int32_t slot, const std::string& version, const std::string& source) {
    const auto nc_cv = NetworkConfigurationComponentVariables::get_component_variable(
        slot, NetworkConfigurationComponentVariables::OcppVersion);
    if (nc_cv.variable.has_value()) {
        set_value(nc_cv.component, nc_cv.variable.value(), AttributeEnum::Actual, version, source);
    }
}

void DeviceModel::set_security_ctrl_security_profile(int32_t security_profile, const std::string& source) {
    const auto result = set_read_only_value(ControllerComponents::SecurityCtrlr,
                                            NetworkConfigurationComponentVariables::SecurityProfile,
                                            AttributeEnum::Actual, std::to_string(security_profile), source);
    if (result != SetVariableStatusEnum::Accepted) {
        EVLOG_warning << "Could not set SecurityCtrlr.SecurityProfile to " << security_profile << ": " << result;
    }
}

void DeviceModel::set_security_ctrl_identity(const std::string& identity, const std::string& source) {
    set_read_only_value(ControllerComponents::SecurityCtrlr, NetworkConfigurationComponentVariables::Identity,
                        AttributeEnum::Actual, identity, source);
}

} // namespace v2
} // namespace ocpp
