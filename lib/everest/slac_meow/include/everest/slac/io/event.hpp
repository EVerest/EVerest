// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/slac/io/socket.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/utils.hpp>

namespace everest::slac::io {
class SlacEvent : public everest::lib::io::event::fd_event_register_interface {
public:
    using HomeplugMessage = slac_client::ClientPayloadT;
    using HomeplugMessageHandler = std::function<void(HomeplugMessage const&)>;
    using HomeplugErrorHandler = std::function<void(bool, std::string const&)>;
    using HomeplugReadyHandler = std::function<void()>;
    using MacAddress = slac_socket::MacAddress;

    SlacEvent(std::string const& if_name);
    bool send(HomeplugMessage& msg);

    const uint8_t* get_mac_addr();

    void set_callback(HomeplugMessageHandler const& callback);
    void set_error_callback(HomeplugErrorHandler const& callback);
    void set_ready_callback(HomeplugReadyHandler const& callback);

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;

private:
    void handle_socket_error(int id, std::string const& msg);
    void handle_socket_rx(HomeplugMessage const& data, slac_client::interface& client);
    void handle_error_timer();

    slac_client m_connection;
    ::everest::lib::io::event::timer_fd m_error_timer;
    bool m_on_error{false};
    std::string m_error_detail;
    HomeplugMessageHandler m_callback;
    HomeplugErrorHandler m_error_cb;
    // The interface MAC lookup in the constructor is best effort - the PLC device may enumerate
    // after module start - and its failure is swallowed. Without this initialiser get_mac_addr()
    // hands indeterminate bytes to the state machine; the contract is that a missing interface
    // yields an all-zero MAC, which the module re-captures from the I/O ready callback.
    MacAddress m_mac_address{};
    std::string m_if_name;
};
} // namespace everest::slac::io
