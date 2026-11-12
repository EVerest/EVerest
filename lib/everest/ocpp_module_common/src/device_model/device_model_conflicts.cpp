// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/device_model_conflicts.hpp>

#include <sstream>
#include <stdexcept>

#include <everest/logging.hpp>

namespace ocpp_module_common::device_model {

namespace {
std::string name_of(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
    std::stringstream name;
    name << component << "." << variable;
    return name.str();
}
} // namespace

std::vector<std::string> check_device_model_conflicts(const std::vector<OwnerRegistrationResult>& results,
                                                      const bool fail_on_conflicts) {
    std::vector<std::string> conflicts;
    for (const auto& [owner, result] : results) {
        for (const auto& rejected : result.rejected) {
            if (rejected.reason != ocpp::v2::RegistrationRejectionReason::AlreadyDefined) {
                continue;
            }
            const auto name = name_of(rejected.component, rejected.variable);
            if (rejected.defined_by == ocpp::v2::VARIABLE_OWNER_COMPONENT_CONFIG) {
                conflicts.push_back(name + " is defined by the component config and registered by " + owner +
                                    ": remove it from the component config");
            } else if (rejected.defined_by == owner) {
                conflicts.push_back(name + " is registered twice by " + owner);
            } else {
                conflicts.push_back(name + " is registered by " + rejected.defined_by + " and by " + owner);
            }
        }
    }
    if (conflicts.empty()) {
        return conflicts;
    }

    std::stringstream message;
    message << "The device model has " << conflicts.size() << " conflict(s):";
    for (const auto& conflict : conflicts) {
        message << "\n - " << conflict;
    }
    EVLOG_error << message.str();
    if (fail_on_conflicts) {
        throw std::runtime_error(message.str() + "\nFailOnDeviceModelConflicts is set, so the module does not start");
    }
    return conflicts;
}

} // namespace ocpp_module_common::device_model
