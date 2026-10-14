// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/ocpp_connection.hpp>

#include <everest/logging.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_abstract.hpp>
#include <ocpp/v2/message_permission.hpp>

#include <stdexcept>
#include <utility>

namespace ocpp {
namespace v2 {

OcppConnection::OcppConnection(ConnectionId id, std::unique_ptr<ocpp::ConnectivityManagerConfiguration> configuration,
                               std::shared_ptr<ConnectivityManagerInterface> connectivity_manager,
                               std::shared_ptr<MessageQueue<MessageType>> message_queue,
                               std::unique_ptr<MessageDispatcherInterface<MessageType>> dispatcher,
                               DeviceModelAbstract& device_model) :
    m_id(std::move(id)),
    m_configuration(std::move(configuration)),
    m_connectivity_manager(std::move(connectivity_manager)),
    m_message_queue(std::move(message_queue)),
    m_dispatcher(std::move(dispatcher)),
    m_device_model(device_model) {
    if (this->m_connectivity_manager == nullptr) {
        throw std::invalid_argument("OcppConnection " + this->m_id.to_string() + " requires a connectivity manager");
    }
    if (this->m_message_queue == nullptr) {
        throw std::invalid_argument("OcppConnection " + this->m_id.to_string() + " requires a message queue");
    }
    if (this->m_dispatcher == nullptr) {
        throw std::invalid_argument("OcppConnection " + this->m_id.to_string() + " requires a message dispatcher");
    }

    this->reload_permissions();
}

void OcppConnection::reload_permissions() {
    if (this->m_id.is_primary()) {
        // The primary connection has no allow list: it is the authoritative connection and may be
        // sent everything.
        return;
    }

    const auto read = [this](const Variable& variable) {
        return parse_message_permissions(
            this->m_device_model
                .get_optional_value<std::string>(
                    ConnectionComponentVariables::get_component_variable(this->m_id, variable))
                .value_or(""));
    };

    auto upstream = read(ConnectionComponentVariables::UpstreamMsgs);
    auto downstream = read(ConnectionComponentVariables::DownstreamMsgs);
    auto writable = VariablePermissionList::parse(
        this->m_device_model
            .get_optional_value<std::string>(ConnectionComponentVariables::get_component_variable(
                this->m_id, ConnectionComponentVariables::AllowSetVariables))
            .value_or(""));

    EVLOG_info << this->m_id.to_string() << " may be sent " << upstream.size() << " message type(s), may send "
               << downstream.size() << ", and may write " << (writable.empty() ? "nothing" : "some variables");
    *this->m_upstream_msgs.handle() = std::move(upstream);
    *this->m_downstream_msgs.handle() = std::move(downstream);
    *this->m_allow_set_variables.handle() = std::move(writable);
}

bool OcppConnection::is_privilege_variable(const Component& component, const Variable& variable) {
    if (!(component.name == ControllerComponents::OCPPCommCtrlr.name)) {
        return false;
    }
    return variable.name == ConnectionComponentVariables::AllowSetVariables.name or
           variable.name == ConnectionComponentVariables::UpstreamMsgs.name or
           variable.name == ConnectionComponentVariables::DownstreamMsgs.name;
}

bool OcppConnection::may_set_variable(const Component& component, const Variable& variable) const {
    if (this->m_id.is_primary()) {
        return true;
    }

    // Section 3.6.2: these stay read only for a secondary system even when the whole OCPPCommCtrlr
    // component has been granted, so that it can never widen its own privileges.
    if (is_privilege_variable(component, variable)) {
        return false;
    }

    auto handle = this->m_allow_set_variables.handle();
    return handle->allows(component, variable);
}

bool OcppConnection::may_receive(const MessageType message_type) const {
    if (this->m_id.is_primary()) {
        return true;
    }

    // Default deny, as for may_send. Nothing is exempt here: unlike sending, there is no message a
    // secondary connection needs to be able to send in order to keep its own link alive, and the
    // link's own messages travel the other way.
    auto handle = this->m_downstream_msgs.handle();
    return handle->find(message_type) != handle->end();
}

const ConnectionId& OcppConnection::id() const {
    return this->m_id;
}

ConnectivityManagerInterface& OcppConnection::connectivity_manager() {
    return *this->m_connectivity_manager;
}

std::shared_ptr<ConnectivityManagerInterface> OcppConnection::connectivity_manager_ptr() const {
    return this->m_connectivity_manager;
}

MessageQueue<MessageType>& OcppConnection::message_queue() {
    return *this->m_message_queue;
}

std::shared_ptr<MessageQueue<MessageType>> OcppConnection::message_queue_ptr() const {
    return this->m_message_queue;
}

MessageDispatcherInterface<MessageType>& OcppConnection::dispatcher() {
    return *this->m_dispatcher;
}

bool OcppConnection::may_send(const MessageType message_type) const {
    if (this->m_id.is_primary()) {
        return true;
    }

    // Default deny, per application note section 3.6: a secondary system gets nothing it has not
    // been granted, so an absent or empty UpstreamMsgs means nothing is sent to it.
    // These are about the pipe rather than the charging station's business: a connection that exists
    // at all must be able to announce itself and stay alive, so they are never withheld.
    if (message_type == MessageType::BootNotification or message_type == MessageType::Heartbeat) {
        return true;
    }

    auto handle = this->m_upstream_msgs.handle();
    return handle->find(message_type) != handle->end();
}

void OcppConnection::start_heartbeat(std::function<void()> send, const std::chrono::seconds interval) {
    if (interval <= std::chrono::seconds(0)) {
        this->stop_heartbeat();
        return;
    }
    this->m_heartbeat_timer.interval(std::move(send), interval);
}

void OcppConnection::stop_heartbeat() {
    this->m_heartbeat_timer.stop();
}

} // namespace v2
} // namespace ocpp
