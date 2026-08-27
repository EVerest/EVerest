// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/identifiers.hpp>

#include <regex>

namespace opcp {

bool is_valid_evseid(const std::string& value) {
    // ISO 15118-2 Annex H.2.1 as spelled out in the OPCP EST description
    static const std::regex pattern(R"(^[A-Za-z]{2}\*?[A-Za-z0-9]{3}\*?[Ee][A-Za-z0-9\*]{1,30}$)");
    return std::regex_match(value, pattern);
}

bool is_valid_seccid(const std::string& value) {
    // <Country Code><S><EVSE Operator ID><S>S<S><ControllerID><S><Check Digit>, separator "-" optional
    static const std::regex pattern(R"(^[A-Za-z]{2}(-?)[A-Za-z0-9]{3}\1[Ss]\1[A-Za-z0-9]{1,32}(\1[A-Za-z0-9])?$)");
    return std::regex_match(value, pattern);
}

std::string operator_id_of(const std::string& value) {
    static const std::regex pattern(R"(^[A-Za-z]{2}[\*\-]?([A-Za-z0-9]{3}).*$)");
    std::smatch match;
    if (std::regex_match(value, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    return {};
}

} // namespace opcp
