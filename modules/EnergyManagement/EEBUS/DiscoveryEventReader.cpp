// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <DiscoveryEventReader.hpp>

#include <everest/logging.hpp>

namespace module {

DiscoveryEventReader::DiscoveryEventReader(std::shared_ptr<control_service::ControlService::Stub> stub,
                                           EventCallback event_callback, DisconnectionCallback disconnection_callback) :
    m_stub(std::move(stub)),
    m_event_callback(std::move(event_callback)),
    m_disconnection_callback(std::move(disconnection_callback)),
    m_done_future(m_done_promise.get_future()) {
}

DiscoveryEventReader::~DiscoveryEventReader() {
    if (m_started) {
        stop();
        m_done_future.wait();
    }
}

void DiscoveryEventReader::start() {
    m_started = true;
    if (!m_stub) {
        return;
    }
    m_stub->async()->SubscribeDiscoveryEvents(&m_context, &m_request, this);
    StartRead(&m_response);
    StartCall();
}

void DiscoveryEventReader::stop() {
    bool expected = false;
    if (m_stopped.compare_exchange_strong(expected, true)) {
        m_context.TryCancel();
    }
}

void DiscoveryEventReader::OnReadDone(bool ok) {
    if (ok) {
        if (m_event_callback) {
            m_event_callback(m_response);
        }
        StartRead(&m_response);
    }
}

void DiscoveryEventReader::OnDone(const grpc::Status& s) {
    m_done_promise.set_value();
    if (s.error_code() == grpc::StatusCode::CANCELLED) {
        return;
    }
    if (!s.ok()) {
        EVLOG_error << "DiscoveryEventReader stream failed: " << s.error_message();
    } else {
        EVLOG_warning << "DiscoveryEventReader stream closed by server unexpectedly.";
    }
    if (m_disconnection_callback) {
        m_disconnection_callback();
    }
}

} // namespace module
