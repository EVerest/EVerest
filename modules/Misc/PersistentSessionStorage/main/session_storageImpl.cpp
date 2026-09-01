// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "session_storageImpl.hpp"

namespace module {
namespace main {

void session_storageImpl::init() {
}

void session_storageImpl::ready() {
}

types::session_storage::SessionList
session_storageImpl::handle_get_sessions(types::session_storage::GetSessionsRequest& request) {
    return mod->store().get_sessions(request);
}

types::session_storage::SessionResult
session_storageImpl::handle_get_session(types::session_storage::SessionIdentifier& identifier) {
    types::session_storage::SessionResult result{};
    result.session = mod->store().get_session(identifier);
    return result;
}

types::session_storage::ClearSessionsResult session_storageImpl::handle_clear_sessions() {
    types::session_storage::ClearSessionsResult result{};
    result.cleared = mod->store().clear_sessions();
    return result;
}

} // namespace main
} // namespace module
