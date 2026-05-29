// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <string>

#include <utils/error.hpp>

#include <everest_api_types/yeti_simulator/API.hpp>

namespace yeti_sim_router {

/// Adapters that the router dispatches through.
/// Tests inject lambdas; production code wires the real p_* peers.
struct PeerAdapters {
    // Raise/clear per target
    std::function<void(const Everest::error::Error&)> raise_board_support;
    std::function<void(const std::string& type, const std::string& sub_type)> clear_board_support;

    std::function<void(const Everest::error::Error&)> raise_connector_lock;
    std::function<void(const std::string& type, const std::string& sub_type)> clear_connector_lock;

    std::function<void(const Everest::error::Error&)> raise_rcd;
    std::function<void(const std::string& type, const std::string& sub_type)> clear_rcd;

    std::function<void(const Everest::error::Error&)> raise_powermeter;
    std::function<void(const std::string& type, const std::string& sub_type)> clear_powermeter;

    // Error factory hooks per target; tests inject fakes, production wraps p_*->error_factory.
    std::function<Everest::error::Error(const std::string& type, const std::string& sub_type,
                                        const std::string& message, Everest::error::Severity)>
        make_board_support_error;
    std::function<Everest::error::Error(const std::string& type, const std::string& sub_type,
                                        const std::string& message, Everest::error::Severity)>
        make_connector_lock_error;
    std::function<Everest::error::Error(const std::string& type, const std::string& sub_type,
                                        const std::string& message, Everest::error::Severity)>
        make_rcd_error;
    std::function<Everest::error::Error(const std::string& type, const std::string& sub_type,
                                        const std::string& message, Everest::error::Severity)>
        make_powermeter_error;
};

/// Returns true when the type was recognized and dispatched; false on unknown type.
bool route_raise(const everest::lib::API::V1_0::types::yeti_simulator::RaiseError& cmd, const PeerAdapters& peers);

/// Returns true when the type was recognized and dispatched; false on unknown type.
bool route_clear(const everest::lib::API::V1_0::types::yeti_simulator::ClearError& cmd, const PeerAdapters& peers);

} // namespace yeti_sim_router
