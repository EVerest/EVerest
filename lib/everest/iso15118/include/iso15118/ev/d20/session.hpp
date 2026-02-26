// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>

namespace iso15118::ev::d20 {

class Session {

public:
    static constexpr auto ID_LENGTH = 8;
    Session(std::array<uint8_t, ID_LENGTH> id_) : id(id_){};
    ~Session() = default;

    std::array<uint8_t, ID_LENGTH> get_id() const {
        return id;
    }

    // Sets the session ID, mostly for testing purposes
    void set_id(const std::array<uint8_t, ID_LENGTH>& new_id) {
        id = new_id;
    }

private:
    std::array<uint8_t, ID_LENGTH> id{};
};

} // namespace iso15118::ev::d20
