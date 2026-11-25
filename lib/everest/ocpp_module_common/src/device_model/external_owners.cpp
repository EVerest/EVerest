// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/external_owners.hpp>

#include <algorithm>
#include <exception>
#include <random>
#include <stdexcept>

#include <everest/logging.hpp>
#include <everest/ocpp_module_common/conversions.hpp>
#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>
#include <ocpp/common/cistring.hpp>
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
    case ocpp::v2::RegistrationRejectionReason::Ocpp16KeyInUse:
        return dm_types::RegistrationRejectionReason::Ocpp16KeyInUse;
    case ocpp::v2::RegistrationRejectionReason::Ocpp16KeyInvalid:
        return dm_types::RegistrationRejectionReason::Ocpp16KeyInvalid;
    case ocpp::v2::RegistrationRejectionReason::ProviderChanged:
        // The device model still holds the owner with an earlier provider, so the variable is defined by the owner
        return dm_types::RegistrationRejectionReason::AlreadyDefined;
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

/// \brief Throws when \p key is not a valid OCPP 1.6 configuration key: empty or longer than 50 characters
void check_ocpp16_key(const std::string& key) {
    if (key.empty()) {
        throw std::out_of_range("ocpp16_key must not be empty");
    }
    static_cast<void>(ocpp::CiString<50>(key));
}

/// \brief Converts the components of a registration, leaving out the variables that do not fit the OCPP types and
/// adding them to \p unconvertible. Only a module that provides ocpp_device_model itself can send these, while EVerest
/// does not validate the schema.
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
                if (variable.ocpp16_key.has_value()) {
                    check_ocpp16_key(variable.ocpp16_key.value());
                }
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

/// \brief The OCPP 1.6 keys that \p components declare. The components and variables fit the OCPP types.
std::vector<DeclaredOcpp16Key> declared_keys(const std::vector<dm_types::RegisteredComponent>& components) {
    std::vector<DeclaredOcpp16Key> keys;
    for (const auto& component : components) {
        for (const auto& variable : component.variables) {
            if (variable.ocpp16_key.has_value()) {
                keys.push_back({to_ocpp_component(component.component), to_ocpp_variable(variable.variable),
                                variable.ocpp16_key.value()});
            }
        }
    }
    return keys;
}

/// \brief \p components without the variables in \p refused, and without the components left empty
std::vector<ocpp::v2::RegisteredComponent> without(const std::vector<ocpp::v2::RegisteredComponent>& components,
                                                   const std::vector<ocpp::v2::RejectedVariable>& refused) {
    if (refused.empty()) {
        return components;
    }
    std::vector<ocpp::v2::RegisteredComponent> result;
    for (const auto& component : components) {
        ocpp::v2::RegisteredComponent kept{component.component, {}};
        for (const auto& variable : component.variables) {
            const bool is_refused = std::any_of(refused.begin(), refused.end(), [&](const auto& rejected) {
                return rejected.component == component.component and rejected.variable == variable.variable;
            });
            if (not is_refused) {
                kept.variables.push_back(variable);
            }
        }
        if (not kept.variables.empty()) {
            result.push_back(std::move(kept));
        }
    }
    return result;
}

dm_types::RegistrationResult make_result(const std::string& owner, const dm_types::RegistrationStatus status,
                                         const std::vector<dm_types::RejectedVariable>& rejected = {},
                                         const ocpp::v2::ComponentRegistrationResult& registration = {}) {
    dm_types::RegistrationResult result;
    result.owner = owner;
    result.status = status;
    result.rejected = rejected;
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

dm_types::ValueChange to_value_change(const dm_types::WriteRequest& request) {
    dm_types::ValueChange change;
    change.owner = request.owner;
    change.component = request.component;
    change.variable = request.variable;
    change.attribute_type = request.attribute_type;
    change.value = request.value;
    return change;
}

} // namespace

/// \brief Passes the writes to the variables of one owner on to the connection that registered it. The owner's
/// accepted values reach the device model through its write results, in the order of its other messages.
class ExternalOwners::Provider final : public ocpp::v2::VariableValueProvider {
public:
    Provider(ExternalOwners& owners, std::size_t connection, std::string owner) :
        owners(owners), connection(connection), owner(std::move(owner)) {
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
        return conversions::to_ocpp_set_variable_status_enum(
            this->owners.write(this->connection, this->owner, request));
    }

    bool reports_accepted_values() const override {
        return true;
    }

private:
    ExternalOwners& owners;
    std::size_t connection;
    std::string owner;
};

bool is_external_owner(const std::string& owner) {
    return !is_taken_by_ocpp_module(owner) && owner != ocpp::v2::VARIABLE_OWNER_COMPONENT_CONFIG &&
           owner != ocpp::v2::VARIABLE_OWNER_OCPP_STACK;
}

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
    if (result.reason == dm_types::RegistrationRejectionReason::AlreadyDefined and not rejected.defined_by.empty()) {
        result.defined_by = rejected.defined_by;
    }
    return result;
}

ExternalOwners::ExternalOwners(std::vector<std::shared_ptr<DeviceModelOwnerConnection>> connections) :
    connections(std::move(connections)) {
    auto handle = this->state.handle();
    handle->answered.resize(this->connections.size(), false);
    handle->early.resize(this->connections.size());
    auto pending = this->writes.handle();
    pending->handling.resize(this->connections.size());
    std::random_device random;
    pending->request_id_prefix = std::to_string(random()) + "-" + std::to_string(random()) + "-";
}

ExternalOwners::~ExternalOwners() {
    this->detach();
}

std::string ExternalOwners::owner_of(const std::size_t index, const std::optional<std::string>& owner) const {
    return owner.value_or(this->connections.at(index)->module_id());
}

bool ExternalOwners::is_same_attribute(const std::string& owner, const std::string& module_id,
                                       const dm_types::ValueChange& kept, const dm_types::ValueChange& change) {
    const auto same_evse = [](const std::optional<types::ocpp::EVSE>& first,
                              const std::optional<types::ocpp::EVSE>& second) {
        if (not first.has_value() or not second.has_value()) {
            return first.has_value() == second.has_value();
        }
        return first->id == second->id and first->connector_id == second->connector_id;
    };
    const auto same_instance = [](const std::optional<std::string>& first, const std::optional<std::string>& second) {
        return ocpp::iequals(first.value_or(""), second.value_or(""));
    };
    return kept.owner.value_or(module_id) == owner and ocpp::iequals(kept.component.name, change.component.name) and
           same_instance(kept.component.instance, change.component.instance) and
           same_evse(kept.component.evse, change.component.evse) and
           ocpp::iequals(kept.variable.name, change.variable.name) and
           same_instance(kept.variable.instance, change.variable.instance) and
           kept.attribute_type.value_or(types::ocpp::AttributeEnum::Actual) ==
               change.attribute_type.value_or(types::ocpp::AttributeEnum::Actual);
}

void ExternalOwners::keep_latest(std::vector<dm_types::ValueChange>& changes, const std::size_t index,
                                 const dm_types::ValueChange& change) const {
    const auto module_id = this->connections.at(index)->module_id();
    const auto owner = change.owner.value_or(module_id);
    const auto earlier = std::find_if(changes.begin(), changes.end(), [&](const auto& kept) {
        return is_same_attribute(owner, module_id, kept, change);
    });
    if (earlier != changes.end()) {
        *earlier = change;
    } else {
        changes.push_back(change);
    }
}

void ExternalOwners::keep_early(State& state, const std::size_t index, const dm_types::OwnerMessage& message) const {
    auto& early = state.early.at(index);
    const auto module_id = this->connections.at(index)->module_id();
    if (message.registration.has_value()) {
        // A registration replaces everything its owner published before
        const auto owner = this->owner_of(index, message.registration->owner);
        early.erase(std::remove_if(early.begin(), early.end(),
                                   [&](const dm_types::OwnerMessage& kept) {
                                       const auto& kept_owner = kept.registration.has_value()
                                                                    ? kept.registration->owner
                                                                    : kept.value_change->owner;
                                       return kept_owner.value_or(module_id) == owner;
                                   }),
                    early.end());
        early.push_back(message);
        return;
    }
    const auto& change = message.value_change.value();
    const auto owner = change.owner.value_or(module_id);
    const auto earlier = std::find_if(early.begin(), early.end(), [&](const dm_types::OwnerMessage& kept) {
        return kept.value_change.has_value() and is_same_attribute(owner, module_id, kept.value_change.value(), change);
    });
    if (earlier != early.end()) {
        earlier->value_change = change;
    } else {
        early.push_back(message);
    }
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
            handle->outbox.push_back(std::move(applied.result.value()));
        }
    }
    this->replay_early(*handle, index);
}

void ExternalOwners::on_owner_message(const std::size_t index, const dm_types::OwnerMessage& message) {
    const auto parts = static_cast<int>(message.registration.has_value()) +
                       static_cast<int>(message.value_change.has_value()) +
                       static_cast<int>(message.write_result.has_value());
    if (parts != 1) {
        EVLOG_warning << "Message of " << this->connections.at(index)->module_id() << " ignored: it carries " << parts
                      << " parts instead of one";
        return;
    }
    const auto previous = std::exchange(this->writes.handle()->handling.at(index), std::this_thread::get_id());
    try {
        if (message.registration.has_value()) {
            this->on_registration(index, message.registration.value());
        } else if (message.value_change.has_value()) {
            this->on_value_changed(index, message.value_change.value());
        } else {
            this->on_write_result(index, message.write_result.value());
        }
    } catch (...) {
        this->writes.handle()->handling.at(index) = previous;
        throw;
    }
    this->writes.handle()->handling.at(index) = previous;
}

void ExternalOwners::on_registration(const std::size_t index, const dm_types::Registration& registration) {
    Applied applied;
    ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
    Ocpp16KeyBinder* binder{nullptr};
    std::vector<ocpp::v2::RegisteredComponent> components;
    std::vector<DeclaredOcpp16Key> keys;
    std::shared_ptr<ocpp::v2::VariableValueProvider> provider;
    {
        auto handle = this->state.handle();
        if (not handle->answered.at(index)) {
            dm_types::OwnerMessage message;
            message.registration = registration;
            this->keep_early(*handle, index, message);
            return;
        }
        applied = this->apply(*handle, index, registration, nullptr);
        registry = handle->registry;
        binder = handle->binder;
        if (applied.call == RegistryCall::None and applied.result.has_value()) {
            handle->outbox.push_back(std::move(applied.result.value()));
        } else if (applied.call == RegistryCall::Register) {
            components = handle->owners.at(applied.owner).components;
            keys = handle->owners.at(applied.owner).keys;
            provider = handle->owners.at(applied.owner).provider;
        }
    }

    // Without the lock: the device model waits for the writes in progress to the owner and notifies its listeners
    if (applied.call == RegistryCall::Unregister) {
        // Writes to the owner waiting for their write result stop waiting, which unregistering waits for
        this->end_writes_of(applied.owner);
        this->unregister(*registry, applied.owner);
        this->state.handle()->outbox.push_back(std::move(applied.result.value()));
        EVLOG_info << "Owner " << applied.owner << " released";
    } else if (applied.call == RegistryCall::Register) {
        std::optional<ocpp::v2::ComponentRegistrationResult> result;
        try {
            result = register_owner(*registry, binder, applied.owner, components, keys, provider);
        } catch (const std::exception& e) {
            EVLOG_error << "Registering owner " << applied.owner << " failed, the owner is dropped: " << e.what();
        }
        if (result.has_value()) {
            this->report_held(*registry, index, applied.owner, result.value());
        } else {
            // The device model may hold parts of the registration or the previous one: removed like a release, which
            // ends the registry call
            this->state.handle()->owners.at(applied.owner).releasing = true;
            this->end_writes_of(applied.owner);
            this->unregister(*registry, applied.owner);
            this->state.handle()->outbox.emplace_back(
                index, make_result(applied.owner, dm_types::RegistrationStatus::Rejected));
        }
    }

    this->flush();
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
    ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
    {
        auto handle = this->state.handle();
        if (not handle->answered.at(index)) {
            dm_types::OwnerMessage message;
            message.value_change = change;
            this->keep_early(*handle, index, message);
            return;
        }
        const auto entry = handle->owners.find(owner);
        if (entry == handle->owners.end() or entry->second.connection != index or entry->second.releasing) {
            EVLOG_warning << "Value change of " << component << "." << variable << " ignored: owner " << owner
                          << " is not registered by " << this->connections.at(index)->module_id();
            return;
        }
        if (handle->registry == nullptr) {
            if (not store(*handle, owner, change)) {
                EVLOG_warning << "Value change of " << component << "." << variable << " ignored: owner " << owner
                              << " did not register it";
            }
            return;
        }
        if (not entry->second.answered) {
            // The registration is being applied and may add the attribute
            this->keep_latest(entry->second.held_values, index, change);
            return;
        }
        registry = handle->registry;
        ++handle->registry_calls_in_progress;
    }
    this->report(*registry, index, owner, change);
}

void ExternalOwners::on_write_result(const std::size_t index, const dm_types::WriteResult& result) {
    std::optional<dm_types::WriteRequest> request;
    {
        auto pending = this->writes.handle();
        const auto entry = pending->pending.find(result.request_id);
        const auto module_id = this->connections.at(index)->module_id();
        if (entry != pending->pending.end() and entry->second.connection == index and
            is_same_attribute(entry->second.owner, module_id, to_value_change(result.request),
                              to_value_change(entry->second.request))) {
            request = entry->second.request;
        }
    }
    if (result.status == types::ocpp::SetVariableStatusEnumType::Accepted) {
        // The owner applied the value, also when the write stopped waiting for this result
        this->on_value_changed(index, to_value_change(request.value_or(result.request)));
    }
    if (not request.has_value()) {
        return;
    }
    {
        auto pending = this->writes.handle();
        const auto entry = pending->pending.find(result.request_id);
        if (entry == pending->pending.end()) {
            return;
        }
        entry->second.applied = true;
    }
    this->writes.notify_all();
}

types::ocpp::SetVariableStatusEnumType ExternalOwners::write(const std::size_t index, const std::string& owner,
                                                             const dm_types::WriteRequest& request) {
    std::string request_id;
    {
        auto pending = this->writes.handle();
        request_id = pending->request_id_prefix + std::to_string(++pending->last_request_id);
        pending->pending.emplace(request_id, PendingWrite{index, owner, request});
    }
    auto status = types::ocpp::SetVariableStatusEnumType::Rejected;
    try {
        status = this->connections.at(index)->set_value(request, request_id);
    } catch (const std::exception& e) {
        EVLOG_warning << "Write to " << request.component.name << "." << request.variable.name << " of owner " << owner
                      << " rejected: " << e.what();
    }

    auto pending = this->writes.handle();
    if (status == types::ocpp::SetVariableStatusEnumType::Accepted) {
        const auto self = std::this_thread::get_id();
        if (std::any_of(pending->handling.begin(), pending->handling.end(),
                        [self](const auto& thread) { return thread == self; })) {
            // Written while applying a message of a connection, which may hold up the messages of this connection: the
            // write result is applied after that message
            pending->pending.erase(request_id);
            return status;
        }
        // The connection published the write result before it returned. A release or detach, which waits for this
        // write, may be handled before the result and ends the wait.
        pending.wait_for(
            [&pending, &request_id, &owner]() {
                const auto& write = pending->pending.at(request_id);
                return write.applied or write.ended or pending->detached or pending->ending.count(owner) != 0;
            },
            write_result_timeout);
        if (not pending->pending.at(request_id).applied) {
            EVLOG_warning << "Write to " << request.component.name << "." << request.variable.name << " of owner "
                          << owner << " answered Rejected: the owner accepted it, but its write result was not applied";
            status = types::ocpp::SetVariableStatusEnumType::Rejected;
        }
    }
    pending->pending.erase(request_id);
    return status;
}

void ExternalOwners::end_writes_of(const std::string& owner) {
    {
        auto pending = this->writes.handle();
        pending->ending.insert(owner);
        for (auto& [request_id, write] : pending->pending) {
            if (write.owner == owner) {
                write.ended = true;
            }
        }
    }
    this->writes.notify_all();
}

void ExternalOwners::end_all_writes() {
    {
        auto pending = this->writes.handle();
        pending->detached = true;
        for (auto& [request_id, write] : pending->pending) {
            write.ended = true;
        }
    }
    this->writes.notify_all();
}

void ExternalOwners::report_held(ocpp::v2::DeviceModelRegistryInterface& registry, const std::size_t index,
                                 const std::string& owner, const ocpp::v2::ComponentRegistrationResult& result) {
    while (true) {
        std::vector<dm_types::ValueChange> held;
        {
            auto handle = this->state.handle();
            auto& owned = handle->owners.at(owner);
            held = std::exchange(owned.held_values, {});
            if (held.empty()) {
                owned.answered = true;
                --handle->registry_calls_in_progress;
                handle->outbox.emplace_back(index,
                                            make_result(owner, dm_types::RegistrationStatus::Accepted, {}, result));
                break;
            }
            // Changes arriving meanwhile are held behind these, so that none is overwritten by an older one
            handle->registry_calls_in_progress += held.size();
        }
        for (const auto& change : held) {
            this->report(registry, index, owner, change);
        }
    }
    this->state.notify_all();
}

void ExternalOwners::report(ocpp::v2::DeviceModelRegistryInterface& registry, const std::size_t index,
                            const std::string& owner, const dm_types::ValueChange& change) {
    // Without the lock: the device model evaluates the monitors of the variable and notifies its listeners
    auto status = ocpp::v2::SetVariableStatusEnum::Rejected;
    try {
        const auto component = to_ocpp_component(change.component);
        const auto variable = to_ocpp_variable(change.variable);
        const auto attribute = change.attribute_type.has_value() ? to_ocpp_attribute_enum(change.attribute_type.value())
                                                                 : ocpp::v2::AttributeEnum::Actual;
        status = registry.notify_value_changed(component, variable, attribute, change.value, owner);
        if (status != ocpp::v2::SetVariableStatusEnum::Accepted) {
            EVLOG_warning << "Value change of " << component << "." << variable << " of owner " << owner
                          << " ignored: " << status;
        }
    } catch (const std::exception& e) {
        EVLOG_warning << "Value change of " << change.component.name << "." << change.variable.name << " of owner "
                      << owner << " ignored: " << e.what();
    }
    {
        auto handle = this->state.handle();
        --handle->registry_calls_in_progress;
        const auto entry = handle->owners.find(owner);
        if (status == ocpp::v2::SetVariableStatusEnum::Accepted and entry != handle->owners.end() and
            entry->second.connection == index and not entry->second.releasing) {
            store(*handle, owner, change);
        }
    }
    this->state.notify_all();
}

ocpp::v2::ComponentRegistrationResult
ExternalOwners::register_owner(ocpp::v2::DeviceModelRegistryInterface& registry, Ocpp16KeyBinder* binder,
                               const std::string& owner, const std::vector<ocpp::v2::RegisteredComponent>& components,
                               const std::vector<DeclaredOcpp16Key>& keys,
                               const std::shared_ptr<ocpp::v2::VariableValueProvider>& provider) {
    if (binder == nullptr) {
        return registry.register_components(owner, components, provider);
    }
    const auto refused = binder->bind(owner, components, keys);
    ocpp::v2::ComponentRegistrationResult result;
    try {
        result = registry.register_components(owner, without(components, refused), provider);
    } catch (...) {
        binder->unbind(owner);
        throw;
    }
    binder->unbind_variables(owner, result.rejected);
    result.rejected.insert(result.rejected.end(), refused.begin(), refused.end());
    return result;
}

std::vector<OwnerRegistrationResult> ExternalOwners::register_with(ocpp::v2::DeviceModelRegistryInterface& registry,
                                                                   Ocpp16KeyBinder* binder) {
    std::vector<OwnerRegistrationResult> results;
    {
        auto handle = this->state.handle();
        for (std::size_t index = 0; index < this->connections.size(); ++index) {
            if (not handle->answered.at(index)) {
                this->replay_early(*handle, index);
            }
        }
        handle->registry = &registry;
        handle->binder = binder;
        this->writes.handle()->detached = false;
        std::vector<std::string> failed;
        for (auto& [owner, entry] : handle->owners) {
            try {
                const auto result =
                    register_owner(registry, binder, owner, entry.components, entry.keys, entry.provider);
                results.push_back({owner, result});
                if (not entry.answered) {
                    entry.answered = true;
                    handle->outbox.emplace_back(entry.connection,
                                                make_result(owner, dm_types::RegistrationStatus::Accepted, {}, result));
                }
            } catch (const std::exception& e) {
                EVLOG_error << "Registering owner " << owner << " failed, the owner is dropped: " << e.what();
                failed.push_back(owner);
                handle->outbox.emplace_back(entry.connection,
                                            make_result(owner, dm_types::RegistrationStatus::Rejected));
            }
        }
        for (const auto& owner : failed) {
            // A write never waits for the lock held here, so unregistering can wait for the writes to the owner
            this->end_writes_of(owner);
            try {
                registry.unregister_components(owner);
            } catch (const std::exception& e) {
                EVLOG_error << "Unregistering owner " << owner << " failed: " << e.what();
            }
            this->writes.handle()->ending.erase(owner);
            handle->owners.erase(owner);
        }
    }
    this->flush();
    return results;
}

void ExternalOwners::detach() {
    std::vector<std::string> owners;
    ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
    Ocpp16KeyBinder* binder{nullptr};
    {
        auto handle = this->state.handle();
        handle.wait([&handle]() { return handle->registry_calls_in_progress == 0; });
        registry = std::exchange(handle->registry, nullptr);
        binder = std::exchange(handle->binder, nullptr);
        if (registry == nullptr) {
            return;
        }
        for (const auto& [owner, entry] : handle->owners) {
            owners.push_back(owner);
        }
    }
    // Writes waiting for their write result stop waiting, which unregistering waits for
    this->end_all_writes();
    // Without the lock: unregistering waits for the writes to the owners in progress
    for (const auto& owner : owners) {
        try {
            registry->unregister_components(owner);
        } catch (const std::exception& e) {
            EVLOG_error << "Unregistering owner " << owner << " failed: " << e.what();
        }
        if (binder != nullptr) {
            binder->unbind(owner);
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
        return {owner, PendingResult{index, make_result(owner, dm_types::RegistrationStatus::OwnerInUse)},
                RegistryCall::None};
    }
    if (registration.components.empty()) {
        if (claimed != nullptr) {
            claimed->push_back(owner);
        }
        Applied applied{owner, PendingResult{index, make_result(owner, dm_types::RegistrationStatus::Accepted)},
                        RegistryCall::None};
        if (entry == state.owners.end()) {
            return applied;
        }
        if (state.registry == nullptr) {
            state.owners.erase(entry);
            EVLOG_info << "Owner " << owner << " released";
            return applied;
        }
        entry->second.releasing = true;
        ++state.registry_calls_in_progress;
        applied.call = RegistryCall::Unregister;
        return applied;
    }

    std::vector<dm_types::RejectedVariable> unconvertible;
    auto components = to_registered_components(owner, registration.components, unconvertible);
    if (not unconvertible.empty()) {
        EVLOG_warning << "Registration of owner " << owner << " by " << this->connections.at(index)->module_id()
                      << " is invalid: " << unconvertible.size() << " variables do not fit the OCPP types";
        return {owner, PendingResult{index, make_result(owner, dm_types::RegistrationStatus::Invalid, unconvertible)},
                RegistryCall::None};
    }
    if (claimed != nullptr) {
        claimed->push_back(owner);
    }

    auto& owned = state.owners[owner];
    owned.connection = index;
    owned.answered = false;
    owned.components = std::move(components);
    owned.keys = declared_keys(registration.components);
    if (owned.provider == nullptr) {
        owned.provider = std::make_shared<Provider>(*this, index, owner);
    }
    this->writes.handle()->ending.erase(owner);
    if (state.registry == nullptr) {
        return {owner, std::nullopt, RegistryCall::None};
    }
    ++state.registry_calls_in_progress;
    return {owner, std::nullopt, RegistryCall::Register};
}

void ExternalOwners::replay_early(State& state, const std::size_t index) {
    state.answered.at(index) = true;
    auto early = std::move(state.early.at(index));
    state.early.at(index).clear();
    for (const auto& message : early) {
        if (message.registration.has_value()) {
            auto applied = this->apply(state, index, message.registration.value(), nullptr);
            if (applied.result.has_value()) {
                state.outbox.push_back(std::move(applied.result.value()));
            }
            continue;
        }
        const auto& change = message.value_change.value();
        const auto owner = this->owner_of(index, change.owner);
        const auto entry = state.owners.find(owner);
        if (entry == state.owners.end() or entry->second.connection != index or not store(state, owner, change)) {
            EVLOG_warning << "Value change of " << change.component.name << "." << change.variable.name
                          << " ignored: owner " << owner << " did not register it";
        }
    }
}

bool ExternalOwners::store(State& state, const std::string& owner, const dm_types::ValueChange& change) {
    try {
        const auto attribute = change.attribute_type.has_value() ? to_ocpp_attribute_enum(change.attribute_type.value())
                                                                 : ocpp::v2::AttributeEnum::Actual;
        return store_value(state.owners.at(owner).components, to_ocpp_component(change.component),
                           to_ocpp_variable(change.variable), attribute, change.value);
    } catch (const std::exception& e) {
        EVLOG_warning << "Value change of " << change.component.name << "." << change.variable.name << " of owner "
                      << owner << " not kept for the next registration: " << e.what();
        return true;
    }
}

void ExternalOwners::unregister(ocpp::v2::DeviceModelRegistryInterface& registry, const std::string& owner) {
    // set while the registration or release counted in registry_calls_in_progress is in progress
    auto* const binder = this->state.handle()->binder;
    try {
        registry.unregister_components(owner);
    } catch (const std::exception& e) {
        EVLOG_error << "Unregistering owner " << owner << " failed: " << e.what();
    }
    // No write to the owner is in progress any more
    this->writes.handle()->ending.erase(owner);
    if (binder != nullptr) {
        binder->unbind(owner);
    }
    {
        auto handle = this->state.handle();
        const auto entry = handle->owners.find(owner);
        if (entry != handle->owners.end() and entry->second.releasing) {
            handle->owners.erase(entry);
        }
        --handle->registry_calls_in_progress;
    }
    this->state.notify_all();
}

void ExternalOwners::flush() {
    {
        auto handle = this->state.handle();
        if (handle->sending or handle->registry == nullptr) {
            return;
        }
        handle->sending = true;
    }
    while (true) {
        PendingResult next;
        {
            auto handle = this->state.handle();
            if (handle->outbox.empty() or handle->registry == nullptr) {
                handle->sending = false;
                return;
            }
            next = std::move(handle->outbox.front());
            handle->outbox.pop_front();
        }
        this->send(next);
    }
}

void ExternalOwners::send(const PendingResult& result) {
    const auto& [index, registration_result] = result;
    try {
        this->connections.at(index)->set_registration_result(registration_result);
    } catch (const std::exception& e) {
        EVLOG_warning << "Registration result for owner " << registration_result.owner << " not delivered to "
                      << this->connections.at(index)->module_id() << ": " << e.what();
    }
}

} // namespace ocpp_module_common::device_model
