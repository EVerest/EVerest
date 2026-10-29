// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "utilities/Topics.hpp"
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

namespace everest::lib::API {

void Topics::setup(std::string const& target_module_id, std::string const& api_type, unsigned int version) {
    m_target_module_id = target_module_id;
    m_api_type = api_type;
    m_api_version = std::to_string(version);
}

std::string Topics::entrypoint(const std::string& var) const {
    std::stringstream topic;
    topic << everest::lib::API::Topics::api_base << "/" << var;
    return topic.str();
}

std::string Topics::nonmodule_to_extern(const std::string& var) const {
    std::stringstream topic;
    topic << api_base << "/" << m_api_version << "/" << m_api_type << "/" << api_out << "/" << var;
    return topic.str();
}

std::string Topics::extern_to_nonmodule(const std::string& var) const {
    std::stringstream topic;
    topic << api_base << "/" << m_api_version << "/" << m_api_type << "/" << api_in << "/" << var;
    return topic.str();
}

std::string Topics::everest_to_extern(const std::string& var) const {
    std::stringstream topic;
    topic << api_base << "/" << m_api_version << "/" << m_api_type << "/" << m_target_module_id << "/" << api_out << "/"
          << var;
    return topic.str();
}

std::string Topics::extern_to_everest(const std::string& var) const {
    std::stringstream topic;
    topic << api_base << "/" << m_api_version << "/" << m_api_type << "/" << m_target_module_id << "/" << api_in << "/"
          << var;
    return topic.str();
}

std::string Topics::reply_to_everest(const std::string& reply) const {
    std::stringstream topic;
    topic << api_base << "/" << m_api_version << "/" << m_api_type << "/" << m_target_module_id << "/" << api_in
          << "/reply/" << reply;
    return topic.str();
}

bool Topics::is_publishable(std::string_view topic) {
    static constexpr std::size_t max_topic_length = 65535;
    if (topic.empty() || topic.length() > max_topic_length) {
        return false;
    }
    if (topic.find('\0') != std::string_view::npos || topic.find_first_of("+#") != std::string_view::npos) {
        return false;
    }
    return topic.front() != '$';
}

bool Topics::is_valid_reply_to(std::string_view topic) {
    if (not is_publishable(topic)) {
        return false;
    }
    std::vector<std::string_view> levels;
    for (std::size_t begin = 0;;) {
        auto end = topic.find('/', begin);
        levels.push_back(topic.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    // the framework's internal namespace (default mqtt_everest_prefix) and the Node-RED debug
    // interface that EvseManager, EvManager and the BSPs subscribe to
    if (levels.front() == "everest" ||
        (levels.size() > 1 && levels[0] == "everest_external" && levels[1] == "nodered")) {
        return false;
    }
    if (levels.front() != api_base) {
        return true;
    }
    // Everything in everest_api is either a topic EVerest subscribes to (m2e, discover, query-modules,
    // <module>/cmd/...) or one it publishes to (e2m/<name>, always one level deep). A reply needs a
    // level of its own below e2m/<name>, so it is taken neither for a request nor for EVerest's
    // own publication.
    auto const e2m = std::find(levels.begin(), levels.end(), api_out);
    return std::distance(e2m, levels.end()) >= 3 && std::find(levels.begin(), levels.end(), api_in) == levels.end();
}

const std::string Topics::api_base = "everest_api";
const std::string Topics::api_out = "e2m";
const std::string Topics::api_in = "m2e";

} // namespace everest::lib::API
