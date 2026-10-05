// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// clang-format off
#include <everest/io/netlink/vcan_netlink_manager.hpp>
#include <linux/netlink.h>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <net/if.h>
#include <linux/can.h>
#include <linux/if_arp.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
// clang-format on

namespace everest::lib::io::netlink {

struct vcan_netlink_manager::NetlinkMessage {
    struct nlmsghdr header;
    char buffer[4096];
};

void vcan_netlink_manager::add_attribute(nlmsghdr* nlh, int maxlen, int type, const void* data, int len) {
    int attrlen = RTA_LENGTH(len);
    if (NLMSG_ALIGN(nlh->nlmsg_len) + attrlen > static_cast<uint32_t>(maxlen)) {
        throw std::runtime_error("Netlink attribute too long for message buffer.");
    }
    struct rtattr* rta = (struct rtattr*)(((char*)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = attrlen;
    memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + attrlen;
}

vcan_netlink_manager::vcan_netlink_manager() {
    m_nl_socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (m_nl_socket_fd < 0) {
        throw std::runtime_error("Failed to create shared Netlink socket: " + std::string(strerror(errno)));
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(m_nl_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        ::close(m_nl_socket_fd);
        m_nl_socket_fd = -1;
        throw std::runtime_error("Could not set receive timeout on shared Netlink socket: " +
                                 std::string(strerror(errno)));
    }
}

vcan_netlink_manager::~vcan_netlink_manager() {
    if (m_nl_socket_fd != -1) {
        ::close(m_nl_socket_fd);
        m_nl_socket_fd = -1;
    }
}

vcan_netlink_manager& vcan_netlink_manager::Instance() {
    static vcan_netlink_manager obj;
    return obj;
}

void vcan_netlink_manager::send_message(NetlinkMessage const& msg, int flags) {
    iovec iov;
    iov.iov_base = (void*)&msg.header;
    iov.iov_len = msg.header.nlmsg_len;

    msghdr message;
    message.msg_name = nullptr;
    message.msg_namelen = 0;
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = nullptr;
    message.msg_controllen = 0;
    message.msg_flags = 0;

    ssize_t bytes_sent = sendmsg(m_nl_socket_fd, &message, flags);
    if (bytes_sent < 0) {
        throw std::runtime_error("Failed to send Netlink message: " + std::string(strerror(errno)));
    }
    if (static_cast<std::uint32_t>(bytes_sent) != msg.header.nlmsg_len) {
        throw std::runtime_error("Partial Netlink message sent. Expected " + std::to_string(msg.header.nlmsg_len) +
                                 " bytes, sent " + std::to_string(bytes_sent));
    }
}

void vcan_netlink_manager::receive_message(NetlinkMessage& msg) {
    iovec iov;
    iov.iov_base = (void*)&msg.header;
    iov.iov_len = sizeof(msg);

    msghdr message;
    message.msg_name = nullptr;
    message.msg_namelen = 0;
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = nullptr;
    message.msg_controllen = 0;
    message.msg_flags = 0;

    ssize_t bytes_received = recvmsg(m_nl_socket_fd, &message, 0); // Use shared FD
    if (bytes_received < 0) {
        throw std::runtime_error("Failed to receive Netlink message: " + std::string(strerror(errno)));
    }
    if (bytes_received == 0) {
        throw std::runtime_error("Netlink socket closed during receive.");
    }
    if (bytes_received < static_cast<int>(sizeof(struct nlmsghdr))) {
        throw std::runtime_error("Incomplete Netlink message header.");
    }
}

void vcan_netlink_manager::send_netlink_request_impl(int msg_type, int flags, cb_type const& callback) {
    NetlinkMessage req;
    memset(&req, 0, sizeof(req));

    req.header.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.header.nlmsg_type = msg_type;
    req.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
    req.header.nlmsg_seq = ++s_netlink_seq_counter;
    req.header.nlmsg_pid = getpid();

    struct ifinfomsg* ifi = (struct ifinfomsg*)NLMSG_DATA(&req.header);
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index = 0;
    ifi->ifi_flags = 0;
    ifi->ifi_change = 0xFFFFFFFF;

    callback(&req.header, ifi, sizeof(req.buffer));
    send_message(req);
    await_ack(req, msg_type);
}

namespace {
// NLMSG_ERROR with the errno kept apart from the text.
class netlink_error : public std::runtime_error {
public:
    explicit netlink_error(int negative_errno) :
        std::runtime_error(std::string(strerror(-negative_errno)) + " (error code: " + std::to_string(negative_errno) +
                           ")"),
        m_errnum(-negative_errno) {
    }
    int errnum() const {
        return m_errnum;
    }

private:
    int m_errnum;
};
} // namespace

void vcan_netlink_manager::await_ack(NetlinkMessage const& req, int msg_type) {
    NetlinkMessage response;
    bool ack_received = false;
    int max_recv_attempts = 10;

    for (int i = 0; i < max_recv_attempts; ++i) {
        ssize_t bytes_received = 0;
        try {
            memset(&response, 0, sizeof(response));
            receive_message(response);
            bytes_received = response.header.nlmsg_len;
        } catch (const std::runtime_error& e) {
            continue;
        }

        for (struct nlmsghdr* nlh = &response.header; NLMSG_OK(nlh, bytes_received);
             nlh = NLMSG_NEXT(nlh, bytes_received)) {
            if (static_cast<int>(nlh->nlmsg_pid) != getpid() || nlh->nlmsg_seq != req.header.nlmsg_seq) {
                continue;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr* err = (struct nlmsgerr*)NLMSG_DATA(nlh);
                if (err->error == 0) {
                    ack_received = true;
                    return;
                } else {
                    throw netlink_error(err->error);
                }
            } else if (nlh->nlmsg_type == msg_type) {
                ack_received = true;
                return;
            }
        }
    }

    if (!ack_received) {
        throw std::runtime_error("No valid ACK");
    }
}

bool vcan_netlink_manager::send_netlink_request(int msg_type, int flags, cb_type const& callback,
                                                std::string const& interface_name, std::string const& caller) {
    try {
        send_netlink_request_impl(msg_type, flags, callback);
        return true;
    } catch (std::exception& e) {
        report_error(interface_name, caller, e.what());
    } catch (...) {
        report_error(interface_name, caller, "Unexpected exception");
    }
    return false;
}

void vcan_netlink_manager::set_error_handler(error_handler_type handler) {
    m_error_handler = std::move(handler);
}

void vcan_netlink_manager::report_error(std::string const& interface_name, std::string const& caller,
                                        std::string const& reason) {
    auto const message = "[VCAN Netlink] (" + interface_name + ") '" + caller + "' -> " + reason;
    if (m_error_handler) {
        try {
            m_error_handler(message);
            return;
        } catch (...) {
            // Fall back to the default sink. Reporting runs inside exception handlers and, via
            // destroy(), inside destructors, so a failing handler must not escape from here.
        }
    }
    std::cerr << message << std::endl;
}

bool vcan_netlink_manager::create(std::string const& interface_name) {
    return send_netlink_request(
        RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL,
        [&](struct nlmsghdr* nlh, struct ifinfomsg* ifi, int max_buf_len) {
            ifi->ifi_type = AF_NETROM;

            add_attribute(nlh, max_buf_len, IFLA_IFNAME, interface_name.c_str(), interface_name.length() + 1);

            struct rtattr* linkinfo_attr = (struct rtattr*)(((char*)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
            linkinfo_attr->rta_type = IFLA_LINKINFO;
            linkinfo_attr->rta_len = RTA_LENGTH(0);

            nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(0));

            const char* vcan_kind = "vcan";
            add_attribute(nlh, max_buf_len, IFLA_INFO_KIND, vcan_kind, strlen(vcan_kind) + 1);

            linkinfo_attr->rta_len = (char*)nlh + nlh->nlmsg_len - (char*)linkinfo_attr;
        },
        interface_name, "create");
}

bool vcan_netlink_manager::bring_up(std::string const& interface_name) {
    return send_netlink_request(
        RTM_NEWLINK, 0,
        [&](struct nlmsghdr* nlh, struct ifinfomsg* ifi, int max_buf_len) {
            add_attribute(nlh, max_buf_len, IFLA_IFNAME, interface_name.c_str(), interface_name.length() + 1);
            ifi->ifi_flags = IFF_UP;
            ifi->ifi_change = IFF_UP;
        },
        interface_name, "bringUp");
}

bool vcan_netlink_manager::bring_down(std::string const& interface_name) {
    return send_netlink_request(
        RTM_NEWLINK, 0,
        [&](struct nlmsghdr* nlh, struct ifinfomsg* ifi, int max_buf_len) {
            add_attribute(nlh, max_buf_len, IFLA_IFNAME, interface_name.c_str(), interface_name.length() + 1);
            ifi->ifi_flags = 0;
            ifi->ifi_change = IFF_UP;
        },
        interface_name, "bringDown");
}

namespace {
// Qdisc tree installed on a shaped vcan (see set_transmit_rate_limit):
//   1:  prio, 2 bands, priomap: skb->priority 6/7 -> band 0, everything else -> band 1
//   1:1 pfifo, explicit limit (a vcan has tx_queue_len 0, the default pfifo would drop all)
//   1:2 tbf, the rate limit
// SO_PRIORITY 6 (unshaped_priority) bypasses the shaper, the default priority 0 is shaped.
constexpr std::uint32_t prio_handle = 0x00010000U;    // 1:
constexpr std::uint32_t band_unshaped = 0x00010001U;  // 1:1
constexpr std::uint32_t band_shaped = 0x00010002U;    // 1:2
constexpr std::uint32_t unshaped_pfifo_limit = 4096U; // frames
constexpr std::uint8_t unshaped_priority = 6;         // skb->priority mapped to band 0
} // namespace

std::uint8_t vcan_netlink_manager::unshaped_socket_priority() {
    return unshaped_priority;
}

void vcan_netlink_manager::send_qdisc(int msg_type, unsigned int ifindex, std::uint32_t parent, std::uint32_t handle,
                                      char const* kind, std::function<void(NetlinkMessage&)> const& fill_options) {
    // NLM_F_CREATE | NLM_F_REPLACE with handle 0 is `tc qdisc replace`: create, change parameters of
    // the same kind, or swap a different kind.
    NetlinkMessage req;
    memset(&req, 0, sizeof(req));
    req.header.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
    req.header.nlmsg_type = msg_type;
    req.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    if (msg_type == RTM_NEWQDISC) {
        req.header.nlmsg_flags |= NLM_F_CREATE | NLM_F_REPLACE;
    }
    req.header.nlmsg_seq = ++s_netlink_seq_counter;
    req.header.nlmsg_pid = getpid();
    struct tcmsg* tcm = (struct tcmsg*)NLMSG_DATA(&req.header);
    tcm->tcm_family = AF_UNSPEC;
    tcm->tcm_ifindex = static_cast<int>(ifindex);
    tcm->tcm_handle = handle;
    tcm->tcm_parent = parent;
    if (kind != nullptr) {
        add_attribute(&req.header, sizeof(req.buffer), TCA_KIND, kind, strlen(kind) + 1);
    }
    if (fill_options) {
        fill_options(req);
    }
    send_message(req);
    await_ack(req, msg_type);
}

void vcan_netlink_manager::remove_root_qdisc(unsigned int ifindex) {
    try {
        send_qdisc(RTM_DELQDISC, ifindex, TC_H_ROOT, 0, nullptr, {});
    } catch (netlink_error const& e) {
        // The kernel's default queue has handle 0 and is refused with ENOENT: nothing installed.
        if (e.errnum() != ENOENT) {
            throw;
        }
    }
}

bool vcan_netlink_manager::set_transmit_rate_limit(std::string const& interface_name, std::uint64_t rate_bps,
                                                   std::uint32_t burst_bytes, std::uint32_t limit_bytes) {
    static constexpr char const* caller = "setTransmitRateLimit";
    if (rate_bps == 0 or burst_bytes == 0 or limit_bytes == 0) {
        report_error(interface_name, caller, "rate, burst and limit must be non-zero");
        return false;
    }
    if (burst_bytes < CANFD_MTU) {
        // tbf refuses a bucket below the MTU, or on kernels that only warn, stalls FD frames.
        report_error(interface_name, caller,
                     "burst must be at least the CAN FD MTU of " + std::to_string(CANFD_MTU) + " bytes");
        return false;
    }
    unsigned int const ifindex = ::if_nametoindex(interface_name.c_str());
    if (ifindex == 0) {
        report_error(interface_name, caller, std::string("if_nametoindex failed: ") + std::strerror(errno));
        return false;
    }

    // Only the tbf carries the arguments. With the tree in place this is the whole update; ENOENT
    // (no parent) installs the tree below.
    auto const send_tbf = [&]() {
        send_qdisc(
            RTM_NEWQDISC, ifindex, band_shaped, 0, "tbf",
            [this, rate_bps, burst_bytes, limit_bytes](NetlinkMessage& req) {
                // TCA_OPTIONS is a nest: open it, append the tbf attributes, then fix up its length.
                struct rtattr* options = (struct rtattr*)(((char*)&req.header) + NLMSG_ALIGN(req.header.nlmsg_len));
                options->rta_type = TCA_OPTIONS;
                options->rta_len = RTA_LENGTH(0);
                req.header.nlmsg_len = NLMSG_ALIGN(req.header.nlmsg_len) + RTA_ALIGN(RTA_LENGTH(0));

                // rate in bytes/s; linklayer ETHERNET = plain byte accounting, no rate table. buffer is the
                // bucket in psched ticks (64 ns) for kernels without TCA_TBF_BURST (since 3.13, in bytes).
                struct tc_tbf_qopt qopt;
                memset(&qopt, 0, sizeof(qopt));
                std::uint64_t const rate_bytes = rate_bps / 8U;
                qopt.rate.rate = rate_bytes > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(rate_bytes);
                qopt.rate.linklayer = TC_LINKLAYER_ETHERNET;
                qopt.limit = limit_bytes;
                // bytes * 8e9 / rate / 64 = bytes * 125e6 / rate; below 2^59 for any argument.
                std::uint64_t const burst_ticks = (static_cast<std::uint64_t>(burst_bytes) * 125000000ULL) / rate_bps;
                qopt.buffer = burst_ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(burst_ticks);
                add_attribute(&req.header, sizeof(req.buffer), TCA_TBF_PARMS, &qopt, sizeof(qopt));
                std::uint32_t const burst = burst_bytes;
                add_attribute(&req.header, sizeof(req.buffer), TCA_TBF_BURST, &burst, sizeof(burst));

                options->rta_len = (char*)&req.header + req.header.nlmsg_len - (char*)options;
            });
    };
    try {
        send_tbf();
        return true;
    } catch (netlink_error const& e) {
        if (e.errnum() != ENOENT) {
            report_error(interface_name, caller, e.what());
            return false;
        }
        // No parent 1: to hang the tbf under: install the tree.
    } catch (std::exception& e) {
        report_error(interface_name, caller, e.what());
        return false;
    } catch (...) {
        report_error(interface_name, caller, "Unexpected exception");
        return false;
    }

    // prio root with an explicit handle so its bands can be parents. TCA_OPTIONS is the bare
    // tc_prio_qopt.
    try {
        send_qdisc(RTM_NEWQDISC, ifindex, TC_H_ROOT, prio_handle, "prio", [this](NetlinkMessage& req) {
            struct tc_prio_qopt qopt;
            memset(&qopt, 0, sizeof(qopt));
            qopt.bands = 2;
            for (auto& band : qopt.priomap) {
                band = 1;
            }
            qopt.priomap[6] = 0;
            qopt.priomap[7] = 0;
            add_attribute(&req.header, sizeof(req.buffer), TCA_OPTIONS, &qopt, sizeof(qopt));
        });
    } catch (std::exception& e) {
        report_error(interface_name, caller, e.what());
        return false;
    } catch (...) {
        report_error(interface_name, caller, "Unexpected exception");
        return false;
    }

    // Past the root, a failure would leave the shaped band with the kernel's default child (a
    // limit 0 pfifo on a vcan, dropping everything). Remove the root so false always means unshaped.
    auto const fail_unshaped = [&](std::string const& reason) {
        std::string outcome;
        try {
            remove_root_qdisc(ifindex);
            outcome = "; the interface is left unshaped";
        } catch (std::exception& e) {
            outcome = std::string("; removing the half installed tree failed as well: ") + e.what();
        } catch (...) {
            outcome = "; removing the half installed tree failed as well";
        }
        report_error(interface_name, caller, reason + outcome);
        return false;
    };
    try {
        // 1:1 unshaped band: pfifo with an explicit packet limit.
        send_qdisc(RTM_NEWQDISC, ifindex, band_unshaped, 0, "pfifo", [this](NetlinkMessage& req) {
            struct tc_fifo_qopt qopt;
            memset(&qopt, 0, sizeof(qopt));
            qopt.limit = unshaped_pfifo_limit;
            add_attribute(&req.header, sizeof(req.buffer), TCA_OPTIONS, &qopt, sizeof(qopt));
        });
        // 1:2 shaped band: the token bucket.
        send_tbf();
        return true;
    } catch (std::exception& e) {
        return fail_unshaped(e.what());
    } catch (...) {
        return fail_unshaped("Unexpected exception");
    }
}

bool vcan_netlink_manager::clear_transmit_rate_limit(std::string const& interface_name) {
    static constexpr char const* caller = "clearTransmitRateLimit";
    unsigned int const ifindex = ::if_nametoindex(interface_name.c_str());
    if (ifindex == 0) {
        report_error(interface_name, caller, std::string("if_nametoindex failed: ") + std::strerror(errno));
        return false;
    }
    try {
        remove_root_qdisc(ifindex);
        return true;
    } catch (std::exception& e) {
        report_error(interface_name, caller, e.what());
    } catch (...) {
        report_error(interface_name, caller, "Unexpected exception");
    }
    return false;
}

bool vcan_netlink_manager::destroy(std::string const& interface_name) {
    return send_netlink_request(
        RTM_DELLINK, 0,
        [&](struct nlmsghdr* nlh, struct ifinfomsg*, int max_buf_len) {
            add_attribute(nlh, max_buf_len, IFLA_IFNAME, interface_name.c_str(), interface_name.length() + 1);
        },
        interface_name, "destroy");
}

} // namespace everest::lib::io::netlink
