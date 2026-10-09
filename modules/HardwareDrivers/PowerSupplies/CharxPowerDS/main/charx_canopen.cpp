// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "charx_canopen.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace charx {

namespace {
const char* const flag_names[32] = {"CanValue",          "PfcOff",          "DcOutputSideOff",  "InternalFailure",
                                    "AcAsymmetry",       "AcPhaseLoss",     "LoadSharing",      "ModuleIdRepetition",
                                    "Derating",          "CanInterruption", "UvpInput",         "UvpOutput",
                                    "OvpInput",          "OvpOutput",       "UnderTemperature", "OverTemperature",
                                    "OverCurrent",       "ShortCircuit",    "OverPower",        "FanFault",
                                    "DischargeProblem",  "EmergencyStop",   "AcOverload",       "VoltageLimit",
                                    "CurrentLimit",      "PowerLimit",      "RemoteOff",        "DisplayCommunication",
                                    "ConsumerHeartbeat", "PowerOn",         "BootUp",           "ConverterError"};

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}

Frame sdo_request(uint8_t command, Obj o, int32_t value) {
    Frame f{command, static_cast<uint8_t>(o.index & 0xFF), static_cast<uint8_t>(o.index >> 8), o.sub, 0, 0, 0, 0};
    const auto v = static_cast<uint32_t>(value);
    for (int b = 0; b < 4; b++) {
        f[4 + b] = static_cast<uint8_t>(v >> (8 * b));
    }
    return f;
}
} // namespace

std::string flags1_to_string(uint32_t flags) {
    std::string s;
    for (int bit = 0; bit < 32; bit++) {
        if (flags & (1u << bit)) {
            if (!s.empty()) {
                s += ",";
            }
            s += flag_names[bit];
        }
    }
    return s.empty() ? "-" : s;
}

std::string SdoResult::describe() const {
    char buf[96];
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::Timeout:
        return "SDO timeout";
    case Status::IoError:
        return "CAN I/O error";
    case Status::Protocol:
        return "unexpected SDO response";
    case Status::Abort:
        std::snprintf(buf, sizeof(buf), "SDO abort 0x%08X%s", abort_code,
                      abort_code == ABORT_DEVICE_STATE ? " (device state)"
                      : abort_code == ABORT_LENGTH     ? " (length)"
                                                       : "");
        return buf;
    }
    return "?";
}

Frame sdo_upload_request(Obj o) {
    return sdo_request(0x40, o, 0);
}

Frame sdo_download_request(Obj o, int32_t value) {
    return sdo_request(0x22, o, value);
}

std::optional<SdoResult> parse_sdo_response(const uint8_t* data, std::size_t len, Obj o, bool upload) {
    if (len < 8) {
        return std::nullopt;
    }
    const uint16_t index = static_cast<uint16_t>(data[1] | (data[2] << 8));
    if (index != o.index || data[3] != o.sub) {
        return std::nullopt;
    }
    const uint32_t raw = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
                         (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
    const uint8_t cs = data[0];
    SdoResult r;
    if (cs == 0x80) {
        r.status = SdoResult::Status::Abort;
        r.abort_code = raw;
        return r;
    }
    if (upload) {
        // expedited upload response: ccs 2 (0x40), e = bit 1; s = bit 0 makes n (bits 2-3) the unused bytes
        if ((cs & 0xE0) != 0x40 || (cs & 0x02) == 0) {
            r.status = SdoResult::Status::Protocol; // segmented upload: not used by this module
            return r;
        }
        uint32_t v = raw;
        if (cs & 0x01) {
            const int size = 4 - ((cs >> 2) & 0x03);
            if (size < 4) {
                v &= (1u << (8 * size)) - 1u;
            }
        }
        r.status = SdoResult::Status::Ok;
        r.value = static_cast<int32_t>(v);
        return r;
    }
    if (cs != 0x60) {
        r.status = SdoResult::Status::Protocol;
        return r;
    }
    r.status = SdoResult::Status::Ok;
    return r;
}

Canopen::Canopen(const std::string& interface_name, uint8_t node_id_, uint8_t master_node_id_,
                 std::chrono::milliseconds sdo_timeout_) :
    node_id(node_id_), master_node_id(master_node_id_), sdo_timeout(sdo_timeout_) {
    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        throw_errno("CAN socket");
    }
    try {
        struct ifreq ifr {};
        if (interface_name.size() >= sizeof(ifr.ifr_name)) {
            throw std::runtime_error("CAN interface name too long: " + interface_name);
        }
        std::strncpy(ifr.ifr_name, interface_name.c_str(), sizeof(ifr.ifr_name) - 1);
        if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
            throw_errno("CAN interface " + interface_name);
        }
        // Only the node's SDO responses are of interest; everything else (heartbeats, emergencies, other
        // masters) stays out of the receive queue.
        struct can_filter filter {};
        filter.can_id = 0x580 + node_id;
        filter.can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
        if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
            throw_errno("CAN filter");
        }
        struct sockaddr_can addr {};
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw_errno("CAN bind " + interface_name);
        }
    } catch (...) {
        close(fd);
        throw;
    }
    heartbeat_thread = std::thread(&Canopen::heartbeat_loop, this);
}

Canopen::Canopen(int socket_fd, uint8_t node_id_, uint8_t master_node_id_, std::chrono::milliseconds sdo_timeout_) :
    fd(socket_fd), node_id(node_id_), master_node_id(master_node_id_), sdo_timeout(sdo_timeout_) {
    heartbeat_thread = std::thread(&Canopen::heartbeat_loop, this);
}

Canopen::~Canopen() {
    stop = true;
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }
    if (fd >= 0) {
        close(fd);
    }
}

bool Canopen::send(uint32_t can_id, const uint8_t* data, uint8_t len) {
    struct can_frame frame {};
    frame.can_id = can_id;
    frame.can_dlc = len;
    std::memcpy(frame.data, data, len);
    std::lock_guard<std::mutex> lock(send_mutex);
    // MSG_NOSIGNAL: a socket whose peer is gone must fail the send, not raise SIGPIPE
    return ::send(fd, &frame, sizeof(frame), MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(frame));
}

void Canopen::heartbeat_loop() {
    // Master heartbeat, state operational. The module supervises it (0x1016:01). A failed send is not an
    // error of its own: the SDO transfers report the bus problem.
    const uint8_t operational = 0x05;
    while (!stop) {
        send(0x700 + master_node_id, &operational, 1);
        for (int i = 0; i < 10 && !stop; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool Canopen::start_remote_node() {
    const uint8_t cmd[2] = {0x01, node_id};
    return send(0x000, cmd, 2);
}

SdoResult Canopen::transfer(const Frame& request, Obj o, bool upload) {
    std::lock_guard<std::mutex> lock(sdo_mutex);
    SdoResult result;

    // drop stale responses (e.g. a late answer to a timed-out request)
    struct can_frame frame {};
    while (recv(fd, &frame, sizeof(frame), MSG_DONTWAIT) > 0) {
    }

    if (!send(0x600 + node_id, request.data(), 8)) {
        io_errors++;
        result.status = SdoResult::Status::IoError;
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + sdo_timeout;
    while (true) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            io_errors = 0;
            result.status = SdoResult::Status::Timeout;
            return result;
        }
        struct pollfd pfd {
            fd, POLLIN, 0
        };
        const int pr = poll(&pfd, 1, static_cast<int>(left.count()));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            io_errors++;
            result.status = SdoResult::Status::IoError;
            return result;
        }
        if (pr == 0) {
            continue;
        }
        const ssize_t n = recv(fd, &frame, sizeof(frame), 0);
        if (n != static_cast<ssize_t>(sizeof(frame))) {
            continue;
        }
        const auto parsed = parse_sdo_response(frame.data, frame.can_dlc, o, upload);
        if (parsed) {
            io_errors = 0;
            return *parsed;
        }
    }
}

SdoResult Canopen::read(Obj o) {
    return transfer(sdo_upload_request(o), o, true);
}

SdoResult Canopen::write(Obj o, int32_t value) {
    return transfer(sdo_download_request(o, value), o, false);
}

} // namespace charx
