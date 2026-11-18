// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>
#include <vector>

#include <ocpp/v2/device_model_registry_interface.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp_module_common::device_model {

/// \brief The OCPP 1.6 key that an owner declares for one of its variables
struct DeclaredOcpp16Key {
    ocpp::v2::Component component;
    ocpp::v2::Variable variable;
    std::string key;
};

/// \brief Maps the OCPP 1.6 keys that external owners declare for their variables to those variables, in OCPP 1.6
/// mode. Calls for different owners may run concurrently.
class Ocpp16KeyBinder {
public:
    virtual ~Ocpp16KeyBinder() = default;

    /// \brief Replaces the keys of \p owner with \p keys, declared for variables of \p components
    /// \returns The variables whose key is refused, with the reason Ocpp16KeyInUse or Ocpp16KeyInvalid
    virtual std::vector<ocpp::v2::RejectedVariable> bind(const std::string& owner,
                                                         const std::vector<ocpp::v2::RegisteredComponent>& components,
                                                         const std::vector<DeclaredOcpp16Key>& keys) = 0;

    /// \brief Removes the keys of the variables of \p owner in \p rejected, which the device model refused
    virtual void unbind_variables(const std::string& owner,
                                  const std::vector<ocpp::v2::RejectedVariable>& rejected) = 0;

    /// \brief Removes every key of \p owner
    virtual void unbind(const std::string& owner) = 0;
};

} // namespace ocpp_module_common::device_model
