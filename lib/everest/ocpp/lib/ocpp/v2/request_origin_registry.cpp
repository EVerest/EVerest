// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/request_origin_registry.hpp>

namespace ocpp {
namespace v2 {

void RequestOriginRegistry::remember(const RequestKind kind, const std::int32_t request_id,
                                     const ConnectionId& connection) {
    auto handle = this->origins.handle();
    (*handle)[Key{kind, request_id}] = connection;
}

std::optional<ConnectionId> RequestOriginRegistry::lookup(const RequestKind kind, const std::int32_t request_id,
                                                          const bool last) {
    auto handle = this->origins.handle();
    const auto it = handle->find(Key{kind, request_id});
    if (it == handle->end()) {
        return std::nullopt;
    }
    const auto connection = it->second;
    if (last) {
        handle->erase(it);
    }
    return connection;
}

void RequestOriginRegistry::forget(const RequestKind kind, const std::int32_t request_id) {
    this->origins.handle()->erase(Key{kind, request_id});
}

} // namespace v2
} // namespace ocpp
