// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstddef>
#include <string_view>

namespace everest::lib::util {

/**
 * @brief Checks the nesting depth of JSON text without parsing it.
 *
 * Copying or serializing a parsed JSON value recurses once per nesting level, so input from untrusted sources should
 * be checked before parsing to prevent stack overflows.
 *
 * @param payload JSON text
 * @param max_depth maximum allowed number of nested arrays and objects
 * @return true if \p payload nests arrays and objects deeper than \p max_depth. The result is exact for valid JSON;
 * for invalid JSON, which a parser rejects anyway, it is unspecified.
 */
inline bool exceeds_json_nesting_depth(std::string_view payload, std::size_t max_depth) {
    std::size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char c : payload) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        switch (c) {
        case '"':
            in_string = true;
            break;
        case '[':
        case '{':
            if (++depth > max_depth) {
                return true;
            }
            break;
        case ']':
        case '}':
            if (depth > 0) {
                --depth;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

} // namespace everest::lib::util
