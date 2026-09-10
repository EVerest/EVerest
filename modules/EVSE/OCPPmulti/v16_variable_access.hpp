// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include <ocpp/v16/messages/GetConfiguration.hpp>
#include <ocpp/v16/ocpp_enums.hpp>
#include <ocpp/v16/variable_resolver.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/ocpp_types.hpp>

#include "generic_chargepoint_interface.hpp"
#include "v16_connection_config_validator.hpp"

namespace ocpp_multi {

/// \brief Routes v2-shaped get/set variable requests in v16 mode: legacy keys and key-backed
///        CVs go through the 1.6 chargepoint, unmapped CVs hit the shared device model.
class V16VariableAccess {
public:
    using get_keys_fn = std::function<ocpp::v16::GetConfigurationResponse(const std::vector<ocpp::CiString<50>>&)>;
    using set_key_fn =
        std::function<ocpp::v16::ConfigurationStatus(const ocpp::CiString<50>&, const ocpp::CiString<500>&)>;
    /// \brief Invoked at most once per set() batch when a committed write touched a connection-config CV
    ///        (NetworkConfiguration components, network priority/active slot, SecurityCtrlr credentials), so the
    ///        owner can refresh the OCPP1.6 stack's cached network profiles - mirroring the 2.x SetVariables path.
    using on_connection_config_changed_fn = std::function<void()>;

    V16VariableAccess(const ocpp::v16::VariableResolver& resolver, ocpp::v2::DeviceModelInterface& device_model,
                      get_keys_fn get_keys, set_key_fn set_key,
                      on_connection_config_changed_fn on_connection_config_changed = nullptr);

    std::vector<ocpp::v2::GetVariableResult> get(const std::vector<ocpp::v2::GetVariableData>& requests);
    std::vector<SetVariableOutcome> set(const std::vector<ocpp::v2::SetVariableData>& requests,
                                        const std::string& source);

    /// \brief Keys a deprecation warning was already emitted for (test accessor).
    const std::set<std::string>& warned_keys() const {
        return m_warned_keys;
    }

private:
    void warn_deprecated_key(const std::string& key);
    void clear_active_slot_override(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable);

    const ocpp::v16::VariableResolver& m_resolver;
    ocpp::v2::DeviceModelInterface& m_device_model;
    V16ConnectionConfigValidator m_connection_config_validator;
    get_keys_fn m_get_keys;
    set_key_fn m_set_key;
    on_connection_config_changed_fn m_on_connection_config_changed;
    std::set<std::string> m_warned_keys;
};

} // namespace ocpp_multi
