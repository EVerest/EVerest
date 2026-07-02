// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <UseCaseEventReader.hpp>

#include <everest/logging.hpp>

#include "common_types/types.pb.h"
#include "control_service/messages.pb.h"

namespace module {

UseCaseEventReader::UseCaseEventReader(
    std::shared_ptr<control_service::ControlService::Stub> stub,
    std::function<void(const control_service::SubscribeUseCaseEventsResponse&)> event_callback,
    std::function<void()> disconnection_callback) :
    m_stub(std::move(stub)),
    m_event_callback(std::move(event_callback)),
    m_disconnection_callback(std::move(disconnection_callback)),
    m_done_future(m_done_promise.get_future()) {
}

UseCaseEventReader::~UseCaseEventReader() {
    if (m_started) {
        stop();
        m_done_future.wait();
    }
}

void UseCaseEventReader::start(const common_types::EntityAddress& entity_address,
                               const control_service::UseCase& use_case) {
    m_started = true;
    if (!m_stub) {
        return;
    }
    m_request.mutable_entity_address()->CopyFrom(entity_address);
    m_request.mutable_use_case()->CopyFrom(use_case);
    m_stub->async()->SubscribeUseCaseEvents(&m_context, &m_request, this);
    StartRead(&m_response);
    StartCall();
}

void UseCaseEventReader::stop() {
    m_context.TryCancel();
}

void UseCaseEventReader::OnReadDone(bool ok) {
    if (ok) {
        if (m_event_callback) {
            m_event_callback(m_response);
        }
        StartRead(&m_response);
    }
}

void UseCaseEventReader::OnDone(const grpc::Status& s) {
    m_done_promise.set_value();
    if (s.error_code() == grpc::StatusCode::CANCELLED) {
        return; // Normal shutdown via stop()
    }
    if (!s.ok()) {
        EVLOG_error << "UseCaseEventReader stream failed: " << s.error_message();
    } else {
        EVLOG_warning << "UseCaseEventReader stream closed by server unexpectedly.";
    }
    if (m_disconnection_callback) {
        m_disconnection_callback();
    }
}

} // namespace module
