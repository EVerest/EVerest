// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/utils.hpp>
#include <functional>
#include <string>

namespace everest::slac::io {

class slac_socket {
public:
    using PayloadT = messages::HomeplugMessage;
    using MacAddress = messages::HomeplugMessage::MacAddress;

    slac_socket() = default;
    ~slac_socket() = default;

    bool open(std::string const& if_name);
    void close();

    bool tx(PayloadT const& payload);
    bool rx(PayloadT& buffer);

    int get_fd() const;
    int get_error() const;
    std::string get_error_message() const;
    bool is_open() const;
    MacAddress get_mac_address() const;

private:
    ::everest::lib::io::event::unique_fd m_fd{};
    // Only open() writes this, so without the initialiser get_mac_address() on an unopened socket
    // returns indeterminate bytes. SlacEvent::m_mac_address records the same contract.
    MacAddress m_mac{};
    int m_error_code{0};
    std::string m_error_message{};
};

using slac_client = ::everest::lib::io::event::fd_event_client<slac_socket>::type;

} // namespace everest::slac::io
