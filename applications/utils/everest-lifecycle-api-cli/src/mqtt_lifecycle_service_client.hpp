// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "lifecycle_service_client_ifc.hpp"

#include "everest_api_types/utilities/Topics.hpp"
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace everest::lifecycle_cli {

class MqttLifecycleServiceClient : public LifecycleServiceClientIfc {
public:
    MqttLifecycleServiceClient(const std::string& host, std::uint16_t port, const everest::lib::API::Topics& topics,
                               std::uint32_t reconnect_ms = 5000, bool verbose = false);
    ~MqttLifecycleServiceClient() override;

    std::optional<API_types_lc::StopModulesResult> stop_modules() override;
    std::optional<API_types_lc::StartModulesResult> start_modules() override;
    std::optional<API_types_lc::EVerestVersion> get_everest_version() override;
    void subscribe_to_status_updates(StatusCallback callback) override;

private:
    std::optional<std::string> perform_rpc_raw(const std::string& operation, const std::string& payload_json);

    void setup_status_subscription();

    everest::lib::io::event::fd_event_handler m_event_handler;
    everest::lib::io::mqtt::mqtt_client m_client;

    everest::lib::API::Topics m_topics;
    std::string m_reply_topic_base;

    std::atomic<bool> m_running{true};
    everest::lib::io::event::event_fd m_cancel_event;
    std::thread m_event_loop_thread;

    std::promise<void> m_connected_promise;
    std::shared_future<void> m_connected_future{m_connected_promise.get_future().share()};

    StatusCallback m_status_callback;
};

} // namespace everest::lifecycle_cli
