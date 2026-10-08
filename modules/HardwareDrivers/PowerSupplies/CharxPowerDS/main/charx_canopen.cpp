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
    case Status::Abort:
        std::snprintf(buf, sizeof(buf), "SDO abort 0x%08X%s", abort_code,
                      abort_code == 0x08000022   ? " (device state)"
                      : abort_code == 0x06070010 ? " (length)"
                                                 : "");
        return buf;
    }
    return "?";
}

Canopen::Canopen(const std::string& interface_name, uint8_t node_id_, uint8_t master_node_id_,
                 std::chrono::milliseconds sdo_timeout_) :
    node_id(node_id_), master_node_id(master_node_id_), sdo_timeout(sdo_timeout_) {
    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        throw_errno("CAN socket");
    }
    struct ifreq ifr {};
    if (interface_name.size() >= sizeof(ifr.ifr_name)) {
        throw std::runtime_error("CAN interface name too long: " + interface_name);
    }
    std::strncpy(ifr.ifr_name, interface_name.c_str(), sizeof(ifr.ifr_name) - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        throw_errno("CAN interface " + interface_name);
    }
    // Only the node's SDO replies are of interest; everything else (heartbeats,
    // emergencies, other masters) stays out of the receive queue.
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

void Canopen::send(uint32_t can_id, const uint8_t* data, uint8_t len) {
    struct can_frame frame {};
    frame.can_id = can_id;
    frame.can_dlc = len;
    std::memcpy(frame.data, data, len);
    std::lock_guard<std::mutex> lock(send_mutex);
    if (::write(fd, &frame, sizeof(frame)) != sizeof(frame)) {
        throw_errno("CAN write");
    }
}

void Canopen::heartbeat_loop() {
    // Master heartbeat, state operational. The module supervises it (0x1016:01).
    const uint8_t operational = 0x05;
    while (!stop) {
        try {
            send(0x700 + master_node_id, &operational, 1);
        } catch (const std::exception&) {
            // bus off / interface down: keep trying, the SDO layer reports the fault
        }
        for (int i = 0; i < 10 && !stop; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Canopen::nmt_start_remote_node() {
    const uint8_t cmd[2] = {0x01, node_id};
    send(0x000, cmd, 2);
}

SdoResult Canopen::transfer(const uint8_t request[8], Obj o) {
    std::lock_guard<std::mutex> lock(sdo_mutex);
    SdoResult result;

    // drop stale replies (e.g. a late answer to a timed-out request)
    struct can_frame frame {};
    while (recv(fd, &frame, sizeof(frame), MSG_DONTWAIT) > 0) {
    }

    try {
        send(0x600 + node_id, request, 8);
    } catch (const std::exception&) {
        result.status = SdoResult::Status::IoError;
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + sdo_timeout;
    while (true) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
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
            result.status = SdoResult::Status::IoError;
            return result;
        }
        if (pr == 0) {
            continue;
        }
        const ssize_t n = recv(fd, &frame, sizeof(frame), 0);
        if (n != sizeof(frame) || frame.can_dlc < 8) {
            continue;
        }
        const uint16_t index = frame.data[1] | (frame.data[2] << 8);
        if (index != o.index || frame.data[3] != o.sub) {
            continue;
        }
        const uint8_t cs = frame.data[0];
        int32_t value = 0;
        std::memcpy(&value, &frame.data[4], 4);
        if (cs == 0x80) {
            result.status = SdoResult::Status::Abort;
            result.abort_code = static_cast<uint32_t>(value);
            return result;
        }
        // expedited upload: size from command specifier (n = unused bytes)
        if ((cs & 0xE0) == 0x40 && (cs & 0x03) == 0x03) {
            const int size = 4 - ((cs >> 2) & 0x03);
            if (size < 4) {
                value &= static_cast<int32_t>((1u << (8 * size)) - 1);
            }
        }
        result.status = SdoResult::Status::Ok;
        result.value = value;
        return result;
    }
}

SdoResult Canopen::read(Obj o) {
    const uint8_t req[8] = {
        0x40, static_cast<uint8_t>(o.index & 0xFF), static_cast<uint8_t>(o.index >> 8), o.sub, 0, 0, 0, 0};
    return transfer(req, o);
}

SdoResult Canopen::write(Obj o, int32_t value) {
    uint8_t req[8] = {0x22, static_cast<uint8_t>(o.index & 0xFF), static_cast<uint8_t>(o.index >> 8), o.sub, 0, 0, 0,
                      0};
    std::memcpy(&req[4], &value, 4);
    return transfer(req, o);
}

} // namespace charx
