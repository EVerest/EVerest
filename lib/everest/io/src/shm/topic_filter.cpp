// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/topic_filter.hpp>

#include <cstddef>

namespace everest::lib::io::shm {

bool is_valid_topic_filter(std::string_view filter) {
    if (filter.empty()) {
        return false;
    }

    std::size_t level_start = 0U;
    while (level_start <= filter.size()) {
        auto level_end = filter.find('/', level_start);
        if (level_end == std::string_view::npos) {
            level_end = filter.size();
        }

        const auto level = filter.substr(level_start, level_end - level_start);
        if (level.find('+') != std::string_view::npos && level != "+") {
            return false;
        }
        if (level.find('#') != std::string_view::npos) {
            if (level != "#" || level_end != filter.size()) {
                return false;
            }
        }

        if (level_end == filter.size()) {
            break;
        }
        level_start = level_end + 1U;
    }

    return true;
}

bool topic_filter_matches(std::string_view filter, std::string_view topic) {
    if (!is_valid_topic_filter(filter) || topic.empty()) {
        return false;
    }

    std::size_t filter_pos = 0U;
    std::size_t topic_pos = 0U;

    while (true) {
        const auto filter_end = filter.find('/', filter_pos);
        const auto topic_end = topic.find('/', topic_pos);

        const bool filter_last = filter_end == std::string_view::npos;
        const bool topic_last = topic_end == std::string_view::npos;

        const auto filter_level_end = filter_last ? filter.size() : filter_end;
        const auto topic_level_end = topic_last ? topic.size() : topic_end;
        const auto filter_level = filter.substr(filter_pos, filter_level_end - filter_pos);
        const auto topic_level = topic.substr(topic_pos, topic_level_end - topic_pos);

        if (filter_level == "#") {
            return true;
        }
        if (filter_level != "+" && filter_level != topic_level) {
            return false;
        }
        if (filter_last && topic_last) {
            return true;
        }
        if (!filter_last && topic_last) {
            return filter.substr(filter_end + 1U) == "#";
        }
        if (filter_last || topic_last) {
            return false;
        }

        filter_pos = filter_end + 1U;
        topic_pos = topic_end + 1U;
    }
}

} // namespace everest::lib::io::shm
