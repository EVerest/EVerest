// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <everest/ocpp_module_common/device_model/device_model_registrations.hpp>
#include <everest/util/async/monitor.hpp>
#include <generated/types/ocpp.hpp>
#include <generated/types/ocpp_device_model.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>

namespace ocpp_module_common::device_model {

/// \brief A module that provides the ocpp_device_model interface
class DeviceModelOwnerConnection {
public:
    virtual ~DeviceModelOwnerConnection() = default;
    /// \brief The owner of a registration or a value change that names none
    virtual std::string module_id() const = 0;
    virtual void set_registration_result(const types::ocpp_device_model::RegistrationResult& result) = 0;
    virtual types::ocpp::SetVariableStatusEnumType set_value(const types::ocpp_device_model::WriteRequest& request) = 0;
};

/// \brief Converts \p variable of a registration to the form the device model registers. An attribute without
/// mutability is ReadOnly, so that every write to it is rejected. Throws when a name or value is longer than its OCPP
/// string allows.
ocpp::v2::RegisteredVariable to_registered_variable(const types::ocpp_device_model::RegisteredVariable& variable);

types::ocpp_device_model::RejectedVariable to_everest_rejected_variable(const ocpp::v2::RejectedVariable& rejected);

/// \brief The owners that the modules connected through the ocpp_device_model interface act for. Assigns each owner
/// name to the connection that registered it first, until that connection releases it, registers the owners with a
/// device model, routes the writes to their variables to the owning connection and reports the values the owners
/// change. Registrations and value changes arriving while no device model is attached are kept and applied when one
/// is.
class ExternalOwners {
public:
    explicit ExternalOwners(std::vector<std::shared_ptr<DeviceModelOwnerConnection>> connections);
    ~ExternalOwners();
    ExternalOwners(const ExternalOwners&) = delete;
    ExternalOwners& operator=(const ExternalOwners&) = delete;
    ExternalOwners(ExternalOwners&&) = delete;
    ExternalOwners& operator=(ExternalOwners&&) = delete;

    /// \brief Takes the answer of get_registrations of the connection \p index. The registrations the connection
    /// published before are newer and are applied after it.
    void set_startup_registrations(std::size_t index, const types::ocpp_device_model::Registrations& registrations);

    /// \brief Applies a registration that the connection \p index published. Registrations of one connection must
    /// not be applied concurrently, which the framework ensures for the publications of one variable.
    void on_registration(std::size_t index, const types::ocpp_device_model::Registration& registration);

    /// \brief Reports a value that an owner of the connection \p index changed. A change for an owner of another
    /// connection is ignored.
    void on_value_changed(std::size_t index, const types::ocpp_device_model::ValueChange& change);

    /// \brief Registers every owner with \p registry and answers each registration not answered yet. Connections
    /// whose startup registrations are missing count as having none. Call detach() before \p registry is destroyed.
    /// \returns The result of each owner's registration
    std::vector<OwnerRegistrationResult> register_with(ocpp::v2::DeviceModelRegistryInterface& registry);

    /// \brief Unregisters the owners from the attached registry, which keeps their monitors for the next
    /// registration, once the releases in progress finished. The owners and their registrations are kept for the
    /// next register_with.
    void detach();

private:
    struct Owner {
        std::size_t connection;
        std::vector<ocpp::v2::RegisteredComponent> components;
        /// \brief The variables of the registration that do not fit the OCPP types, rejected with InvalidNameKey
        std::vector<types::ocpp_device_model::RejectedVariable> unconvertible;
        std::shared_ptr<ocpp::v2::VariableValueProvider> provider;
        /// \brief Being unregistered after its connection released it; the name stays in use until then
        bool releasing{false};
    };

    using PendingResult = std::pair<std::size_t, types::ocpp_device_model::RegistrationResult>;

    struct State {
        ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
        std::map<std::string, Owner> owners;
        std::vector<bool> answered;
        /// \brief Per connection, the registrations published before its startup registrations arrived
        std::vector<std::vector<types::ocpp_device_model::Registration>> early;
        /// \brief Results that are sent once a registry is attached
        std::vector<PendingResult> pending_results;
        /// \brief The number of releases that unregister an owner from the registry without the lock
        std::size_t releases_in_progress{0};
    };

    /// \brief What applying a registration leaves to do without the lock
    struct Applied {
        std::optional<PendingResult> result;
        /// \brief The registry to unregister a released owner from, set when the owner was marked releasing
        ocpp::v2::DeviceModelRegistryInterface* release_from{nullptr};
    };

    std::string owner_of(std::size_t index, const std::optional<std::string>& owner) const;
    /// \brief Applies \p registration to \p state, and to the registry if one is attached. The release of an
    /// attached owner is completed by the caller, see Applied::release_from.
    /// \param claimed The owners already registered by the same get_registrations answer
    Applied apply(State& state, std::size_t index, const types::ocpp_device_model::Registration& registration,
                  std::vector<std::string>* claimed);
    /// \brief Marks the startup registrations of the connection \p index as arrived and applies the registrations it
    /// published before
    void replay_early(State& state, std::size_t index);
    void send(const std::vector<PendingResult>& results);

    std::vector<std::shared_ptr<DeviceModelOwnerConnection>> connections;
    everest::lib::util::monitor<State> state;
};

} // namespace ocpp_module_common::device_model
