// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "lifecycle_service_client_ifc.hpp"
#include <memory>

namespace everest::lifecycle_cli {

class CommandHandlers {
public:
    CommandHandlers(std::shared_ptr<LifecycleServiceClientIfc> client);

    void stop_modules();
    void start_modules();
    void get_everest_version();
    void monitor();

private:
    std::shared_ptr<LifecycleServiceClientIfc> m_client;
};

} // namespace everest::lifecycle_cli
