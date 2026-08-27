// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "opcp_certificate_managerImpl.hpp"

#include <everest/logging.hpp>

namespace module {
namespace main {

void opcp_certificate_managerImpl::init() {
}

void opcp_certificate_managerImpl::ready() {
}

void opcp_certificate_managerImpl::shutdown() {
}

bool opcp_certificate_managerImpl::handle_renew_leaf_certificates(bool& force) {
    auto* manager = mod->manager();
    if (manager == nullptr) {
        EVLOG_warning << "renew_leaf_certificates refused: module is misconfigured";
        return false;
    }
    return manager->request_renewal(force);
}

bool opcp_certificate_managerImpl::handle_sync_root_certificates() {
    auto* manager = mod->manager();
    if (manager == nullptr) {
        EVLOG_warning << "sync_root_certificates refused: module is misconfigured";
        return false;
    }
    return manager->request_root_sync();
}

} // namespace main
} // namespace module
