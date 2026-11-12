// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/common/connectivity_manager_configuration.hpp>
#include <ocpp/v2/device_model_base.hpp>
#include <ocpp/v2/device_model_interface.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelAbstract : public DeviceModelBase,
                            public DeviceModelInterface,
                            public ocpp::ConnectivityManagerConfiguration {
public:
    /// \brief DeviceModelBase has a private get_variable of the same signature, which one override serves as well
    using DeviceModelInterface::get_variable;
};

} // namespace v2
} // namespace ocpp
