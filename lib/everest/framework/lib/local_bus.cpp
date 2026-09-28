// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <framework/local_bus.hpp>

#include <everest/logging.hpp>

namespace Everest {

ModuleLogScope::ModuleLogScope(const std::string& name) : m_previous(Logging::get_thread_process_name()) {
    Logging::set_thread_process_name(name);
}

ModuleLogScope::~ModuleLogScope() {
    Logging::set_thread_process_name(m_previous);
}

} // namespace Everest
