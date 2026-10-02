// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <generated/interfaces/auth_token_provider/Implementation.hpp>

class NfcHandler {
public:
    struct RuntimeConfig {
        std::string transport;
        std::string device;
        std::string i2c_address;
        std::string pin_int;
        std::string pin_enable;
        std::string pin_fwdnld;
    };

    enum class Protocol
    {
        UNKNOWN,
        MIFARE,
        ISO_IEC_15693,
    };

    using Callback = std::function<void(char* uid, size_t length, Protocol)>;

    NfcHandler(const std::filesystem::path& config_path, const RuntimeConfig& runtime_config);
    ~NfcHandler();

    bool start(Callback);

private:
    void on_tag_arrival(void* pTagInfo);
    void on_tag_departure();

    Callback callback{nullptr};
};
