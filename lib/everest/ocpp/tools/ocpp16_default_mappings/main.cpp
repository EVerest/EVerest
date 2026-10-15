// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <fstream>
#include <iostream>

#include <ocpp/v16/known_keys_yaml.hpp>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output yaml file>\n";
        return 1;
    }

    std::ofstream out(argv[1]);
    out << ocpp::v16::keys::default_mappings_to_yaml();
    out.close();
    if (!out) {
        std::cerr << "Failed to write " << argv[1] << '\n';
        return 1;
    }
    return 0;
}
