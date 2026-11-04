// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/uds/uds_utils.hpp>

#include <optional>
#include <utility>

namespace {

using everest::lib::io::event::unique_fd;
using everest::lib::io::uds::uds_payload;

// The message every send_fd overload sends, or nothing when it cannot be built: a descriptor that
// cannot be duplicated or is empty, or metadata past what a payload may carry.
std::optional<uds_payload> fd_message(int fd, std::string const& metadata) {
    uds_payload payload;
    if (not payload.set_message(metadata) or not payload.attach_duplicate(fd)) {
        return std::nullopt;
    }
    return payload;
}

std::optional<uds_payload> fd_message(unique_fd&& fd, std::string const& metadata) {
    uds_payload payload;
    if (not payload.set_message(metadata) or not payload.attach(std::move(fd))) {
        return std::nullopt;
    }
    return payload;
}

template <class SenderT, class FdT> bool send_through(SenderT& sender, FdT&& fd, std::string const& metadata) {
    auto message = fd_message(std::forward<FdT>(fd), metadata);
    return message.has_value() and sender.tx(*message);
}

} // namespace

namespace everest::lib::io::uds {

bool send_fd(uds_client_socket& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_client_socket& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

bool send_fd(uds_server_socket& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_server_socket& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

bool send_fd(uds_client_interface& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_client_interface& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

bool send_fd(uds_server_interface& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_server_interface& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

bool send_fd(uds_seqpacket_socket_base& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_seqpacket_socket_base& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

bool send_fd(uds_seqpacket_client_interface& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_seqpacket_client_interface& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

bool send_fd(uds_seqpacket_peer_interface& sender, int fd, std::string const& metadata) {
    return send_through(sender, fd, metadata);
}

bool send_fd(uds_seqpacket_peer_interface& sender, event::unique_fd&& fd, std::string const& metadata) {
    return send_through(sender, std::move(fd), metadata);
}

} // namespace everest::lib::io::uds
