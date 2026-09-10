// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "OtlpCollector.hpp"

#include <map>
#include <optional>
#include <string_view>

#include <fmt/format.h>

namespace module {

namespace {

namespace otlp = opentelemetry::proto;

std::optional<std::string> to_string(const otlp::common::v1::AnyValue& value) {
    switch (value.value_case()) {
    case otlp::common::v1::AnyValue::kStringValue:
        return value.string_value();
    case otlp::common::v1::AnyValue::kIntValue:
        return std::to_string(value.int_value());
    case otlp::common::v1::AnyValue::kDoubleValue:
        return fmt::format("{}", value.double_value());
    case otlp::common::v1::AnyValue::kBoolValue:
        return value.bool_value() ? "true" : "false";
    default:
        return std::nullopt; // arrays, maps and bytes have no place in a variable instance
    }
}

std::optional<std::string> resource_attribute(const otlp::resource::v1::Resource& resource, std::string_view key) {
    for (const auto& attribute : resource.attributes()) {
        if (attribute.key() == key) {
            return to_string(attribute.value());
        }
    }
    return std::nullopt;
}

// Data point attributes become the variable instance, rendered as
// "key=value,key=value" in key order so that the same label set always maps to
// the same variable.
std::optional<std::string>
variable_instance(const google::protobuf::RepeatedPtrField<otlp::common::v1::KeyValue>& attributes) {
    std::map<std::string, std::string> sorted;
    for (const auto& attribute : attributes) {
        if (auto value = to_string(attribute.value())) {
            sorted.emplace(attribute.key(), std::move(*value));
        }
    }
    if (sorted.empty()) {
        return std::nullopt;
    }
    std::string instance;
    for (const auto& [key, value] : sorted) {
        instance += (instance.empty() ? "" : ",") + key + "=" + value;
    }
    return instance;
}

std::string to_string(const otlp::metrics::v1::NumberDataPoint& point) {
    if (point.has_as_int()) {
        return std::to_string(point.as_int());
    }
    return fmt::format("{}", point.as_double());
}

std::string to_string(const types::ocpp::ComponentVariable& cv) {
    return fmt::format("{}[{}]/{}[{}]", cv.component.name, cv.component.instance.value_or(""), cv.variable.name,
                       cv.variable.instance.value_or(""));
}

} // namespace

void OtlpCollector::init() {
    invoke_init(*p_main);
}

void OtlpCollector::ready() {
    invoke_ready(*p_main);

    server = std::make_unique<OtlpHttpServer>(static_cast<std::uint16_t>(config.listen_port),
                                              [this](const std::string& payload) { return on_metrics(payload); });
    EVLOG_info << "Receiving OTLP/HTTP metrics on port " << config.listen_port << " for device model component '"
               << config.component_name << "'";
}

void OtlpCollector::shutdown() {
    server.reset(); // stop accepting before the module goes away
    invoke_shutdown(*p_main);
}

bool OtlpCollector::on_metrics(const std::string& payload) {
    otlp::collector::metrics::v1::ExportMetricsServiceRequest request;
    if (not request.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        EVLOG_warning << "Discarding " << payload.size() << " bytes: not a valid ExportMetricsServiceRequest";
        return false;
    }

    auto requests = to_set_variable_requests(request);

    if (r_ocpp.empty()) {
        // no OCPP module connected: show what would have been written
        for (const auto& req : requests) {
            EVLOG_info << "metric " << to_string(req.component_variable) << " = " << req.value;
        }
        return true;
    }

    std::string source = info.id;
    for (const auto& result : r_ocpp.at(0)->call_set_variables(requests, source)) {
        if (result.status != types::ocpp::SetVariableStatusEnumType::Accepted) {
            EVLOG_warning << "Device model rejected " << to_string(result.component_variable) << ": "
                          << types::ocpp::set_variable_status_enum_type_to_string(result.status);
        }
    }
    return true;
}

std::vector<types::ocpp::SetVariableRequest> OtlpCollector::to_set_variable_requests(
    const otlp::collector::metrics::v1::ExportMetricsServiceRequest& request) const {
    std::vector<types::ocpp::SetVariableRequest> requests;

    for (const auto& resource_metrics : request.resource_metrics()) {
        // the producing module (service.instance.id, see the Example module) becomes the component instance
        types::ocpp::Component component;
        component.name = config.component_name;
        component.instance = resource_attribute(resource_metrics.resource(), "service.instance.id");

        for (const auto& scope_metrics : resource_metrics.scope_metrics()) {
            for (const auto& metric : scope_metrics.metrics()) {
                // Gauges and sums carry one number per data point. Histograms and
                // summaries have no obvious variable representation and are skipped.
                const google::protobuf::RepeatedPtrField<otlp::metrics::v1::NumberDataPoint>* points = nullptr;
                if (metric.has_gauge()) {
                    points = &metric.gauge().data_points();
                } else if (metric.has_sum()) {
                    points = &metric.sum().data_points();
                } else {
                    EVLOG_debug << "Skipping metric " << metric.name() << ": only gauges and sums are mapped";
                    continue;
                }

                for (const auto& point : *points) {
                    types::ocpp::SetVariableRequest req;
                    req.component_variable.component = component;
                    req.component_variable.variable.name = metric.name();
                    req.component_variable.variable.instance = variable_instance(point.attributes());
                    req.value = to_string(point);
                    req.attribute_type = types::ocpp::AttributeEnum::Actual;
                    requests.push_back(std::move(req));
                }
            }
        }
    }
    return requests;
}

} // namespace module
