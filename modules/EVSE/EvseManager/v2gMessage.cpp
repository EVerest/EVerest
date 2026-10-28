// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "v2gMessage.hpp"

#include <chrono>
#include <cstddef>
#include <date/date.h>
#include <date/tz.h>

#include <everest/util/json/nesting_depth.hpp>
#include <fmt/format.h>

namespace module {

namespace {
constexpr std::size_t MAX_JSON_NESTING_DEPTH = 128;
} // namespace

v2g_message::v2g_message() {
}

bool v2g_message::from_xml(const std::string& xml) {
    return doc.load_string(xml.c_str());
}

std::string v2g_message::to_xml() {
    std::stringstream ss;
    doc.save(ss);
    return ss.str();
}

bool v2g_message::from_json(const std::string& json_str) {
    if (everest::lib::util::exceeds_json_nesting_depth(json_str, MAX_JSON_NESTING_DEPTH)) {
        j = nullptr;
        return false;
    }
    j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
        j = nullptr;
        return false;
    }
    return true;
}

std::string v2g_message::to_json() {
    return j.dump(4);
}

} // namespace module
