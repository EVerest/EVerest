// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping_converter.hpp>

#include <stdexcept>
#include <utility>

#include <everest/ocpp_module_common/error_handling.hpp>

namespace ocpp_module_common::custom_error_mapping {

CustomErrorMappingConverter::CustomErrorMappingConverter(std::shared_ptr<const CustomErrorMapping> mapping,
                                                         std::vector<const ErrorMappingV16*> base_v16,
                                                         std::vector<const ErrorMappingV2X*> base_v2) :
    m_mapping(std::move(mapping)), m_base_v16(std::move(base_v16)), m_base_v2(std::move(base_v2)) {
    if (m_mapping == nullptr) {
        throw std::invalid_argument("CustomErrorMappingConverter needs a loaded custom error mapping");
    }
}

const CustomErrorMapping& CustomErrorMappingConverter::mapping() const {
    return *m_mapping;
}

const Entry* CustomErrorMappingConverter::entry_for(const Everest::error::Error& error) const {
    if (error.type == EVSE_MANAGER_INOPERATIVE_ERROR) {
        return nullptr;
    }
    return m_mapping->find(error.type, error.sub_type);
}

std::optional<ocpp::v16::ErrorInfo> CustomErrorMappingConverter::try_convert(const Everest::error::Error& error) const {
    const auto* entry = entry_for(error);
    if (entry == nullptr || !entry->v16.has_value()) {
        return std::nullopt;
    }

    std::optional<ocpp::v16::ErrorInfo> result;
    for (const auto* base : m_base_v16) {
        if (result = base->try_convert(error); result.has_value()) {
            break;
        }
    }
    if (!result.has_value()) {
        result = make_v16_error_info(error);
    }

    const auto& v16 = entry->v16.value();
    if (v16.error_code.has_value()) {
        result->error_code = v16.error_code.value();
    }
    if (v16.vendor_id.has_value()) {
        result->vendor_id = ocpp::CiString<255>(v16.vendor_id.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v16.vendor_error_code.has_value()) {
        result->vendor_error_code = ocpp::CiString<50>(v16.vendor_error_code.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v16.info.has_value()) {
        result->info = ocpp::CiString<50>(v16.info.value(), ocpp::StringTooLarge::Truncate);
    }
    return result;
}

std::optional<ocpp::v2::EventData> CustomErrorMappingConverter::try_convert(const Everest::error::Error& error,
                                                                            const bool cleared,
                                                                            const std::int32_t event_id) const {
    const auto* entry = entry_for(error);
    if (entry == nullptr || !entry->v2.has_value()) {
        return std::nullopt;
    }

    std::optional<ocpp::v2::EventData> result;
    for (const auto* base : m_base_v2) {
        if (result = base->try_convert(error, cleared, event_id); result.has_value()) {
            break;
        }
    }
    if (!result.has_value()) {
        result = make_v2_event_data(error, cleared, event_id);
    }

    const auto& v2 = entry->v2.value();
    if (v2.tech_code.has_value()) {
        result->techCode = ocpp::CiString<50>(v2.tech_code.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.tech_info.has_value()) {
        result->techInfo = ocpp::CiString<500>(v2.tech_info.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.component_name.has_value()) {
        result->component.name = ocpp::CiString<50>(v2.component_name.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.component_instance.has_value()) {
        result->component.instance = ocpp::CiString<50>(v2.component_instance.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.variable_name.has_value()) {
        result->variable.name = ocpp::CiString<50>(v2.variable_name.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.variable_instance.has_value()) {
        result->variable.instance = ocpp::CiString<50>(v2.variable_instance.value(), ocpp::StringTooLarge::Truncate);
    }
    return result;
}

} // namespace ocpp_module_common::custom_error_mapping
