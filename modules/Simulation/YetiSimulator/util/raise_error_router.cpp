// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "raise_error_router.hpp"

#include "errors.hpp"

#include <everest/logging.hpp>

namespace yeti_sim_router {

namespace {

Everest::error::Severity map_severity(everest::lib::API::V1_0::types::yeti_simulator::Severity s) {
    using ApiSev = everest::lib::API::V1_0::types::yeti_simulator::Severity;
    switch (s) {
    case ApiSev::Low:
        return Everest::error::Severity::Low;
    case ApiSev::Medium:
        return Everest::error::Severity::Medium;
    case ApiSev::High:
        return Everest::error::Severity::High;
    }
    EVLOG_warning << "Unknown yeti_simulator Severity enumerator; defaulting to High";
    return Everest::error::Severity::High;
}

} // namespace

bool route_raise(const everest::lib::API::V1_0::types::yeti_simulator::RaiseError& cmd, const PeerAdapters& peers) {
    const auto def_opt = lookup_error_definition(cmd.type);
    if (!def_opt.has_value()) {
        EVLOG_warning << "Unknown yeti_simulator error type: " << cmd.type;
        return false;
    }
    const auto& def = *def_opt;

    const std::string sub_type = cmd.sub_type.value_or(def.sub_type);
    const std::string message = cmd.message.value_or(def.message);
    const Everest::error::Severity severity = cmd.severity.has_value() ? map_severity(*cmd.severity) : def.severity;

    switch (def.error_target) {
    case ErrorTarget::BoardSupport: {
        const auto error = peers.make_board_support_error(cmd.type, sub_type, message, severity);
        peers.raise_board_support(error);
        break;
    }
    case ErrorTarget::ConnectorLock: {
        const auto error = peers.make_connector_lock_error(cmd.type, sub_type, message, severity);
        peers.raise_connector_lock(error);
        break;
    }
    case ErrorTarget::Rcd: {
        const auto error = peers.make_rcd_error(cmd.type, sub_type, message, severity);
        peers.raise_rcd(error);
        break;
    }
    case ErrorTarget::Powermeter: {
        const auto error = peers.make_powermeter_error(cmd.type, sub_type, message, severity);
        peers.raise_powermeter(error);
        break;
    }
    }
    return true;
}

bool route_clear(const everest::lib::API::V1_0::types::yeti_simulator::ClearError& cmd, const PeerAdapters& peers) {
    const auto def_opt = lookup_error_definition(cmd.type);
    if (!def_opt.has_value()) {
        EVLOG_warning << "Unknown yeti_simulator error type: " << cmd.type;
        return false;
    }
    const auto& def = *def_opt;
    const std::string sub_type = cmd.sub_type.value_or(std::string{def.sub_type});

    switch (def.error_target) {
    case ErrorTarget::BoardSupport:
        peers.clear_board_support(cmd.type, sub_type);
        break;
    case ErrorTarget::ConnectorLock:
        peers.clear_connector_lock(cmd.type, sub_type);
        break;
    case ErrorTarget::Rcd:
        peers.clear_rcd(cmd.type, sub_type);
        break;
    case ErrorTarget::Powermeter:
        peers.clear_powermeter(cmd.type, sub_type);
        break;
    }
    return true;
}

} // namespace yeti_sim_router
