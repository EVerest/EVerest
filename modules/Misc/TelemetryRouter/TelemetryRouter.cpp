// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "TelemetryRouter.hpp"

#include <filesystem>
#include <stdexcept>

#include <sys/socket.h>

#include <everest/logging.hpp>
#include <everest/utils/yaml_loader.hpp>
#include <fmt/format.h>

#include <utils/telemetry/transport.hpp>

namespace module {

namespace {

using telemetry_router::OcppWrite;
using telemetry_router::OcppWriteStatus;

types::ocpp::ComponentVariable to_component_variable(const OcppWrite& write) {
    types::ocpp::ComponentVariable component_variable;
    component_variable.component.name = write.component_name;
    component_variable.component.instance = write.component_instance;
    if (write.evse.has_value()) {
        component_variable.component.evse = types::ocpp::EVSE{write.evse.value(), write.connector};
    }
    component_variable.variable.name = write.variable_name;
    component_variable.variable.instance = write.variable_instance;
    return component_variable;
}

bool same_address(const types::ocpp::ComponentVariable& lhs, const types::ocpp::ComponentVariable& rhs) {
    const auto evse_id = [](const types::ocpp::Component& component) {
        return component.evse.has_value() ? std::optional<int>(component.evse->id) : std::nullopt;
    };
    const auto connector_id = [](const types::ocpp::Component& component) {
        return component.evse.has_value() ? component.evse->connector_id : std::nullopt;
    };
    return lhs.component.name == rhs.component.name and lhs.component.instance == rhs.component.instance and
           evse_id(lhs.component) == evse_id(rhs.component) and
           connector_id(lhs.component) == connector_id(rhs.component) and lhs.variable.name == rhs.variable.name and
           lhs.variable.instance == rhs.variable.instance;
}

OcppWriteStatus to_write_status(types::ocpp::SetVariableStatusEnumType status) {
    switch (status) {
    case types::ocpp::SetVariableStatusEnumType::Accepted:
        return OcppWriteStatus::Accepted;
    case types::ocpp::SetVariableStatusEnumType::Rejected:
        return OcppWriteStatus::Rejected;
    case types::ocpp::SetVariableStatusEnumType::UnknownComponent:
        return OcppWriteStatus::UnknownComponent;
    case types::ocpp::SetVariableStatusEnumType::UnknownVariable:
        return OcppWriteStatus::UnknownVariable;
    case types::ocpp::SetVariableStatusEnumType::NotSupportedAttributeType:
        return OcppWriteStatus::NotSupportedAttributeType;
    case types::ocpp::SetVariableStatusEnumType::RebootRequired:
        return OcppWriteStatus::RebootRequired;
    }
    return OcppWriteStatus::Rejected;
}

class OcppClient : public telemetry_router::IOcppClient {
public:
    OcppClient(ocppIntf& ocpp, std::string source) : m_ocpp(ocpp), m_source(std::move(source)) {
    }

    std::vector<OcppWriteStatus> set_variables(const std::vector<OcppWrite>& writes) override {
        std::vector<types::ocpp::SetVariableRequest> requests;
        requests.reserve(writes.size());
        for (const auto& write : writes) {
            types::ocpp::SetVariableRequest request;
            request.component_variable = to_component_variable(write);
            request.value = write.value;
            request.attribute_type = types::ocpp::AttributeEnum::Actual;
            requests.push_back(std::move(request));
        }

        const auto results = m_ocpp.call_set_variables(requests, m_source);

        std::vector<OcppWriteStatus> statuses(writes.size(), OcppWriteStatus::Rejected);
        for (std::size_t i = 0; i < requests.size(); ++i) {
            for (const auto& result : results) {
                if (same_address(result.component_variable, requests[i].component_variable)) {
                    statuses[i] = to_write_status(result.status);
                    break;
                }
            }
        }
        return statuses;
    }

private:
    ocppIntf& m_ocpp;
    std::string m_source;
};

std::filesystem::path resolve_rules_file(const std::string& rules_file, const std::filesystem::path& share_dir) {
    const std::filesystem::path path(rules_file);
    return path.is_absolute() ? path : share_dir / path;
}

} // namespace

void TelemetryRouter::init() {
    invoke_init(*p_main);

    const auto bound_path = everest::telemetry::bound_receiver_socket_path(everest::telemetry::RECEIVER_FD);
    if (not bound_path.has_value()) {
        throw std::runtime_error(
            "TelemetryRouter did not inherit the telemetry socket; it must be started by a manager "
            "with settings.telemetry_socket_enabled");
    }

    const auto rules_path = resolve_rules_file(config.rules_file, info.paths.share);
    telemetry_router::RulesConfig rules;
    try {
        rules = telemetry_router::parse_rules(nlohmann::json(Everest::load_yaml(rules_path)));
    } catch (const std::exception& e) {
        throw std::runtime_error(fmt::format("Invalid telemetry rules file {}: {}", rules_path.string(), e.what()));
    }

    telemetry_router::ElementCatalog catalog({static_cast<std::size_t>(config.max_dynamic_producers),
                                              static_cast<std::size_t>(config.max_dynamic_elements)});
    catalog.seed(get_telemetry_catalog());

    if (not r_ocpp.empty()) {
        m_ocpp_client = std::make_unique<OcppClient>(*r_ocpp.at(0), info.id);
    }
    telemetry_router::SinkEnvironment environment;
    environment.ocpp_client = m_ocpp_client.get();
    environment.ocpp_flush_interval_ms = config.ocpp_flush_interval_ms;
    environment.ocpp_min_interval_s = config.ocpp_min_interval_s;

    try {
        m_router = std::make_unique<telemetry_router::Router>(std::move(catalog), std::move(rules),
                                                              telemetry_router::default_sink_factories(), environment);
        for (const auto& problem : m_router->bind_all(config.strict_rules)) {
            EVLOG_warning << "Telemetry rules: " << problem;
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(fmt::format("Invalid telemetry rules file {}:\n{}", rules_path.string(), e.what()));
    }

    std::size_t element_count = 0;
    for (const auto& [id, producer] : m_router->catalog().producers()) {
        element_count += producer.elements.size();
    }
    EVLOG_info << fmt::format("Routing {} telemetry elements of {} modules received on {}", element_count,
                              m_router->catalog().producers().size(), bound_path.value());

    const int receive_buffer = config.socket_receive_buffer_bytes;
    if (::setsockopt(everest::telemetry::RECEIVER_FD, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer)) !=
        0) {
        EVLOG_warning << "Could not set the receive buffer size of the telemetry socket";
    }

    m_receiver = std::make_unique<telemetry_router::DatagramReceiver>(
        everest::lib::io::event::unique_fd(everest::telemetry::RECEIVER_FD),
        [this](const std::uint8_t* data, std::size_t size) { m_router->handle_datagram(data, size); },
        [this]() { m_router->handle_truncated(); }, std::chrono::seconds(config.stats_log_interval_s),
        [this]() { log_statistics(); });
    m_receiver->start();
}

void TelemetryRouter::ready() {
    invoke_ready(*p_main);

    m_router->enable_sinks();
}

void TelemetryRouter::shutdown() {
    if (m_receiver) {
        m_receiver->stop();
    }
    if (m_router) {
        m_router->stop_sinks();
    }

    invoke_shutdown(*p_main);
}

void TelemetryRouter::log_statistics() {
    const auto& current = m_router->statistics();
    const auto& last = m_logged_statistics;
    EVLOG_info << fmt::format(
        "Telemetry: {} datagrams, {} samples, {} forwarded, {} dropped by rules, {} unknown producer, {} unknown "
        "element, {} invalid values, {} undecodable, {} truncated, {} declarations accepted, {} rejected",
        current.datagrams - last.datagrams, current.samples - last.samples, current.forwarded - last.forwarded,
        current.dropped_by_rules - last.dropped_by_rules, current.unknown_producer - last.unknown_producer,
        current.unknown_element - last.unknown_element, current.invalid_value - last.invalid_value,
        current.decode_errors - last.decode_errors, current.truncated - last.truncated,
        current.declares_accepted - last.declares_accepted, current.declares_rejected - last.declares_rejected);
    m_logged_statistics = current;
}

} // namespace module
