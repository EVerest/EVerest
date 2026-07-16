// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>

namespace stubs {

// Minimal map-backed DeviceModelInterface: Actual attribute only, per-variable mutability.
class DeviceModelStub : public ocpp::v2::DeviceModelInterface {
public:
    struct Entry {
        std::string value;
        ocpp::v2::MutabilityEnum mutability{ocpp::v2::MutabilityEnum::ReadWrite};
        std::string last_source;
        // when set, a committed set_value returns this status instead of Accepted (e.g. RebootRequired)
        std::optional<ocpp::v2::SetVariableStatusEnum> forced_set_status;
    };

    void add(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable, const std::string& value,
             ocpp::v2::MutabilityEnum mutability = ocpp::v2::MutabilityEnum::ReadWrite) {
        m_store[{component, variable}] = Entry{value, mutability, {}, std::nullopt};
        m_components.insert(component);
    }

    void set_forced_set_status(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                               ocpp::v2::SetVariableStatusEnum status) {
        m_store.at({component, variable}).forced_set_status = status;
    }

    const Entry& entry(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const {
        return m_store.at({component, variable});
    }

    ocpp::v2::GetVariableStatusEnum get_variable(const ocpp::v2::Component& component_id,
                                                 const ocpp::v2::Variable& variable_id,
                                                 const ocpp::v2::AttributeEnum& attribute_enum, std::string& value,
                                                 bool allow_write_only = false) const override {
        if (m_components.count(component_id) == 0) {
            return ocpp::v2::GetVariableStatusEnum::UnknownComponent;
        }
        const auto it = m_store.find({component_id, variable_id});
        if (it == m_store.end()) {
            return ocpp::v2::GetVariableStatusEnum::UnknownVariable;
        }
        if (attribute_enum != ocpp::v2::AttributeEnum::Actual) {
            return ocpp::v2::GetVariableStatusEnum::NotSupportedAttributeType;
        }
        if (!allow_write_only && (it->second.mutability == ocpp::v2::MutabilityEnum::WriteOnly)) {
            return ocpp::v2::GetVariableStatusEnum::Rejected;
        }
        value = it->second.value;
        return ocpp::v2::GetVariableStatusEnum::Accepted;
    }

    ocpp::v2::SetVariableStatusEnum set_value(const ocpp::v2::Component& component_id,
                                              const ocpp::v2::Variable& variable_id,
                                              const ocpp::v2::AttributeEnum& attribute_enum, const std::string& value,
                                              const std::string& source, bool allow_read_only = false) override {
        if (m_components.count(component_id) == 0) {
            return ocpp::v2::SetVariableStatusEnum::UnknownComponent;
        }
        const auto it = m_store.find({component_id, variable_id});
        if (it == m_store.end()) {
            return ocpp::v2::SetVariableStatusEnum::UnknownVariable;
        }
        if (attribute_enum != ocpp::v2::AttributeEnum::Actual) {
            return ocpp::v2::SetVariableStatusEnum::NotSupportedAttributeType;
        }
        if (!allow_read_only && (it->second.mutability == ocpp::v2::MutabilityEnum::ReadOnly)) {
            return ocpp::v2::SetVariableStatusEnum::Rejected;
        }
        it->second.value = value;
        it->second.last_source = source;
        if (it->second.forced_set_status.has_value()) {
            return it->second.forced_set_status.value();
        }
        return ocpp::v2::SetVariableStatusEnum::Accepted;
    }

    ocpp::v2::SetVariableStatusEnum set_read_only_value(const ocpp::v2::Component& component_id,
                                                        const ocpp::v2::Variable& variable_id,
                                                        const ocpp::v2::AttributeEnum& attribute_enum,
                                                        const std::string& value, const std::string& source) override {
        ++m_set_read_only_calls;
        return set_value(component_id, variable_id, attribute_enum, value, source, true);
    }

    ocpp::v2::SetVariableStatusEnum clear_value(const ocpp::v2::Component& component_id,
                                                const ocpp::v2::Variable& variable_id,
                                                const ocpp::v2::AttributeEnum& attribute_enum,
                                                const std::string& source) override {
        return set_value(component_id, variable_id, attribute_enum, "", source, true);
    }

    std::optional<ocpp::v2::MutabilityEnum> get_mutability(const ocpp::v2::Component& component_id,
                                                           const ocpp::v2::Variable& variable_id,
                                                           const ocpp::v2::AttributeEnum& attribute_enum) override {
        (void)attribute_enum;
        const auto it = m_store.find({component_id, variable_id});
        if (it == m_store.end()) {
            return std::nullopt;
        }
        return it->second.mutability;
    }

    std::optional<ocpp::v2::VariableMetaData> get_variable_meta_data(const ocpp::v2::Component& component_id,
                                                                     const ocpp::v2::Variable& variable_id) override {
        if (m_store.count({component_id, variable_id}) == 0) {
            return std::nullopt;
        }
        return ocpp::v2::VariableMetaData{};
    }

    std::vector<ocpp::v2::ReportData> get_base_report_data(const ocpp::v2::ReportBaseEnum&) override {
        return {};
    }

    std::vector<ocpp::v2::ReportData> get_custom_report_data(
        const std::optional<std::vector<ocpp::v2::ComponentVariable>>& = std::nullopt,
        const std::optional<std::vector<ocpp::v2::ComponentCriterionEnum>>& = std::nullopt) override {
        return {};
    }

    std::vector<ocpp::v2::SetMonitoringResult>
    set_monitors(const std::vector<ocpp::v2::SetMonitoringData>&,
                 const ocpp::v2::VariableMonitorType = ocpp::v2::VariableMonitorType::CustomMonitor) override {
        return {};
    }

    bool update_monitor_reference(std::int32_t, const std::string&) override {
        return false;
    }

    std::vector<ocpp::v2::VariableMonitoringPeriodic> get_periodic_monitors() override {
        return {};
    }

    std::vector<ocpp::v2::MonitoringData> get_monitors(const std::vector<ocpp::v2::MonitoringCriterionEnum>&,
                                                       const std::vector<ocpp::v2::ComponentVariable>&) override {
        return {};
    }

    std::vector<ocpp::v2::ClearMonitoringResult> clear_monitors(const std::vector<int>&, bool = false) override {
        return {};
    }

    std::int32_t clear_custom_monitors() override {
        return 0;
    }

    void register_variable_listener(
        std::function<void(const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>&,
                           const ocpp::v2::Component&, const ocpp::v2::Variable&,
                           const ocpp::v2::VariableCharacteristics&, const ocpp::v2::VariableAttribute&,
                           const std::string&, const std::string&)>&&) override {
    }

    void
    register_monitor_listener(std::function<void(const ocpp::v2::VariableMonitoringMeta&, const ocpp::v2::Component&,
                                                 const ocpp::v2::Variable&, const ocpp::v2::VariableCharacteristics&,
                                                 const ocpp::v2::VariableAttribute&, const std::string&)>&&) override {
    }

    void check_integrity(const std::map<std::int32_t, std::int32_t>&) override {
    }

    int set_read_only_calls() const {
        return m_set_read_only_calls;
    }

private:
    std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, Entry> m_store;
    std::set<ocpp::v2::Component> m_components;
    int m_set_read_only_calls{0};
};

} // namespace stubs
