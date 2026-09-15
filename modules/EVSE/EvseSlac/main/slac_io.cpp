// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "slac_io.hpp"

#include <everest/slac/slac_event.hpp>

namespace module::main {

namespace {

class SlacEventIo final : public SlacIo {
public:
    explicit SlacEventIo(std::string const& device) : impl(device) {
    }
    bool send(Frame& frame) override {
        return impl.send(frame);
    }
    const std::uint8_t* mac_address() override {
        return impl.get_mac_addr();
    }
    void set_rx_handler(RxHandler handler) override {
        impl.set_callback(std::move(handler));
    }
    void set_error_handler(ErrorHandler handler) override {
        impl.set_error_callback(std::move(handler));
    }
    void set_ready_handler(ReadyHandler handler) override {
        impl.set_ready_callback(std::move(handler));
    }
    bool register_events(everest::lib::io::event::fd_event_handler& handler) override {
        return impl.register_events(handler);
    }
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override {
        return impl.unregister_events(handler);
    }

private:
    everest::lib::slac::SlacEvent impl;
};

} // namespace

std::unique_ptr<SlacIo> make_plc_socket_io(std::string const& device) {
    return std::make_unique<SlacEventIo>(device);
}

} // namespace module::main
