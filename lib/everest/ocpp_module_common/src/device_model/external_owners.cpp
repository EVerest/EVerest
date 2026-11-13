// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/external_owners.hpp>

#include <algorithm>
#include <exception>
#include <stdexcept>

#include <everest/logging.hpp>
#include <everest/ocpp_module_common/conversions.hpp>
#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>
#include <ocpp/v2/comparators.hpp>

namespace ocpp_module_common::device_model {

namespace {

namespace dm_types = types::ocpp_device_model;
using conversions::to_everest_attribute_enum;
using conversions::to_everest_component;
using conversions::to_everest_variable;
using conversions::to_ocpp_attribute_enum;
using conversions::to_ocpp_component;
using conversions::to_ocpp_variable;

ocpp::v2::VariableCharacteristics to_ocpp_characteristics(const dm_types::VariableCharacteristics& characteristics) {
    ocpp::v2::VariableCharacteristics result;
    result.dataType = conversions::to_ocpp_data_enum(characteristics.data_type);
    result.supportsMonitoring = characteristics.supports_monitoring;
    if (characteristics.unit.has_value()) {
        result.unit = characteristics.unit.value();
    }
    result.minLimit = characteristics.min_limit;
    result.maxLimit = characteristics.max_limit;
    result.maxElements = characteristics.max_elements;
    if (characteristics.values_list.has_value()) {
        result.valuesList = characteristics.values_list.value();
    }
    return result;
}

ocpp::v2::VariableAttribute to_ocpp_attribute(const dm_types::VariableAttribute& attribute) {
    ocpp::v2::VariableAttribute result;
    if (attribute.type.has_value()) {
        result.type = to_ocpp_attribute_enum(attribute.type.value());
    }
    if (attribute.value.has_value()) {
        result.value = attribute.value.value();
    }
    result.mutability = attribute.mutability.has_value()
                            ? conversions::to_ocpp_mutability_enum(attribute.mutability.value())
                            : ocpp::v2::MutabilityEnum::ReadOnly;
    result.persistent = attribute.persistent;
    result.constant = attribute.constant;
    return result;
}

ocpp::v2::VariableMonitoringMeta to_ocpp_monitor(const dm_types::RegisteredMonitor& monitor) {
    ocpp::v2::VariableMonitoringMeta result;
    result.monitor.id = 0;
    result.monitor.transaction = monitor.transaction.value_or(false);
    result.monitor.value = monitor.value;
    result.monitor.type = conversions::to_ocpp_monitor_enum(monitor.type);
    result.monitor.severity = monitor.severity;
    switch (monitor.monitor_type) {
    case dm_types::RegisteredMonitorType::HardWiredMonitor:
        result.type = ocpp::v2::VariableMonitorType::HardWiredMonitor;
        break;
    case dm_types::RegisteredMonitorType::PreconfiguredMonitor:
        result.type = ocpp::v2::VariableMonitorType::PreconfiguredMonitor;
        break;
    }
    return result;
}

dm_types::RegistrationRejectionReason to_everest_rejection_reason(const ocpp::v2::RegistrationRejectionReason reason) {
    switch (reason) {
    case ocpp::v2::RegistrationRejectionReason::AlreadyDefined:
        return dm_types::RegistrationRejectionReason::AlreadyDefined;
    case ocpp::v2::RegistrationRejectionReason::NoAttributes:
        return dm_types::RegistrationRejectionReason::NoAttributes;
    case ocpp::v2::RegistrationRejectionReason::ReservedOwner:
        return dm_types::RegistrationRejectionReason::ReservedOwner;
    case ocpp::v2::RegistrationRejectionReason::InvalidNameKey:
        return dm_types::RegistrationRejectionReason::InvalidNameKey;
    case ocpp::v2::RegistrationRejectionReason::ReservedVariable:
        return dm_types::RegistrationRejectionReason::ReservedVariable;
    }
    throw std::out_of_range("Unknown RegistrationRejectionReason");
}

dm_types::RejectedVariable make_unconvertible(const types::ocpp::Component& component,
                                              const types::ocpp::Variable& variable) {
    dm_types::RejectedVariable rejected;
    rejected.component = component;
    rejected.variable = variable;
    rejected.reason = dm_types::RegistrationRejectionReason::InvalidNameKey;
    return rejected;
}

/// \brief Converts the components of a registration, leaving out the variables that do not fit the OCPP types and
/// adding them to \p unconvertible
std::vector<ocpp::v2::RegisteredComponent>
to_registered_components(const std::string& owner, const std::vector<dm_types::RegisteredComponent>& components,
                         std::vector<dm_types::RejectedVariable>& unconvertible) {
    std::vector<ocpp::v2::RegisteredComponent> result;
    for (const auto& component : components) {
        ocpp::v2::RegisteredComponent registered;
        try {
            registered.component = to_ocpp_component(component.component);
        } catch (const std::exception& e) {
            EVLOG_error << "Owner " << owner << ": the variables of component " << component.component.name
                        << " are rejected: " << e.what();
            for (const auto& variable : component.variables) {
                unconvertible.push_back(make_unconvertible(component.component, variable.variable));
            }
            continue;
        }
        for (const auto& variable : component.variables) {
            try {
                registered.variables.push_back(to_registered_variable(variable));
            } catch (const std::exception& e) {
                EVLOG_error << "Owner " << owner << ": variable " << variable.variable.name << " of component "
                            << component.component.name << " is rejected: " << e.what();
                unconvertible.push_back(make_unconvertible(component.component, variable.variable));
            }
        }
        result.push_back(std::move(registered));
    }
    return result;
}

dm_types::RegistrationResult make_result(const std::string& owner, const dm_types::RegistrationStatus status,
                                         const std::vector<dm_types::RejectedVariable>& unconvertible = {},
                                         const ocpp::v2::ComponentRegistrationResult& registration = {}) {
    dm_types::RegistrationResult result;
    result.owner = owner;
    result.status = status;
    result.rejected = unconvertible;
    for (const auto& rejected : registration.rejected) {
        result.rejected.push_back(to_everest_rejected_variable(rejected));
    }
    return result;
}

/// \brief Stores \p value as the value of \p attribute in \p components
/// \returns false if the components do not contain the attribute
bool store_value(std::vector<ocpp::v2::RegisteredComponent>& components, const ocpp::v2::Component& component,
                 const ocpp::v2::Variable& variable, const ocpp::v2::AttributeEnum attribute,
                 const std::string& value) {
    for (auto& registered_component : components) {
        if (not(registered_component.component == component)) {
            continue;
        }
        for (auto& registered_variable : registered_component.variables) {
            if (not(registered_variable.variable == variable)) {
                continue;
            }
            for (auto& registered_attribute : registered_variable.attributes) {
                if (registered_attribute.type.value_or(ocpp::v2::AttributeEnum::Actual) == attribute) {
                    registered_attribute.value = value;
                    return true;
                }
            }
        }
    }
    return false;
}

/// \brief Owners that OCPPmulti registers itself, besides the owners only libocpp uses, which the device model rejects
bool is_taken_by_ocpp_module(const std::string& owner) {
    return owner == VARIABLE_OWNER_EVEREST or owner == VARIABLE_OWNER_EVEREST_CONFIG;
}

/// \brief Passes the writes to the variables of one owner on to the connection that registered it
class OwnerProvider final : public ocpp::v2::VariableValueProvider {
public:
    OwnerProvider(std::shared_ptr<DeviceModelOwnerConnection> connection, std::string owner) :
        connection(std::move(connection)), owner(std::move(owner)) {
    }

    ocpp::v2::SetVariableStatusEnum set_value(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                              const ocpp::v2::AttributeEnum attribute, const std::string& value,
                                              const std::string& source) override {
        dm_types::WriteRequest request;
        request.owner = this->owner;
        request.component = to_everest_component(component);
        request.variable = to_everest_variable(variable);
        request.attribute_type = to_everest_attribute_enum(attribute);
        request.value = value;
        request.source = source;
        try {
            return conversions::to_ocpp_set_variable_status_enum(this->connection->set_value(request));
        } catch (const std::exception& e) {
            EVLOG_warning << "Write to " << component << "." << variable << " of owner " << this->owner
                          << " rejected: " << e.what();
            return ocpp::v2::SetVariableStatusEnum::Rejected;
        }
    }

private:
    std::shared_ptr<DeviceModelOwnerConnection> connection;
    std::string owner;
};

} // namespace

ocpp::v2::RegisteredVariable to_registered_variable(const dm_types::RegisteredVariable& variable) {
    ocpp::v2::RegisteredVariable result;
    result.variable = to_ocpp_variable(variable.variable);
    result.characteristics = to_ocpp_characteristics(variable.characteristics);
    for (const auto& attribute : variable.attributes) {
        result.attributes.push_back(to_ocpp_attribute(attribute));
    }
    if (variable.monitors.has_value()) {
        for (const auto& monitor : variable.monitors.value()) {
            result.monitors.push_back(to_ocpp_monitor(monitor));
        }
    }
    return result;
}

dm_types::RejectedVariable to_everest_rejected_variable(const ocpp::v2::RejectedVariable& rejected) {
    dm_types::RejectedVariable result;
    result.component = to_everest_component(rejected.component);
    result.variable = to_everest_variable(rejected.variable);
    result.reason = to_everest_rejection_reason(rejected.reason);
    if (rejected.reason == ocpp::v2::RegistrationRejectionReason::AlreadyDefined and not rejected.defined_by.empty()) {
        result.defined_by = rejected.defined_by;
    }
    return result;
}

ExternalOwners::ExternalOwners(std::vector<std::shared_ptr<DeviceModelOwnerConnection>> connections) :
    connections(std::move(connections)) {
    auto handle = this->state.handle();
    handle->answered.resize(this->connections.size(), false);
    handle->early.resize(this->connections.size());
}

ExternalOwners::~ExternalOwners() {
    this->detach();
}

std::string ExternalOwners::owner_of(const std::size_t index, const std::optional<std::string>& owner) const {
    return owner.value_or(this->connections.at(index)->module_id());
}

void ExternalOwners::set_startup_registrations(const std::size_t index, const dm_types::Registrations& registrations) {
    auto handle = this->state.handle();
    if (handle->answered.at(index)) {
        EVLOG_warning << "Startup registrations of " << this->connections.at(index)->module_id()
                      << " arrived after the device model was composed; ignored";
        return;
    }
    std::vector<std::string> claimed;
    for (const auto& registration : registrations.registrations) {
        auto applied = this->apply(*handle, index, registration, &claimed);
        if (applied.result.has_value()) {
            handle->pending_results.push_back(std::move(applied.result.value()));
        }
    }
    this->replay_early(*handle, index);
}

void ExternalOwners::on_registration(const std::size_t index, const dm_types::Registration& registration) {
    Applied applied;
    {
        auto handle = this->state.handle();
        if (not handle->answered.at(index)) {
            handle->early.at(index).push_back(registration);
            return;
        }
        applied = this->apply(*handle, index, registration, nullptr);
        if (applied.result.has_value() and handle->registry == nullptr) {
            handle->pending_results.push_back(std::move(applied.result.value()));
            return;
        }
    }

    if (applied.release_from != nullptr) {
        // Without the lock: unregistering waits for the writes to the owner in progress
        const auto& owner = applied.result.value().second.owner;
        try {
            applied.release_from->unregister_components(owner);
        } catch (const std::exception& e) {
            EVLOG_error << "Unregistering owner " << owner << " failed: " << e.what();
        }
        {
            auto handle = this->state.handle();
            const auto entry = handle->owners.find(owner);
            if (entry != handle->owners.end() and entry->second.releasing) {
                handle->owners.erase(entry);
            }
            --handle->releases_in_progress;
        }
        this->state.notify_all();
        EVLOG_info << "Owner " << owner << " released";
    }

    if (applied.result.has_value()) {
        this->send({std::move(applied.result.value())});
    }
}

void ExternalOwners::on_value_changed(const std::size_t index, const dm_types::ValueChange& change) {
    const auto owner = this->owner_of(index, change.owner);
    ocpp::v2::Component component;
    ocpp::v2::Variable variable;
    try {
        component = to_ocpp_component(change.component);
        variable = to_ocpp_variable(change.variable);
    } catch (const std::exception& e) {
        EVLOG_warning << "Value change of owner " << owner << " ignored: " << e.what();
        return;
    }
    const auto attribute = change.attribute_type.has_value() ? to_ocpp_attribute_enum(change.attribute_type.value())
                                                             : ocpp::v2::AttributeEnum::Actual;

    auto handle = this->state.handle();
    const auto entry = handle->owners.find(owner);
    if (entry == handle->owners.end() or entry->second.connection != index or entry->second.releasing) {
        EVLOG_warning << "Value change of " << component << "." << variable << " ignored: owner " << owner
                      << " is not registered by " << this->connections.at(index)->module_id();
        return;
    }
    try {
        if (handle->registry != nullptr) {
            const auto status =
                handle->registry->notify_value_changed(component, variable, attribute, change.value, owner);
            if (status != ocpp::v2::SetVariableStatusEnum::Accepted) {
                EVLOG_warning << "Value change of " << component << "." << variable << " of owner " << owner
                              << " ignored: " << status;
                return;
            }
        }
        if (not store_value(entry->second.components, component, variable, attribute, change.value) and
            handle->registry == nullptr) {
            EVLOG_warning << "Value change of " << component << "." << variable << " ignored: owner " << owner
                          << " did not register it";
        }
    } catch (const std::exception& e) {
        EVLOG_warning << "Value change of " << component << "." << variable << " of owner " << owner
                      << " ignored: " << e.what();
    }
}

std::vector<OwnerRegistrationResult> ExternalOwners::register_with(ocpp::v2::DeviceModelRegistryInterface& registry) {
    std::vector<OwnerRegistrationResult> results;
    std::vector<PendingResult> to_send;
    {
        auto handle = this->state.handle();
        for (std::size_t index = 0; index < this->connections.size(); ++index) {
            if (not handle->answered.at(index)) {
                this->replay_early(*handle, index);
            }
        }
        handle->registry = &registry;
        to_send = std::move(handle->pending_results);
        handle->pending_results.clear();
        for (const auto& [owner, entry] : handle->owners) {
            try {
                const auto result = registry.register_components(owner, entry.components, entry.provider);
                results.push_back({owner, result});
                to_send.emplace_back(entry.connection, make_result(owner, dm_types::RegistrationStatus::Accepted,
                                                                   entry.unconvertible, result));
            } catch (const std::exception& e) {
                EVLOG_error << "Registering owner " << owner << " failed: " << e.what();
            }
        }
    }
    this->send(to_send);
    return results;
}

void ExternalOwners::detach() {
    std::vector<std::string> owners;
    ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
    {
        auto handle = this->state.handle();
        handle.wait([&handle]() { return handle->releases_in_progress == 0; });
        registry = std::exchange(handle->registry, nullptr);
        if (registry == nullptr) {
            return;
        }
        for (const auto& [owner, entry] : handle->owners) {
            owners.push_back(owner);
        }
    }
    // Without the lock: unregistering waits for the writes to the owners in progress
    for (const auto& owner : owners) {
        try {
            registry->unregister_components(owner);
        } catch (const std::exception& e) {
            EVLOG_error << "Unregistering owner " << owner << " failed: " << e.what();
        }
    }
}

ExternalOwners::Applied ExternalOwners::apply(State& state, const std::size_t index,
                                              const dm_types::Registration& registration,
                                              std::vector<std::string>* claimed) {
    const auto owner = this->owner_of(index, registration.owner);
    const auto entry = state.owners.find(owner);
    const bool claimed_in_call =
        claimed != nullptr and std::find(claimed->begin(), claimed->end(), owner) != claimed->end();
    if (is_taken_by_ocpp_module(owner) or claimed_in_call or
        (entry != state.owners.end() and (entry->second.connection != index or entry->second.releasing))) {
        EVLOG_warning << "Registration of owner " << owner << " by " << this->connections.at(index)->module_id()
                      << " refused: the owner is in use";
        return {PendingResult{index, make_result(owner, dm_types::RegistrationStatus::OwnerInUse)}, nullptr};
    }
    if (claimed != nullptr) {
        claimed->push_back(owner);
    }

    if (registration.components.empty()) {
        Applied applied{PendingResult{index, make_result(owner, dm_types::RegistrationStatus::Accepted)}, nullptr};
        if (entry == state.owners.end()) {
            return applied;
        }
        if (state.registry == nullptr) {
            state.owners.erase(entry);
            EVLOG_info << "Owner " << owner << " released";
            return applied;
        }
        entry->second.releasing = true;
        ++state.releases_in_progress;
        applied.release_from = state.registry;
        return applied;
    }

    auto& owned = state.owners[owner];
    owned.connection = index;
    owned.unconvertible.clear();
    owned.components = to_registered_components(owner, registration.components, owned.unconvertible);
    if (owned.provider == nullptr) {
        owned.provider = std::make_shared<OwnerProvider>(this->connections.at(index), owner);
    }
    if (state.registry == nullptr) {
        return {};
    }
    try {
        const auto result = state.registry->register_components(owner, owned.components, owned.provider);
        return {PendingResult{index,
                              make_result(owner, dm_types::RegistrationStatus::Accepted, owned.unconvertible, result)},
                nullptr};
    } catch (const std::exception& e) {
        EVLOG_error << "Registering owner " << owner << " failed: " << e.what();
        return {};
    }
}

void ExternalOwners::replay_early(State& state, const std::size_t index) {
    state.answered.at(index) = true;
    auto early = std::move(state.early.at(index));
    state.early.at(index).clear();
    for (const auto& registration : early) {
        auto applied = this->apply(state, index, registration, nullptr);
        if (applied.result.has_value()) {
            state.pending_results.push_back(std::move(applied.result.value()));
        }
    }
}

void ExternalOwners::send(const std::vector<PendingResult>& results) {
    for (const auto& [index, result] : results) {
        try {
            this->connections.at(index)->set_registration_result(result);
        } catch (const std::exception& e) {
            EVLOG_warning << "Registration result for owner " << result.owner << " not delivered to "
                          << this->connections.at(index)->module_id() << ": " << e.what();
        }
    }
}

} // namespace ocpp_module_common::device_model
