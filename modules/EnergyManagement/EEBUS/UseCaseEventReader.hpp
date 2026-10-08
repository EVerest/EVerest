// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Async reader for a use case's event stream from the EEBUS control service

#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>

#include <control_service/control_service.grpc.pb.h>

namespace module {

/// \brief Subscribes to the control service's use-case event stream and forwards
///        every event to a callback. Read completions arrive on a gRPC thread;
///        the destructor blocks until the stream has fully shut down.
class UseCaseEventReader : public grpc::ClientReadReactor<control_service::SubscribeUseCaseEventsResponse> {
public:
    /// \brief construct a reader over an open control service stub
    /// \param[in] stub - control service stub to subscribe on
    /// \param[in] event_callback - invoked for every received use-case event
    /// \param[in] disconnection_callback - invoked when the stream ends for any reason other than stop()
    UseCaseEventReader(std::shared_ptr<control_service::ControlService::Stub> stub,
                       std::function<void(const control_service::SubscribeUseCaseEventsResponse&)> event_callback,
                       std::function<void()> disconnection_callback);
    ~UseCaseEventReader();
    UseCaseEventReader(const UseCaseEventReader&) = delete;
    UseCaseEventReader(UseCaseEventReader&&) = delete;
    UseCaseEventReader& operator=(const UseCaseEventReader&) = delete;
    UseCaseEventReader& operator=(UseCaseEventReader&&) = delete;

    /// \brief subscribe to the given use case's events and start reading
    /// \param[in] entity_address - entity the use case is registered on
    /// \param[in] use_case - use case to subscribe for
    void start(const common_types::EntityAddress& entity_address, const control_service::UseCase& use_case);
    /// \brief cancel the stream; safe to call multiple times
    void stop();

    void OnReadDone(bool ok) override;
    void OnDone(const grpc::Status& s) override;

private:
    std::shared_ptr<control_service::ControlService::Stub> m_stub;
    std::function<void(const control_service::SubscribeUseCaseEventsResponse&)> m_event_callback;
    std::function<void()> m_disconnection_callback;

    grpc::ClientContext m_context;
    control_service::SubscribeUseCaseEventsRequest m_request;
    control_service::SubscribeUseCaseEventsResponse m_response;

    std::atomic<bool> m_started{false};
    std::promise<void> m_done_promise;
    std::future<void> m_done_future;
};

} // namespace module
