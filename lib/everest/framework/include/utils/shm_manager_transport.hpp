// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <everest/io/shm/shm_server.hpp>

#include <utils/config.hpp>

namespace Everest {

inline constexpr std::uint32_t DEFAULT_SHM_TOPIC_SLOTS = 8;
inline constexpr std::uint32_t DEFAULT_SHM_TOPIC_SLOT_SIZE = 64 * 1024;
inline constexpr std::uint32_t DEFAULT_SHM_TOPIC_REGISTRY_CAPACITY = 1024;

std::string make_shm_transport_name(const MQTTSettings& mqtt_settings, int process_id);

MQTTSettings make_module_mqtt_settings(const MQTTSettings& manager_mqtt_settings);

std::optional<everest::lib::io::shm::server_options>
make_shm_server_options(ManagerConfig& config, const MQTTSettings& mqtt_settings, int process_id);

class ShmManagerTransportServer {
public:
    ShmManagerTransportServer();
    ~ShmManagerTransportServer();

    ShmManagerTransportServer(const ShmManagerTransportServer&) = delete;
    ShmManagerTransportServer& operator=(const ShmManagerTransportServer&) = delete;
    ShmManagerTransportServer(ShmManagerTransportServer&&) noexcept;
    ShmManagerTransportServer& operator=(ShmManagerTransportServer&&) noexcept;

    bool start(everest::lib::io::shm::server_options options);
    void stop();
    bool is_running() const;
    std::string last_error_message() const;
    everest::lib::io::shm::transport_counter_snapshot counter_snapshot() const;
    std::vector<everest::lib::io::shm::subscriber_snapshot> subscriber_snapshots() const;
    std::vector<everest::lib::io::shm::subscriber_snapshot> subscriber_snapshots(std::string_view topic) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Everest
