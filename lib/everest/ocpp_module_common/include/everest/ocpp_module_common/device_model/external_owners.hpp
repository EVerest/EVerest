// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <everest/ocpp_module_common/device_model/device_model_registrations.hpp>
#include <everest/ocpp_module_common/device_model/ocpp16_key_binder.hpp>
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
    /// \brief Passes \p request on to the owner. Returns the owner's answer, after the connection published it as a
    /// write result with \p request_id in its owner messages.
    virtual types::ocpp::SetVariableStatusEnumType set_value(const types::ocpp_device_model::WriteRequest& request,
                                                             const std::string& request_id) = 0;
};

/// \brief Converts \p variable of a registration to the form the device model registers. An attribute without
/// mutability is ReadOnly, so that every write to it is rejected. Throws when a name or value is longer than its OCPP
/// string allows.
ocpp::v2::RegisteredVariable to_registered_variable(const types::ocpp_device_model::RegisteredVariable& variable);

types::ocpp_device_model::RejectedVariable to_everest_rejected_variable(const ocpp::v2::RejectedVariable& rejected);

/// \brief Whether \p owner can name an external owner: the owners that OCPPmulti and libocpp register cannot
bool is_external_owner(const std::string& owner);

/// \brief The owners that the modules connected through the ocpp_device_model interface act for. Assigns each owner
/// name to the connection that registered it first, until that connection releases it, registers the owners with a
/// device model, routes the writes to their variables to the owning connection and applies the owners' messages in
/// the order each connection publishes them, so that an owner's latest message wins. Registrations and value changes
/// arriving while no device model is attached are kept and applied when one is.
class ExternalOwners {
public:
    /// \brief How long a write the owner accepted waits for its write result to be applied. Longer than the longest
    /// time, cfg_request_reply_to_s of ocpp_device_model_API, for which a release of another owner of the connection
    /// can hold up the messages of the connection while it waits for a write to that owner.
    static constexpr std::chrono::seconds write_result_timeout{120};

    explicit ExternalOwners(std::vector<std::shared_ptr<DeviceModelOwnerConnection>> connections);
    ~ExternalOwners();
    ExternalOwners(const ExternalOwners&) = delete;
    ExternalOwners& operator=(const ExternalOwners&) = delete;
    ExternalOwners(ExternalOwners&&) = delete;
    ExternalOwners& operator=(ExternalOwners&&) = delete;

    /// \brief Takes the answer of get_registrations of the connection \p index. The registrations and value changes
    /// the connection published before are newer and are applied after it.
    void set_startup_registrations(std::size_t index, const types::ocpp_device_model::Registrations& registrations);

    /// \brief Applies a message that the connection \p index published. The messages of one connection must be
    /// applied one at a time and in the order they were published, which the framework ensures for the publications of
    /// one variable. A message that carries none or more than one of its parts is ignored.
    void on_owner_message(std::size_t index, const types::ocpp_device_model::OwnerMessage& message);

    /// \brief Applies a registration that the connection \p index published, see on_owner_message. A registration
    /// the device model fails to apply is answered Rejected, and its owner is dropped, which frees its name. A
    /// registration with a variable that does not fit the OCPP types is answered Invalid and changes nothing.
    void on_registration(std::size_t index, const types::ocpp_device_model::Registration& registration);

    /// \brief Reports a value that an owner of the connection \p index changed, see on_owner_message. A change for an
    /// owner that the connection does not hold is ignored. A change during the owner's registration is reported after
    /// it, in the order of arrival.
    void on_value_changed(std::size_t index, const types::ocpp_device_model::ValueChange& change);

    /// \brief Applies the answer of an owner of the connection \p index to a write, see on_owner_message: an Accepted
    /// value is reported like a value change, then the write waiting for the result returns.
    void on_write_result(std::size_t index, const types::ocpp_device_model::WriteResult& result);

    /// \brief Registers every owner with \p registry and answers each registration not answered yet. Connections
    /// whose startup registrations are missing count as having none. An owner whose registration fails is logged and
    /// dropped, which frees its name. Call detach() before \p registry or \p binder is destroyed.
    /// \param binder Maps the OCPP 1.6 keys that the owners declare, from now on until detach(); without one the keys
    /// are ignored. A variable whose key it refuses is not registered.
    /// \returns The result of each owner's registration
    std::vector<OwnerRegistrationResult> register_with(ocpp::v2::DeviceModelRegistryInterface& registry,
                                                       Ocpp16KeyBinder* binder = nullptr);

    /// \brief Unregisters the owners from the attached registry, which keeps their monitors for the next
    /// registration, and removes their OCPP 1.6 keys, once the registrations, releases and value reports in progress
    /// finished. The owners and their registrations are kept for the next register_with. Writes waiting for their write
    /// result return without it.
    void detach();

private:
    class Provider;

    struct Owner {
        std::size_t connection;
        std::vector<ocpp::v2::RegisteredComponent> components;
        std::vector<DeclaredOcpp16Key> keys;
        std::shared_ptr<ocpp::v2::VariableValueProvider> provider;
        /// \brief Being unregistered after a release or a failed registration; the name stays in use until then
        bool releasing{false};
        /// \brief Whether the registration in components has been answered, which happens once the changes held
        /// during it are reported
        bool answered{false};
        /// \brief The latest change of each attribute that arrived while the registration was being applied or its
        /// held changes were being reported, oldest first
        std::vector<types::ocpp_device_model::ValueChange> held_values;
    };

    using PendingResult = std::pair<std::size_t, types::ocpp_device_model::RegistrationResult>;

    /// \brief A write passed on to an owner, until its write result is applied or the write stops waiting for it
    struct PendingWrite {
        std::size_t connection;
        std::string owner;
        types::ocpp_device_model::WriteRequest request;
        bool applied{false};
        /// \brief Set when the owner is released or dropped, or the registry detached, which wait for this write
        bool ended{false};
    };

    /// \brief The writes in progress, apart from State so that a write never waits for the lock of State, which is
    /// held while the registry waits for the writes in progress
    struct Writes {
        std::map<std::string, PendingWrite> pending;
        /// \brief Random, so that the request ids of the OCPP modules that share a connection differ
        std::string request_id_prefix;
        std::uint64_t last_request_id{0};
        /// \brief Per connection, the thread applying one of its messages, whose write results wait behind it
        std::vector<std::optional<std::thread::id>> handling;
        /// \brief Owners being released or dropped, whose writes do not wait for their results
        std::set<std::string> ending;
        bool detached{false};
    };

    struct State {
        ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
        Ocpp16KeyBinder* binder{nullptr};
        std::map<std::string, Owner> owners;
        std::vector<bool> answered;
        /// \brief Per connection, the registrations and value changes published before its startup registrations
        /// arrived, in the order they were published: per owner the latest registration and the latest change of each
        /// attribute after it
        std::vector<std::vector<types::ocpp_device_model::OwnerMessage>> early;
        /// \brief Results in the order they were decided, sent while a registry is attached
        std::deque<PendingResult> outbox;
        /// \brief Whether a thread is sending the outbox
        bool sending{false};
        /// \brief The number of calls to the registry made without the lock, which detach() waits for
        std::size_t registry_calls_in_progress{0};
    };

    /// \brief The registry call that applying a registration leaves to be made without the lock
    enum class RegistryCall {
        None,
        Register,
        Unregister,
    };

    struct Applied {
        std::string owner;
        /// \brief For a call, the result to send once it returned
        std::optional<PendingResult> result;
        RegistryCall call{RegistryCall::None};
    };

    std::string owner_of(std::size_t index, const std::optional<std::string>& owner) const;
    /// \brief Whether \p kept changes the same attribute of \p owner as \p change, with \p module_id as the owner of a
    /// change that names none
    static bool is_same_attribute(const std::string& owner, const std::string& module_id,
                                  const types::ocpp_device_model::ValueChange& kept,
                                  const types::ocpp_device_model::ValueChange& change);
    /// \brief Adds \p change to \p changes, replacing an earlier change of the same attribute
    void keep_latest(std::vector<types::ocpp_device_model::ValueChange>& changes, std::size_t index,
                     const types::ocpp_device_model::ValueChange& change) const;
    /// \brief Keeps \p message, published before the startup registrations of the connection \p index arrived
    void keep_early(State& state, std::size_t index, const types::ocpp_device_model::OwnerMessage& message) const;
    /// \brief Passes a write to \p owner on to the connection \p index, and waits for its write result if the owner
    /// accepted it, see Provider
    types::ocpp::SetVariableStatusEnumType write(std::size_t index, const std::string& owner,
                                                 const types::ocpp_device_model::WriteRequest& request);
    /// \brief Ends the wait of the writes to \p owner for their results, before \p owner is unregistered
    void end_writes_of(const std::string& owner);
    /// \brief Ends the wait of all writes for their results, before the owners are unregistered for a detach
    void end_all_writes();
    /// \brief Applies \p registration to \p state, leaving the call to an attached registry to the caller
    /// \param claimed The owners already registered by the same get_registrations answer
    Applied apply(State& state, std::size_t index, const types::ocpp_device_model::Registration& registration,
                  std::vector<std::string>* claimed);
    /// \brief Marks the startup registrations of the connection \p index as arrived and applies the registrations and
    /// value changes it published before, in order
    void replay_early(State& state, std::size_t index);
    /// \brief Keeps \p change in the registration of its owner for the next register_with
    /// \returns false if the owner did not register the attribute
    static bool store(State& state, const std::string& owner, const types::ocpp_device_model::ValueChange& change);
    /// \brief Reports the changes held for \p owner during its registration, which \p result answers once none is
    /// left, and ends the registration's call counted in registry_calls_in_progress
    void report_held(ocpp::v2::DeviceModelRegistryInterface& registry, std::size_t index, const std::string& owner,
                     const ocpp::v2::ComponentRegistrationResult& result);
    /// \brief Reports \p change of \p owner to \p registry without the lock, and keeps it for the next registration
    /// when accepted. The caller counted the call in registry_calls_in_progress.
    void report(ocpp::v2::DeviceModelRegistryInterface& registry, std::size_t index, const std::string& owner,
                const types::ocpp_device_model::ValueChange& change);
    /// \brief Binds the keys of \p owner with \p binder, if given, and registers the components whose key is not
    /// refused with \p registry, whose result then lists the refused variables too. Without the lock.
    /// \throws What the registry throws, after removing the keys of \p owner
    static ocpp::v2::ComponentRegistrationResult
    register_owner(ocpp::v2::DeviceModelRegistryInterface& registry, Ocpp16KeyBinder* binder, const std::string& owner,
                   const std::vector<ocpp::v2::RegisteredComponent>& components,
                   const std::vector<DeclaredOcpp16Key>& keys,
                   const std::shared_ptr<ocpp::v2::VariableValueProvider>& provider);
    /// \brief Unregisters \p owner, which is marked releasing, from \p registry, removes its keys and drops it
    void unregister(ocpp::v2::DeviceModelRegistryInterface& registry, const std::string& owner);
    /// \brief Sends the outbox while a registry is attached, unless another thread is sending it already
    void flush();
    void send(const PendingResult& result);

    std::vector<std::shared_ptr<DeviceModelOwnerConnection>> connections;
    everest::lib::util::monitor<State> state;
    everest::lib::util::monitor<Writes> writes;
};

} // namespace ocpp_module_common::device_model
