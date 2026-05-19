// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "i_lifecycle_service_client.hpp"
#include <memory>

namespace everest::lifecycle_cli {

class CommandHandlers {
public:
    CommandHandlers(std::shared_ptr<ILifecycleServiceClient> client);

    void stop_modules();
    void start_modules();
    void monitor();

private:
    std::shared_ptr<ILifecycleServiceClient> m_client;
};

} // namespace everest::lifecycle_cli
