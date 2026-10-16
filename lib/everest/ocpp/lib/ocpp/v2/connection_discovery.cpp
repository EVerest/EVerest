// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_discovery.hpp>

#include <algorithm>
#include <set>
#include <string>

#include <everest/logging.hpp>

#include <ocpp/common/utils.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_abstract.hpp>

namespace ocpp {
namespace v2 {

namespace {

constexpr const char* OCPP_COMM_CTRLR = "OCPPCommCtrlr";
constexpr const char* NETWORK_CONFIGURATION = "NetworkConfiguration";

/// Reads the priority string of \p connection and turns it into slot numbers, dropping tokens that
/// are not integers.
std::vector<std::int32_t> read_priority_slots(DeviceModelAbstract& device_model, const ConnectionId& connection) {
    const auto cv = ConnectionComponentVariables::get_component_variable(
        connection, ConnectionComponentVariables::NetworkConfigurationPriority);
    const auto priority = device_model.get_optional_value<std::string>(cv).value_or("");

    std::vector<std::int32_t> slots;
    for (const auto& token : ocpp::split_string(priority, ',', true)) {
        if (token.empty()) {
            continue;
        }
        try {
            slots.push_back(std::stoi(token));
        } catch (const std::exception& e) {
            EVLOG_warning << connection.to_string() << ": ignoring non-integer network configuration slot '" << token
                          << "': " << e.what();
        }
    }
    return slots;
}

/// Slots that exist as a NetworkConfiguration component instance. A slot without one cannot be
/// dialled, because there is nowhere to read its URL and security profile from.
std::set<std::int32_t> read_existing_slots(DeviceModelAbstract& device_model) {
    std::set<std::int32_t> slots;
    for (const auto& instance : device_model.get_component_instances(NETWORK_CONFIGURATION)) {
        try {
            slots.insert(std::stoi(instance));
        } catch (const std::exception&) {
            EVLOG_warning << "Ignoring NetworkConfiguration instance '" << instance << "' that is not a slot number";
        }
    }
    return slots;
}

/// Keeps the slots of \p connection that exist and are not already claimed, recording the ones it
/// keeps in \p claimed so that a later connection cannot claim them too.
std::vector<std::int32_t> claim_slots(const ConnectionId& connection, const std::vector<std::int32_t>& wanted,
                                      const std::set<std::int32_t>& existing_slots, std::set<std::int32_t>& claimed) {
    std::vector<std::int32_t> slots;
    for (const auto slot : wanted) {
        if (existing_slots.find(slot) == existing_slots.end()) {
            EVLOG_error << connection.to_string() << ": ignoring network configuration slot " << slot
                        << " because no NetworkConfiguration component exists for it";
            continue;
        }
        if (!claimed.insert(slot).second) {
            // Section 3.4.1: the slot sets of the connections must not overlap. Dropping the slot
            // from the later connection keeps both connections usable and leaves the primary's
            // slots untouched, rather than refusing to start.
            EVLOG_error << connection.to_string() << ": ignoring network configuration slot " << slot
                        << " because another OCPP connection already uses it";
            continue;
        }
        slots.push_back(slot);
    }
    return slots;
}

} // namespace

std::vector<ConnectionInfo> discover_connections(DeviceModelAbstract& device_model) {
    const auto existing_slots = read_existing_slots(device_model);
    std::set<std::int32_t> claimed_slots;

    const auto read_active_slot = [&device_model](const ConnectionId& connection) {
        return device_model.get_optional_value<std::int32_t>(ConnectionComponentVariables::get_component_variable(
            connection, ConnectionComponentVariables::ActiveNetworkProfile));
    };

    // The primary connection always exists and claims its slots first, so a misconfigured secondary
    // can never take a slot away from it.
    ConnectionInfo primary;
    primary.id = ConnectionId::primary();
    primary.enabled = true;
    primary.network_configuration_slots =
        claim_slots(primary.id, read_priority_slots(device_model, primary.id), existing_slots, claimed_slots);
    primary.active_network_profile_slot = read_active_slot(primary.id);

    std::vector<ConnectionInfo> connections{primary};

    // Sorted, so slot claiming between two secondaries that want the same slot is deterministic.
    auto instances = device_model.get_component_instances(OCPP_COMM_CTRLR);
    std::sort(instances.begin(), instances.end());

    for (const auto& instance : instances) {
        if (!ConnectionId::is_valid_instance_name(instance)) {
            EVLOG_error << "Ignoring OCPPCommCtrlr instance '" << instance << "': not a usable OCPP connection name";
            continue;
        }
        const auto id = ConnectionId::secondary(instance);

        const auto enabled = device_model
                                 .get_optional_value<bool>(ConnectionComponentVariables::get_component_variable(
                                     id, ConnectionComponentVariables::Enabled))
                                 .value_or(false);
        if (!enabled) {
            EVLOG_info << id.to_string() << " is configured but not enabled";
            continue;
        }

        const auto wanted = read_priority_slots(device_model, id);
        if (wanted.empty()) {
            EVLOG_error << id.to_string()
                        << " is enabled but has no NetworkConfigurationPriority, so there is nothing to connect to";
            continue;
        }

        ConnectionInfo secondary;
        secondary.id = id;
        secondary.enabled = true;
        secondary.network_configuration_slots = claim_slots(id, wanted, existing_slots, claimed_slots);
        if (secondary.network_configuration_slots.empty()) {
            EVLOG_error << id.to_string() << " has no usable network configuration slot left; ignoring it";
            continue;
        }
        secondary.active_network_profile_slot = read_active_slot(id);

        std::string slot_list;
        for (const auto slot : secondary.network_configuration_slots) {
            slot_list += (slot_list.empty() ? "" : ",") + std::to_string(slot);
        }
        EVLOG_info << "Discovered secondary OCPP connection " << id.to_string() << " on network configuration slot(s) "
                   << slot_list;
        connections.push_back(std::move(secondary));
    }

    return connections;
}

} // namespace v2
} // namespace ocpp
