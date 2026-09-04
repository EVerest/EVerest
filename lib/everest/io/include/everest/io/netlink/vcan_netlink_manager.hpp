// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

extern "C" struct nlmsghdr;
extern "C" struct ifinfomsg;

namespace everest::lib::io::netlink {

/**
 * @brief vcan_netlink_manager manages setup of
 * <a href=https://docs.kernel.org/networking/can.html>virtual CAN</a>
 * devices via <a href=https://man7.org/linux/man-pages/man7/netlink.7.html>NETLINK</a>.
 * The class is designed as a singleton.
 */
class vcan_netlink_manager {
public:
    struct NetlinkMessage;

    /// Sink for diagnostics about failed netlink operations. Receives one fully formatted line
    /// without trailing newline.
    using error_handler_type = std::function<void(std::string const&)>;

    vcan_netlink_manager(vcan_netlink_manager const&) = delete;
    vcan_netlink_manager& operator=(vcan_netlink_manager const&) = delete;

    /**
     * @brief Redirect diagnostics about failed netlink operations.
     * @details Without a handler, diagnostics are written to std::cerr. Applications that own the
     * terminal (or use a logging framework) can install their own sink here. An empty handler
     * restores the std::cerr default. Exceptions thrown by the handler are swallowed and the message
     * falls back to std::cerr, since reporting happens in exception handlers and destructors.
     * This is not synchronized: install the handler during setup, before the first netlink operation.
     * @param[in] handler The sink to report to
     */
    void set_error_handler(error_handler_type handler);

    /**
     * @brief Creates a new virtual CAN interface.
     * @param interface_name The desired name for the new interface (e.g., "vcan0").
     * @return True on success, false otherwise
     */
    bool create(std::string const& interface_name);

    /**
     * @brief Brings a virtual CAN interface up (activates it).
     * @param interface_name The name of the interface.
     * @return True on success, false otherwise
     */
    bool bring_up(std::string const& interface_name);

    /**
     * @brief Brings a virtual CAN interface down (deactivates it).
     * @param interface_name The name of the interface.
     * @return True on success, false otherwise
     */
    bool bring_down(std::string const& interface_name);

    /**
     * @brief Destroys (deletes) a virtual CAN interface.
     * @param interface_name The name of the interface.
     * @return True on success, false otherwise
     */
    bool destroy(std::string const& interface_name);

    /**
     * @brief Shape what producers write into the vcan with a token bucket (tbf), so a writer feels
     *        the bus rate as on real hardware: a small SO_SNDBUF blocks at bus rate, the default
     *        buffer gets ENOBUFS when the queue is full.
     * @details Qdisc tree over rtnetlink (RTM_NEWQDISC): a 2-band prio root; skb->priority 6/7
     *          (\ref unshaped_socket_priority, the bridge's own bus-to-host writes) -> pfifo,
     *          everything else -> tbf. Idempotent; the qdiscs disappear with the interface. tbf
     *          meters skb bytes (16 per classic frame, 72 per CAN FD frame), derive \p rate_bps from
     *          the wire cost of the traffic. Without IFF_ECHO (vcan default) local readers get a
     *          clone before the qdisc, so only the writer is paced; a reader that needs bus rate
     *          paces its reads.
     * @param interface_name The interface (must exist).
     * @param rate_bps Token refill rate in bit/s of skb bytes.
     * @param burst_bytes Bucket in bytes, at least the CAN FD MTU (72); should be >= rate/HZ.
     * @param limit_bytes Queue capacity in bytes.
     * @return True on success. False otherwise (reported through the error handler); then nothing
     *         is left installed, also when an earlier call had shaped the interface.
     */
    bool set_transmit_rate_limit(std::string const& interface_name, std::uint64_t rate_bps, std::uint32_t burst_bytes,
                                 std::uint32_t limit_bytes);

    /**
     * @brief Remove the shaper of \ref set_transmit_rate_limit. Idempotent.
     * @param interface_name The interface (must exist).
     * @return True on success, false otherwise (reported through the error handler).
     */
    bool clear_transmit_rate_limit(std::string const& interface_name);

    /**
     * @brief SO_PRIORITY that bypasses the shaper of \ref set_transmit_rate_limit, see
     *        \ref can::socket_can_options::socket_priority.
     */
    static std::uint8_t unshaped_socket_priority();

    /**
     * @brief Access the single instance of this object.
     * @details The underlying object is created on the first call to this function.
     * @return Reference to the object instance
     */
    static vcan_netlink_manager& Instance();

private:
    using cb_type = std::function<void(nlmsghdr*, ifinfomsg*, int)>;

    vcan_netlink_manager();
    ~vcan_netlink_manager();

    int m_nl_socket_fd{-1};
    uint32_t s_netlink_seq_counter{1};

    /**
     * @brief Adds a Netlink attribute to the message buffer.
     * This helper handles both top-level and nested attributes by always appending
     * to the current end of the message and updating the overall message length.
     * The parent attribute's length must be updated manually after its nested attributes are added.
     * @param nlh The Netlink message header.
     * @param maxlen The maximum allowed length of the message buffer.
     * @param type The attribute type (e.g., IFLA_IFNAME, IFLA_INFO_KIND).
     * @param data Pointer to the attribute data.
     * @param len Length of the attribute data.
     * @throws std::runtime_error if attribute exceeds buffer maxlen.
     */
    void add_attribute(nlmsghdr* nlh, int maxlen, int type, const void* data, int len);

    /**
     * @brief Sends a Netlink message to the kernel.
     * @param msg The Netlink message to send.
     * @param flags Send flags (e.g., MSG_DONTWAIT).
     * @throws std::runtime_error if sendmsg fails.
     */
    void send_message(NetlinkMessage const& msg, int flags = 0);

    /**
     * @brief Receives a Netlink message response from the kernel.
     * @param msg Output parameter to store the received message.
     * @throws std::runtime_error if recvmsg fails or no valid message is received.
     */
    void receive_message(NetlinkMessage& msg);

    /**
     * @brief Sends a Netlink request and waits for an ACK from the kernel.
     * @param msg_type The type of Netlink message (e.g., RTM_NEWLINK).
     * @param flags Netlink message flags (e.g., NLM_F_CREATE | NLM_F_EXCL).
     * @param callback A lambda or function to populate the `ifinfomsg` and add `rtattr`s.
     * @throws std::runtime_error if the operation fails or ACK is not received.
     */
    void send_netlink_request_impl(int msg_type, int flags, cb_type const& callback);

    /**
     * @brief Wait for the kernel's answer to \p req: returns on ACK, throws on NLMSG_ERROR or when no
     *        matching reply arrives.
     */
    void await_ack(NetlinkMessage const& req, int msg_type);

    /**
     * @brief Send one qdisc request (RTM_NEWQDISC or RTM_DELQDISC) and wait for its ACK; throws on
     *        refusal.
     * @param kind The qdisc kind, nullptr for a delete.
     * @param fill_options Appends TCA_OPTIONS; may be empty.
     */
    void send_qdisc(int msg_type, unsigned int ifindex, std::uint32_t parent, std::uint32_t handle, char const* kind,
                    std::function<void(NetlinkMessage&)> const& fill_options);

    /**
     * @brief Delete the root qdisc; ENOENT (kernel default queue) counts as done. Throws otherwise.
     */
    void remove_root_qdisc(unsigned int ifindex);

    /**
     * @brief Wraps exception handling for \p send_netlink_request_impl
     * @param msg_type The type of Netlink message (e.g., RTM_NEWLINK).
     * @param flags Netlink message flags (e.g., NLM_F_CREATE | NLM_F_EXCL).
     * @param callback A lambda or function to populate the `ifinfomsg` and add `rtattr`s.
     * @param interface_name For error reporting.
     * @param[in] caller The name of the caller
     * @return True on success, false otherwise
     */
    bool send_netlink_request(int msg_type, int flags, cb_type const& callback, std::string const& interface_name,
                              std::string const& caller);

    /**
     * @brief Report a failed netlink operation to the error handler, or to std::cerr if none is set.
     * @param interface_name The interface the operation was about.
     * @param[in] caller The name of the caller
     * @param[in] reason Description of the failure
     */
    void report_error(std::string const& interface_name, std::string const& caller, std::string const& reason);

    error_handler_type m_error_handler{nullptr};
};

} // namespace everest::lib::io::netlink
