// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "error_wrapper.hpp"
#include <utils/date.hpp>

namespace error_converter {

API_types_ext::Severity framework_to_external_api(Everest::error::Severity const& val) {
    using SrcT = Everest::error::Severity;
    using TarT = API_types_ext::Severity;
    switch (val) {
    case SrcT::High:
        return TarT::High;
    case SrcT::Medium:
        return TarT::Medium;
    case SrcT::Low:
        return TarT::Low;
    }
    throw std::out_of_range("Unexpected value for Everest::error::Severity");
}

API_types_ext::Mapping framework_to_external_api(Mapping const& val) {
    API_types_ext::Mapping result;
    result.evse = val.evse;
    result.connector = val.connector;
    return result;
}

API_types_ext::ImplementationIdentifier framework_to_external_api(ImplementationIdentifier const& val) {
    API_types_ext::ImplementationIdentifier result;
    result.implementation_id = val.implementation_id;
    result.module_id = val.module_id;
    if (val.mapping.has_value()) {
        result.mapping = framework_to_external_api(val.mapping.value());
    }
    return result;
}

API_types_ext::State framework_to_external_api(Everest::error::State const& val) {
    using SrcT = Everest::error::State;
    using TarT = API_types_ext::State;
    switch (val) {
    case SrcT::Active:
        return TarT::Active;
    case SrcT::ClearedByModule:
        return TarT::ClearedByModule;
    case SrcT::ClearedByReboot:
        return TarT::ClearedByReboot;
    }
    throw std::out_of_range("Unexpected value for Everest::error::State");
}

API_types_ext::ErrorObject framework_to_external_api(Everest::error::Error const& val) {
    API_types_ext::ErrorObject result;
    result.type = val.type;
    result.description = val.description;
    result.message = val.message;
    result.severity = framework_to_external_api(val.severity);
    result.origin = framework_to_external_api(val.origin);
    result.timestamp = Everest::Date::to_rfc3339(val.timestamp);
    result.uuid = val.uuid.to_string();
    result.state = framework_to_external_api(val.state);
    if (not val.sub_type.empty()) {
        result.sub_type.emplace(val.sub_type);
    }
    return result;
}

} // namespace error_converter
