// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <iso15118/io/sha_hash.hpp>

namespace iso15118::ev::d20 {

class SessionId {

public:
    static constexpr auto ID_LENGTH = 8;
    SessionId(std::array<uint8_t, ID_LENGTH> id_) : id(id_){};
    ~SessionId() = default;

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

struct PauseContext {
    SessionId session_id{std::array<uint8_t, SessionId::ID_LENGTH>{}};
    std::optional<io::sha512_hash_t> charger_cert_hash{std::nullopt};
    std::optional<io::sha512_hash_t> charger_cert_session_hash{std::nullopt};
};

} // namespace iso15118::ev::d20
