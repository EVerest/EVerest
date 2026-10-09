// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/slac/slac.hpp>
#include <string>

namespace everest::lib::slac {

class slac_socket {
public:
    using PayloadT = messages::HomeplugMessage;
    using MacAddress = messages::HomeplugMessage::MacAddress;

    slac_socket() = default;
    ~slac_socket() = default;

    /**
     * @brief Open the PLC socket on \p if_name, non-blocking, with its send buffer at the minimum
     *        so a modem that holds frames makes the fd not writable instead of taking them into a
     *        device queue the client cannot see.
     */
    bool open(std::string const& if_name);
    void close();

    /**
     * @brief Send one frame.
     * @return True when it went out or can never go out (an invalid frame is dropped); false to
     *         retry once the fd is writable (back-pressure, see \ref io::socket::is_send_backpressure)
     *         or, with \ref get_error set, because the send failed.
     */
    bool tx(PayloadT const& payload);
    bool rx(PayloadT& buffer);

    int get_fd() const;
    int get_error() const;
    std::string get_error_message() const;
    bool is_open() const;
    MacAddress get_mac_address() const;

private:
    io::event::unique_fd m_fd;
    MacAddress m_mac;
    int m_error_code{0};
    std::string m_error_message;
};

using slac_client = io::event::fd_event_client<slac_socket>::type;

} // namespace everest::lib::slac
