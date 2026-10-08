// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Connection and life-cycle management for the EEBUS gRPC control service

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <ConfigValidator.hpp>
#include <control_service/control_service.grpc-ext.pb.h>
#include <grpcpp/grpcpp.h>
#include <lpc/LpcUseCaseHandler.hpp>

#include <DiscoveryEventReader.hpp>
#include <EebusCallbacks.hpp>
#include <UseCaseEventReader.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

namespace module {

/// \brief EEBUS use cases this module can register with the control service.
enum class EebusUseCase {
    LPC
};

/// \brief Events driving the connection state machine.
enum class EebusConnectionEvents {
    CONNECTED,
    DISCONNECTED,
    DONE_ADDING_USE_CASES,
    STARTED
};

/// \brief States of the connection to the EEBUS gRPC control service.
enum class State {
    INIT,
    CONNECTED,
    DISCONNECTED,
    ADDING_USE_CASES,
    READY_TO_START,
    RUNNING
};

/// \brief Owns the gRPC channel to the EEBUS control service, registers use cases,
///        and drives reconnection when the service goes away. Integrates into the
///        module's fd event loop via fd_event_sync_interface.
class EebusConnectionHandler : public everest::lib::io::event::fd_event_sync_interface {
public:
    /// \brief connect to the control service described by the validated configuration
    /// \param[in] config - validated module configuration
    explicit EebusConnectionHandler(std::shared_ptr<ConfigValidator> config);
    ~EebusConnectionHandler() override;
    EebusConnectionHandler(const EebusConnectionHandler&) = delete;
    EebusConnectionHandler& operator=(const EebusConnectionHandler&) = delete;
    EebusConnectionHandler(EebusConnectionHandler&&) = delete;
    EebusConnectionHandler& operator=(EebusConnectionHandler&&) = delete;

    everest::lib::io::event::sync_status sync() override;
    int get_poll_fd() override;

    /// \brief register a use case with the control service
    /// \param[in] use_case - the use case to add
    /// \param[in] callbacks - callbacks the use case handler publishes results through
    /// \returns true when the use case was registered successfully
    bool add_use_case(EebusUseCase use_case, const EebusCallbacks& callbacks);
    /// \brief signal that all use cases are registered; starts the service
    void done_adding_use_case();
    /// \brief stop event streams and interrupt any in-progress channel wait
    void stop();

private:
    void start_service();
    void handle_event(EebusConnectionEvents event);
    bool initialize_connection();
    bool create_channel_and_stub();
    bool configure_service();
    void reconnect();
    void reset();
    bool wait_for_channel_ready(const std::shared_ptr<grpc::Channel>& channel, std::chrono::milliseconds timeout);

    void on_discovery_event(const control_service::DiscoveryEvent& evt);
    void register_remote_ski_runtime(const std::string& ski);

    std::shared_ptr<ConfigValidator> m_config;
    std::unique_ptr<lpc::LpcUseCaseHandler> m_lpc_handler;

    std::unique_ptr<UseCaseEventReader> m_event_reader;
    std::unique_ptr<DiscoveryEventReader> m_discovery_event_reader;

    std::shared_ptr<grpc::Channel> m_control_service_channel;
    std::shared_ptr<control_service::ControlService::Stub> m_control_service_stub;
    everest::lib::io::event::fd_event_handler m_handler;
    State m_state{State::INIT};

    everest::lib::io::event::timer_fd m_state_machine_timer;
    everest::lib::io::event::timer_fd m_reconnection_timer;

    std::atomic<bool> m_stop_requested{false};

    // store last added use case for reconnection
    EebusUseCase m_last_use_case{EebusUseCase::LPC};
    EebusCallbacks m_last_callbacks;
    bool m_use_case_added{false};
};

} // namespace module
