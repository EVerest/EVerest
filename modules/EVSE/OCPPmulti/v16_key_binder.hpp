// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <everest/ocpp_module_common/device_model/ocpp16_key_binder.hpp>
#include <everest/util/async/monitor.hpp>
#include <ocpp/v16/variable_resolver.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/ocpp16_key_mappings.hpp>

namespace ocpp_multi {

/// \brief Maps the OCPP 1.6 keys that external owners declare to their variables, as owner keys of \p resolver.
///
/// The key mapping file reserves every key and every target it names, whether or not its entry is usable: a key the
/// file maps to another variable, and a variable the file maps to another key, are refused with Ocpp16KeyInUse. A key
/// the file maps to the same variable is left to the file. Otherwise a key must follow the rules of the key mapping
/// file (see ocpp::v2::find_key_mapping_conflict and ocpp::v2::find_mapping_target_problem), and between owners the
/// first one to bind a key keeps it.
class V16KeyBinder final : public ocpp_module_common::device_model::Ocpp16KeyBinder {
public:
    /// \param resolver The key mapping of the OCPP 1.6 configuration; must outlive this object
    /// \param device_model The device model of the OCPP 1.6 configuration; must outlive this object
    /// \param file_mappings The entries of the key mapping file, as loaded, before any was found unusable
    V16KeyBinder(ocpp::v16::VariableResolver& resolver, ocpp::v2::DeviceModelVariablesInterface& device_model,
                 ocpp::v2::Ocpp16KeyMappings file_mappings);

    std::vector<ocpp::v2::RejectedVariable>
    bind(const std::string& owner, const std::vector<ocpp::v2::RegisteredComponent>& components,
         const std::vector<ocpp_module_common::device_model::DeclaredOcpp16Key>& keys) override;
    void unbind_variables(const std::string& owner, const std::vector<ocpp::v2::RejectedVariable>& rejected) override;
    void unbind(const std::string& owner) override;

private:
    using Target = std::pair<ocpp::v2::Component, ocpp::v2::Variable>;
    /// \brief The key of a variable that an owner holds, per target
    using Bound = std::map<Target, std::string>;

    /// \brief Why \p key cannot be mapped to the variable \p registered of \p target, with the rejection reason
    std::optional<std::pair<ocpp::v2::RegistrationRejectionReason, std::string>>
    find_refusal(const std::string& key, const Target& target, const ocpp::v2::RegisteredVariable& registered) const;
    void remove(Bound& bound);

    ocpp::v16::VariableResolver& resolver;
    ocpp::v2::DeviceModelVariablesInterface& device_model;
    ocpp::v2::Ocpp16KeyMappings file_mappings;
    std::map<Target, std::string> file_key_by_target;
    everest::lib::util::monitor<std::map<std::string, Bound>> bound_by_owner;
};

} // namespace ocpp_multi
