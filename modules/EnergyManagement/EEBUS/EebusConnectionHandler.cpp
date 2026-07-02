// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <EebusConnectionHandler.hpp>

#include <ConfigValidator.hpp>
#include <everest/logging.hpp>
#include <utility>

namespace module {

namespace {
constexpr auto CHANNEL_READY_TIMEOUT = std::chrono::seconds(60);
constexpr auto HEARTBEAT_TIMEOUT_SECONDS = 120;
} // namespace

EebusConnectionHandler::EebusConnectionHandler(std::shared_ptr<ConfigValidator> config) : m_config(std::move(config)) {
    if (!initialize_connection()) {
        m_handler.add_action([this] { handle_event(EebusConnectionEvents::DISCONNECTED); });
    }
}

everest::lib::io::event::sync_status EebusConnectionHandler::sync() {
    m_handler.run_once();
    return everest::lib::io::event::sync_status::ok;
}

int EebusConnectionHandler::get_poll_fd() {
    return m_handler.get_poll_fd();
}

EebusConnectionHandler::~EebusConnectionHandler() {
    stop();
}

void EebusConnectionHandler::reconnect() {
    EVLOG_info << "Attempting to reconnect to EEBUS gRPC server...";
    if (initialize_connection()) {
        m_handler.unregister_event_handler(&m_reconnection_timer);
        EVLOG_info << "Reconnected successfully.";
        if (m_use_case_added) {
            if (!add_use_case(m_last_use_case, m_last_callbacks)) {
                EVLOG_error << "Failed to re-add use case after reconnect. Will retry.";
                m_reconnection_timer.set_timeout(std::chrono::seconds(m_config->get_reconnect_delay_s()));
                return;
            }
            done_adding_use_case();
        }
    } else {
        EVLOG_warning << "Reconnect attempt failed. Will try again in " << m_config->get_reconnect_delay_s()
                      << " seconds.";
        m_reconnection_timer.set_timeout(std::chrono::seconds(m_config->get_reconnect_delay_s()));
    }
}

void EebusConnectionHandler::reset() {
    if (m_event_reader) {
        m_event_reader->stop();
        m_event_reader.reset();
    }
    if (m_discovery_event_reader) {
        m_discovery_event_reader->stop();
        m_discovery_event_reader.reset();
    }
    if (m_lpc_handler) {
        m_lpc_handler.reset();
    }
    if (m_control_service_stub) {
        m_control_service_stub.reset();
    }
    if (m_control_service_channel) {
        m_control_service_channel.reset();
    }
}

bool EebusConnectionHandler::initialize_connection() {
    if (!create_channel_and_stub()) {
        EVLOG_error << "Connection to EEBUS gRPC server failed, stopping.";
        return false;
    }

    if (!configure_service()) {
        EVLOG_error << "Failed to configure EEBUS gRPC service, stopping.";
        return false;
    }

    m_handler.add_action([this] { handle_event(EebusConnectionEvents::CONNECTED); });
    return true;
}

bool EebusConnectionHandler::create_channel_and_stub() {
    const auto address = "localhost:" + std::to_string(m_config->get_grpc_port());
    m_control_service_channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    if (!wait_for_channel_ready(m_control_service_channel, CHANNEL_READY_TIMEOUT)) {
        return false;
    }
    m_control_service_stub = control_service::ControlService::NewStub(m_control_service_channel);
    return true;
}

bool EebusConnectionHandler::configure_service() {
    control_service::SetConfigRequest set_config_request = control_service::CreateSetConfigRequest(
        m_config->get_eebus_service_port(), m_config->get_vendor_code(), m_config->get_device_brand(),
        m_config->get_device_model(), m_config->get_serial_number(),
        {control_service::DeviceCategory_Enum::DeviceCategory_Enum_E_MOBILITY},
        control_service::DeviceType_Enum::DeviceType_Enum_CHARGING_STATION,
        {control_service::EntityType_Enum::EntityType_Enum_EVSE}, HEARTBEAT_TIMEOUT_SECONDS);
    control_service::EmptyResponse set_config_response;
    auto set_config_status =
        control_service::CallSetConfig(m_control_service_stub, set_config_request, &set_config_response);
    if (!set_config_status.ok()) {
        EVLOG_error << "set_config failed: " << set_config_status.error_message();
        return false;
    }

    control_service::EmptyRequest start_setup_request;
    control_service::EmptyResponse start_setup_response;
    auto start_setup_status =
        control_service::CallStartSetup(m_control_service_stub, start_setup_request, &start_setup_response);
    if (!start_setup_status.ok()) {
        EVLOG_warning << "start_setup failed: " << start_setup_status.error_message();
        // This is not considered a fatal error
    }

    // Register every SKI in the effective allowlist before StartService.
    for (const auto& ski : m_config->get_effective_ems_ski_allowlist()) {
        control_service::RegisterRemoteSkiRequest register_ski_request;
        register_ski_request.set_remote_ski(ski);
        control_service::EmptyResponse register_ski_response;
        auto register_ski_status = control_service::CallRegisterRemoteSki(m_control_service_stub, register_ski_request,
                                                                          &register_ski_response);
        if (!register_ski_status.ok()) {
            EVLOG_warning << "register_remote_ski(" << ski << ") failed: " << register_ski_status.error_message();
            // Non-fatal: other allowlisted SKIs may still succeed, and accept_unknown_ems
            // may yet populate the trust list at runtime.
            continue;
        }
        EVLOG_info << "Pre-registered allowlisted EG SKI=" << ski;
    }
    // An empty allowlist is OK here — runtime discovery events may still add trust.

    return true;
}

void EebusConnectionHandler::start_service() {
    control_service::EmptyRequest request;
    control_service::EmptyResponse response;
    auto status = control_service::CallStartService(m_control_service_stub, request, &response);
    if (!status.ok()) {
        EVLOG_error << "start_service failed: " << status.error_message();
        m_handler.add_action([this] { handle_event(EebusConnectionEvents::DISCONNECTED); });
        return;
    }

    if (m_lpc_handler) {
        m_lpc_handler->start();
    }

    m_handler.add_action([this] { handle_event(EebusConnectionEvents::STARTED); });
}

bool EebusConnectionHandler::add_use_case(EebusUseCase use_case, const EebusCallbacks& callbacks) {
    m_last_use_case = use_case;
    m_last_callbacks = callbacks;
    m_use_case_added = true;

    switch (use_case) {
    case EebusUseCase::LPC:
        m_lpc_handler = std::make_unique<lpc::LpcUseCaseHandler>(m_config->get_failsafe_control_limit(),
                                                                 m_config->get_max_nominal_power(), callbacks);

        control_service::UseCase use_case_info = lpc::LpcUseCaseHandler::get_use_case_info();
        common_types::EntityAddress entity_address = common_types::CreateEntityAddress({1});
        control_service::AddUseCaseRequest request =
            control_service::CreateAddUseCaseRequest(&entity_address, &use_case_info);
        control_service::AddUseCaseResponse response;
        auto status = control_service::CallAddUseCase(m_control_service_stub, request, &response);
        std::ignore = request.release_entity_address();
        std::ignore = request.release_use_case();
        if (!status.ok() || response.endpoint().empty()) {
            EVLOG_error << "add_use_case failed: " << status.error_message();
            return false;
        }

        auto lpc_channel = grpc::CreateChannel(response.endpoint(), grpc::InsecureChannelCredentials());
        if (!wait_for_channel_ready(lpc_channel, CHANNEL_READY_TIMEOUT)) {
            EVLOG_error << "Connection to LPC use case gRPC server failed.";
            return false;
        }

        m_lpc_handler->set_stub(cs_lpc::ControllableSystemLPCControl::NewStub(lpc_channel));
        m_lpc_handler->configure_use_case();
        m_event_reader = std::make_unique<UseCaseEventReader>(
            m_control_service_stub,
            [this](const control_service::SubscribeUseCaseEventsResponse& response) {
                m_handler.add_action([this, response] { m_lpc_handler->handle_event(response); });
            },
            [this] { m_handler.add_action([this] { handle_event(EebusConnectionEvents::DISCONNECTED); }); });

        common_types::EntityAddress entity_address_for_event_reader = common_types::CreateEntityAddress({1});
        m_event_reader->start(entity_address_for_event_reader, use_case_info);

        m_discovery_event_reader = std::make_unique<DiscoveryEventReader>(
            m_control_service_stub,
            [this](const control_service::DiscoveryEvent& evt) {
                m_handler.add_action([this, evt] { on_discovery_event(evt); });
            },
            [this] { m_handler.add_action([this] { handle_event(EebusConnectionEvents::DISCONNECTED); }); });
        m_discovery_event_reader->start();
        break;
    }

    return true;
}

void EebusConnectionHandler::handle_event(EebusConnectionEvents event) {
    switch (event) {
    case EebusConnectionEvents::CONNECTED:
        if (State::INIT == m_state || State::DISCONNECTED == m_state) {
            m_state = State::ADDING_USE_CASES;
        }
        break;
    case EebusConnectionEvents::DISCONNECTED:
        m_state = State::DISCONNECTED;
        reset();
        EVLOG_info << "Disconnected from EEBUS gRPC service. Will try to reconnect in "
                   << m_config->get_reconnect_delay_s() << " seconds.";
        m_reconnection_timer.set_timeout(std::chrono::seconds(m_config->get_reconnect_delay_s()));
        m_handler.register_event_handler(&m_reconnection_timer, [this](auto&) { reconnect(); });
        break;
    case EebusConnectionEvents::STARTED:
        if (State::READY_TO_START == m_state) {
            m_state = State::RUNNING;
            m_state_machine_timer.set_timeout(std::chrono::seconds(1));
            m_handler.register_event_handler(&m_state_machine_timer, [this](auto&) {
                if (m_lpc_handler) {
                    m_lpc_handler->run_state_machine();
                }
            });
        }
        break;
    case EebusConnectionEvents::DONE_ADDING_USE_CASES:
        if (State::ADDING_USE_CASES == m_state) {
            m_state = State::READY_TO_START;
            m_handler.add_action([this] { start_service(); });
        }
        break;
    }
}

void EebusConnectionHandler::done_adding_use_case() {
    m_handler.add_action([this] { handle_event(EebusConnectionEvents::DONE_ADDING_USE_CASES); });
}

void EebusConnectionHandler::stop() {
    m_stop_requested = true;
    if (m_event_reader) {
        m_event_reader->stop();
    }
    if (m_discovery_event_reader) {
        m_discovery_event_reader->stop();
    }
}

void EebusConnectionHandler::on_discovery_event(const control_service::DiscoveryEvent& evt) {
    if (evt.type() != control_service::DiscoveryEvent_Type_DISCOVERED) {
        EVLOG_debug << "Discovery event type=" << evt.type() << " ski=" << evt.remote_ski();
        return;
    }
    const std::string& ski = evt.remote_ski();

    if (evt.is_trusted()) {
        EVLOG_debug << "Discovery: sidecar already trusts SKI=" << ski << " (" << evt.brand() << " " << evt.model()
                    << ")";
        return;
    }

    const auto& allowlist = m_config->get_effective_ems_ski_allowlist();
    const bool in_allowlist = allowlist.count(ski) > 0;

    if (in_allowlist) {
        EVLOG_info << "Discovery: allowlisted EG appeared SKI=" << ski << " (" << evt.brand() << " " << evt.model()
                   << "); registering";
        register_remote_ski_runtime(ski);
        return;
    }

    if (m_config->get_accept_unknown_ems()) {
        EVLOG_warning << "Discovery: unknown EG auto-accepted SKI=" << ski << " (" << evt.brand() << " " << evt.model()
                      << ")"
                      << " (accept_unknown_ems=true)";
        register_remote_ski_runtime(ski);
        return;
    }

    EVLOG_info << "Discovery: unknown EG ignored SKI=" << ski << " (" << evt.brand() << " " << evt.model() << ")"
               << " (not in allowlist; accept_unknown_ems=false)";
}

void EebusConnectionHandler::register_remote_ski_runtime(const std::string& ski) {
    control_service::RegisterRemoteSkiRequest request;
    request.set_remote_ski(ski);
    control_service::EmptyResponse response;
    auto status = control_service::CallRegisterRemoteSki(m_control_service_stub, request, &response);
    if (!status.ok()) {
        EVLOG_warning << "runtime register_remote_ski(" << ski << ") failed: " << status.error_message();
    }
}

bool EebusConnectionHandler::wait_for_channel_ready(const std::shared_ptr<grpc::Channel>& channel,
                                                    std::chrono::milliseconds timeout) {
    constexpr auto POLL_INTERVAL = std::chrono::seconds(1);
    auto deadline = std::chrono::system_clock::now() + timeout;
    grpc_connectivity_state state = channel->GetState(true);

    while (state != GRPC_CHANNEL_READY) {
        if (m_stop_requested) {
            EVLOG_info << "Channel wait interrupted by shutdown.";
            return false;
        }

        // Check if deadline has already passed
        if (std::chrono::system_clock::now() >= deadline) {
            EVLOG_error << "Channel is not ready after timeout.";
            return false;
        }

        if (!channel->WaitForStateChange(state, std::chrono::system_clock::now() + POLL_INTERVAL)) {
            continue;
        }
        // State change detected, update state
        state = channel->GetState(true);
    }

    return true;
}

} // namespace module
