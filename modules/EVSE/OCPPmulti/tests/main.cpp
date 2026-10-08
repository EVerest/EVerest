// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file support running the unit tests with different levels of verbosity

#include <getopt.h>
#include <gtest/gtest.h>
#include <iostream>

#include "everest/logging.hpp"

namespace {
// clang-format off
struct option long_options[] = {
    {"debug",      no_argument,       nullptr, 'd'},
    {"info",      no_argument,       nullptr, 'i'},
    {"warning",      no_argument,       nullptr, 'w'},
    {"error",      no_argument,       nullptr, 'e'},
    {"critical",      no_argument,       nullptr, 'c'},
    {"help",      no_argument,       nullptr, 'h'},
    {nullptr, 0, nullptr, 0},
};
// clang-format on
auto* short_options = "l:diwech";

void usage(const char* name) {
    std::cout << "\nAdditional usage:";
    std::cout << "  " << name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help             Show this help and exit\n";
    std::cout << "  -d, --debug            Show all log messages\n";
    std::cout << "  -i, --info             Show Info and above messages (default)\n";
    std::cout << "  -w, --warning          Show Warning and above messages\n";
    std::cout << "  -e, --error            Show Error and above messages\n";
    std::cout << "  -c, --critical         Show Critical messages\n";
    ::exit(1);
}

} // namespace

int main(int argc, char** argv) {
    EVLOG_level(Info);
    const auto* name = argv[0];

    testing::InitGoogleTest(&argc, argv);

    for (;;) {
        int option_index{0};
        const auto c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'd':
            EVLOG_level(Debug);
            break;
        case 'i':
            EVLOG_level(Info);
            break;
        case 'w':
            EVLOG_level(Warning);
            break;
        case 'e':
            EVLOG_level(Error);
            break;
        case 'c':
            EVLOG_level(Critical);
            break;
        case 0:
        case 'h':
        case '?':
        default:
            usage(name);
            break;
        }
    }

    return RUN_ALL_TESTS();
}