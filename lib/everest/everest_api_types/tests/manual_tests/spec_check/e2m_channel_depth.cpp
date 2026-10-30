// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest_api_types/utilities/Topics.hpp>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>
#include <string>

namespace {

namespace fs = std::filesystem;
using everest::lib::API::Topics;

// Topics::is_valid_reply_to accepts a replyTo in everest_api only at least two levels below e2m. That keeps
// replies off EVerest's own e2m channels only as long as every one of them is a single level deep.
TEST(ApiSpecifications, every_e2m_channel_is_one_level_deep) {
    const fs::path spec_dir{EVEREST_API_SPEC_DIR};
    ASSERT_TRUE(fs::is_directory(spec_dir)) << spec_dir;
    const std::regex address{R"(^\s*address:\s*['"]?([^'"\s]+))"};
    const std::string e2m = Topics::api_out + "/";
    std::size_t channels = 0;
    for (const auto& entry : fs::directory_iterator(spec_dir)) {
        if (entry.path().extension() != ".yaml") {
            continue;
        }
        std::ifstream file{entry.path()};
        std::string line;
        while (std::getline(file, line)) {
            std::smatch match;
            if (not std::regex_search(line, match, address)) {
                continue;
            }
            const std::string channel = match[1];
            const auto pos = channel.find(e2m);
            if (pos == std::string::npos) {
                continue;
            }
            ++channels;
            EXPECT_EQ(channel.find('/', pos + e2m.size()), std::string::npos)
                << entry.path().filename() << ": " << channel;
            // the replyTo rule relies on the same property: no e2m channel is a valid reply topic
            EXPECT_FALSE(Topics::is_valid_reply_to("everest_api/1/api/module/" + channel.substr(pos)))
                << entry.path().filename() << ": " << channel;
        }
    }
    EXPECT_GT(channels, 0U) << "no e2m channel found in " << spec_dir;
}

} // namespace
