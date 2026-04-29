// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "mqtt_lifecycle_service_client.hpp"

#include <chrono>
#include <iostream>
#include <random>
#include <string>

#include <everest/io/event/event_fd.hpp>
#include <everest_api_types/generic/codec.hpp>

namespace everest::lifecycle_cli {

namespace API_types = everest::lib::API::V1_0::types;
namespace API_generic = API_types::generic;
namespace API_types_lc = API_types::lifecycle;
namespace IO_mqtt = everest::lib::io::mqtt;
namespace IO_event = everest::lib::io::event;

namespace {
std::string random_id(std::size_t len = 16) {
    static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<std::size_t> dist{0, sizeof(chars) - 2};
    std::string s;
    s.reserve(len);
    for (std::size_t i = 0; i < len; ++i)
        s += chars[dist(rng)];
    return s;
}
} // namespace

MqttLifecycleServiceClient::MqttLifecycleServiceClient(const std::string& host, std::uint16_t port,
                                                       const everest::lib::API::Topics& topics,
                                                       std::uint32_t reconnect_ms, bool verbose) :
    m_client(reconnect_ms, "ev_lc_api_cli_" + random_id()),
    m_topics(topics),
    m_reply_topic_base("ev_lc_api_cli_" + random_id() + "/reply/") {

    if (verbose) {
        m_client.set_error_handler(
            [](int ec, const std::string& msg) { std::cerr << "MQTT error (" << ec << "): " << msg << "\n"; });
    }

    m_client.set_callback_connect([this](auto&, auto rc, auto, auto const&) {
        if (rc == IO_mqtt::mosquitto_cpp::ResponseCode::Success) {
            m_connected_promise.set_value();
        }
    });

    m_client.connect(host, port, 60);

    m_event_handler.register_event_handler(&m_client);
    m_event_handler.register_event_handler(&m_cancel_event, [] {});

    m_event_loop_thread = std::thread([this] { m_event_handler.run(m_running); });
}

MqttLifecycleServiceClient::~MqttLifecycleServiceClient() {
    m_running = false;
    m_cancel_event.notify();
    if (m_event_loop_thread.joinable()) {
        m_event_loop_thread.join();
    }
    m_client.disconnect();
}

void MqttLifecycleServiceClient::setup_status_subscription() {
    if (m_status_callback) {
        m_client.subscribe(
            m_topics.nonmodule_to_extern("status"),
            [this](IO_mqtt::mosquitto_cpp&, const IO_mqtt::mosquitto_cpp::message& msg) {
                auto notice = API_types_lc::try_deserialize<API_types_lc::ExecutionStatusUpdateNotice>(msg.payload);
                if (notice)
                    m_status_callback(*notice);
            },
            IO_mqtt::mosquitto_cpp::QoS::at_least_once);
    }
}

std::optional<std::string> MqttLifecycleServiceClient::perform_rpc_raw(const std::string& operation,
                                                                       const std::string& payload_json) {
    if (!m_running) {
        std::cerr << "Client is not running\n";
        return std::nullopt;
    }

    if (m_connected_future.wait_for(std::chrono::milliseconds(5000)) != std::future_status::ready) {
        std::cerr << "Timeout waiting for MQTT connection\n";
        return std::nullopt;
    }

    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    API_generic::RequestReply rr;
    rr.replyTo = m_reply_topic_base;
    rr.payload = payload_json;
    const std::string wrapped = API_generic::serialize(rr);
    const std::string req_topic = m_topics.extern_to_nonmodule(operation);

    m_event_handler.add_action([this, promise, req_topic, wrapped]() {
        m_client.subscribe(
            m_reply_topic_base,
            [promise](IO_mqtt::mosquitto_cpp&, const IO_mqtt::mosquitto_cpp::message& msg) {
                promise->set_value(msg.payload);
            },
            IO_mqtt::mosquitto_cpp::QoS::at_least_once);
        m_client.publish(req_topic, wrapped);
    });

    std::optional<std::string> result;
    if (future.wait_for(std::chrono::milliseconds(5000)) == std::future_status::ready) {
        result = future.get();
    } else {
        std::cerr << "Timeout waiting for reply on " << m_reply_topic_base << " (requested on " << req_topic << ")\n";
    }

    m_event_handler.add_action([this] { m_client.unsubscribe(m_reply_topic_base, IO_mqtt::PropertiesBase{}); });

    return result;
}

std::optional<API_types_lc::StopModulesResult> MqttLifecycleServiceClient::stop_modules() {
    auto raw = perform_rpc_raw("stop_modules", "{}");
    if (!raw)
        return std::nullopt;
    return API_types_lc::try_deserialize<API_types_lc::StopModulesResult>(*raw);
}

std::optional<API_types_lc::StartModulesResult> MqttLifecycleServiceClient::start_modules() {
    auto raw = perform_rpc_raw("start_modules", "{}");
    if (!raw)
        return std::nullopt;
    return API_types_lc::try_deserialize<API_types_lc::StartModulesResult>(*raw);
}

std::optional<API_types_lc::EVerestVersion> MqttLifecycleServiceClient::get_everest_version() {
    auto raw = perform_rpc_raw("get_everest_version", "{}");
    if (!raw)
        return std::nullopt;
    return API_types_lc::try_deserialize<API_types_lc::EVerestVersion>(*raw);
}

void MqttLifecycleServiceClient::subscribe_to_status_updates(StatusCallback callback) {
    m_status_callback = callback;
    setup_status_subscription();
}

} // namespace everest::lifecycle_cli
