// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/slac/slac.hpp>
#include <everest/slac/slac_socket.hpp>

namespace everest::lib::slac {
class SlacEvent : public everest::lib::io::event::fd_event_register_interface {
public:
    using HomeplugMessage = slac_client::ClientPayloadT;
    using HomeplugMessageHandler = std::function<void(HomeplugMessage const&)>;
    using HomeplugErrorHandler = std::function<void(bool, std::string const&)>;
    using HomeplugReadyHandler = std::function<void()>;
    using MacAddress = slac_socket::MacAddress;

    // Consecutive rejected sends after which the error callback reports the link as failing, even
    // though the socket itself shows no error: a modem that stopped draining would otherwise just
    // fill the transmit queue while the state machine waits for confirmations that cannot come.
    static constexpr unsigned TX_FAILURE_THRESHOLD = 16;

    SlacEvent(std::string const& if_name);
    // Queue \p msg for transmission; false if the socket does not accept it right now.
    //
    // Never calls the error callback itself: send() runs inside the consumer's state machine, and
    // the consumer holds its own locks there. The threshold report is queued on the event handler
    // this object is registered with and runs from the loop's action queue, after the current
    // dispatch has returned. The report also reopens the socket, so recovery arrives as the ready
    // callback of the new connection, like after a socket error. Reported once per connection:
    // the record starts over whenever a connection comes up. Without a registered handler the
    // rejections are counted and the report is made once one is registered and the threshold is
    // hit again.
    bool send(HomeplugMessage& msg);

    // Interface MAC: read at construction when possible, refreshed whenever the socket becomes
    // ready, before the ready callback runs. All zero until then.
    const uint8_t* get_mac_addr();

    void set_callback(HomeplugMessageHandler const& callback);
    void set_error_callback(HomeplugErrorHandler const& callback);
    // Called once per connection, after the interface MAC was refreshed from the open socket.
    void set_ready_callback(HomeplugReadyHandler const& callback);

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;

private:
    void handle_socket_error(int id, std::string const& msg);
    void handle_socket_rx(HomeplugMessage const& data, slac_client::interface& client);
    void handle_socket_ready();
    void handle_error_timer();
    void refresh_mac_address();
    void report_tx_fault();

    slac_client m_connection;
    io::event::timer_fd m_error_timer;
    // The handler this object is registered with; set in register_events, cleared in
    // unregister_events. Carries the deferred transmit-fault report.
    everest::lib::io::event::fd_event_handler* m_handler{nullptr};
    bool m_on_error{false};
    unsigned m_tx_failures{0};
    bool m_tx_fault_reported{false};
    std::string m_error_detail;
    HomeplugMessageHandler m_callback;
    HomeplugErrorHandler m_error_cb;
    HomeplugReadyHandler m_ready_cb;
    MacAddress m_mac_address{};
    std::string m_if_name;
};
} // namespace everest::lib::slac
