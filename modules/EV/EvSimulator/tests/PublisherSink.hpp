// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace module::test {

// Captures FsmContext publish() calls in arrival order; wired as the
// `Publisher` callback in test fixtures.
class PublisherSink {
public:
    void operator()(const std::string& topic, const std::string& payload) {
        records.emplace_back(topic, payload);
    }

    std::vector<std::pair<std::string, std::string>> records;

    void clear() {
        records.clear();
    }
};

} // namespace module::test
