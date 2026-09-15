// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/slac/HomeplugMessage.hpp>

namespace module::main {

// The PLC link as the runtime sees it. Production is a raw socket on the PLC interface (SlacEventIo);
// tests substitute an in-memory fake. Callbacks run on the event-loop thread the object is registered
// with: rx for every received frame, ready whenever the link (re)connected, error on loss/recovery.
class SlacIo : public everest::lib::io::event::fd_event_register_interface {
public:
    using Frame = everest::lib::slac::messages::HomeplugMessage;
    using RxHandler = std::function<void(Frame const&)>;
    using ErrorHandler = std::function<void(bool on_error, std::string const& detail)>;
    using ReadyHandler = std::function<void()>;

    ~SlacIo() override = default;

    // Queue \p frame for transmission; false if the link does not accept it right now.
    virtual bool send(Frame& frame) = 0;
    // MAC of the local interface; valid once ready fired, all zero before that if not yet readable.
    virtual const std::uint8_t* mac_address() = 0;

    virtual void set_rx_handler(RxHandler handler) = 0;
    virtual void set_error_handler(ErrorHandler handler) = 0;
    virtual void set_ready_handler(ReadyHandler handler) = 0;
};

using SlacIoFactory = std::function<std::unique_ptr<SlacIo>(std::string const& device)>;

// Production I/O: SlacEvent on the configured interface.
std::unique_ptr<SlacIo> make_plc_socket_io(std::string const& device);

} // namespace module::main
