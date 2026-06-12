// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Async reader for the EEBUS control service discovery-event stream

#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>

#include <control_service/control_service.grpc.pb.h>

namespace module {

/// \brief Subscribes to the control service's discovery-event stream and forwards
///        every event to a callback. Read completions arrive on a gRPC thread;
///        the destructor blocks until the stream has fully shut down.
class DiscoveryEventReader : public grpc::ClientReadReactor<control_service::DiscoveryEvent> {
public:
    using EventCallback = std::function<void(const control_service::DiscoveryEvent&)>;
    using DisconnectionCallback = std::function<void()>;

    /// \brief construct a reader over an open control service stub
    /// \param[in] stub - control service stub to subscribe on
    /// \param[in] event_callback - invoked for every received discovery event
    /// \param[in] disconnection_callback - invoked when the stream ends for any reason other than stop()
    DiscoveryEventReader(std::shared_ptr<control_service::ControlService::Stub> stub, EventCallback event_callback,
                         DisconnectionCallback disconnection_callback);
    ~DiscoveryEventReader();
    DiscoveryEventReader(const DiscoveryEventReader&) = delete;
    DiscoveryEventReader(DiscoveryEventReader&&) = delete;
    DiscoveryEventReader& operator=(const DiscoveryEventReader&) = delete;
    DiscoveryEventReader& operator=(DiscoveryEventReader&&) = delete;

    /// \brief subscribe and start reading events
    void start();
    /// \brief cancel the stream; safe to call multiple times
    void stop();

    void OnReadDone(bool ok) override;
    void OnDone(const grpc::Status& s) override;

private:
    std::shared_ptr<control_service::ControlService::Stub> m_stub;
    EventCallback m_event_callback;
    DisconnectionCallback m_disconnection_callback;

    grpc::ClientContext m_context;
    control_service::SubscribeDiscoveryEventsRequest m_request;
    control_service::DiscoveryEvent m_response;

    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopped{false};
    std::promise<void> m_done_promise;
    std::future<void> m_done_future;
};

} // namespace module
