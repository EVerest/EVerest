// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace everest::telemetry {

/// The manager places the bound telemetry socket on this descriptor in the telemetry receiver's process.
inline constexpr int RECEIVER_FD = 3;

enum class SendResult {
    Ok,
    WouldBlock,
    NoReceiver,
    TooBig,
    Error,
};

std::string_view to_string(SendResult result);

/// \brief Fire-and-forget datagram transport; send() never blocks and never throws
class DatagramSender {
public:
    virtual ~DatagramSender() = default;
    virtual SendResult send(const std::uint8_t* data, std::size_t size) = 0;
};

/// \brief Sender to the Unix-domain datagram socket at \p path
/// \throws std::runtime_error if the path is invalid or no socket can be created
std::unique_ptr<DatagramSender> make_uds_datagram_sender(const std::string& path);

/// \brief Bind the receiving Unix-domain datagram socket at \p path
/// \details A stale socket file at \p path is replaced; any other kind of file makes the call fail. The descriptor
/// is close-on-exec and owned by the caller.
/// \param mode permission bits of the socket file
/// \throws std::runtime_error if the path is invalid or the socket cannot be bound
int bind_receiver_socket(const std::string& path, std::uint32_t mode);

/// \returns the path \p fd is bound to, if it is a Unix-domain datagram socket bound to a filesystem path
std::optional<std::string> bound_receiver_socket_path(int fd);

} // namespace everest::telemetry
