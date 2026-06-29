#include <everest/io/socket/socket.hpp>
#include <everest/slac/slac.hpp>
#include <everest/slac/slac_socket.hpp>

#include <cerrno>
#include <cstring>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

namespace {
using namespace everest::lib::io;

std::string build_errno_string(std::string const& msg, int error_code) {
    std::stringstream str;
    str << msg;
    if (error_code != 0) {
        str << ": " << strerror(error_code) << " (" << error_code << ")";
    }
    return str.str();
}

event::unique_fd open_raw_socket(std::string const& if_name) {
    const auto protocol = htons(everest::lib::slac::defs::ETH_P_HOMEPLUG_GREENPHY);
    errno = 0;
    int const if_index = if_nametoindex(if_name.c_str());
    if (if_index == 0) {
        auto const error_code = errno == 0 ? ENODEV : errno;
        throw std::system_error(error_code, std::generic_category(), "SLAC PLC interface '" + if_name + "' not found");
    }

    auto const socket_fd = ::socket(AF_PACKET, SOCK_RAW, protocol);
    if (socket_fd == -1) {
        auto const error_code = errno;
        if (error_code == EPERM || error_code == EACCES) {
            throw std::system_error(error_code, std::generic_category(),
                                    "Could not open raw socket on interface '" + if_name +
                                        "': requires CAP_NET_RAW or root privileges to access raw sockets");
        }
        throw std::system_error(error_code, std::generic_category(),
                                "Could not open raw socket on interface '" + if_name + "'");
    }

    struct sockaddr_ll sll;
    std::memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_index;
    sll.sll_protocol = protocol;
    sll.sll_halen = ETH_ALEN;

    if (::bind(socket_fd, (struct sockaddr*)&sll, sizeof(sll)) == -1) {
        auto const error_code = errno;
        auto msg = "Could not bind raw socket to interface '" + if_name + "'";
        if (error_code == EPERM || error_code == EACCES) {
            msg = "Could not bind raw socket to interface '" + if_name + "': requires CAP_NET_RAW or root privileges";
        }
        close(socket_fd);
        throw std::system_error(error_code, std::generic_category(), msg);
    }

    return event::unique_fd(socket_fd);
}

void set_error_state(int& error_code, std::string& error_message, int new_error_code,
                     std::string const& new_error_message) {
    error_code = new_error_code;
    error_message = new_error_message;
}

} // namespace

namespace everest::lib::slac {

bool slac_socket::open(std::string const& if_name) {
    set_error_state(m_error_code, m_error_message, 0, {});
    try {
        auto socket = open_raw_socket(if_name);
        socket::set_non_blocking(socket);
        auto result = socket::get_pending_error(socket);
        if (result != 0) {
            set_error_state(m_error_code, m_error_message, result, build_errno_string("Socket pending error", result));
            return false;
        }
        m_fd = std::move(socket);
        auto result_code = socket::get_pending_error(m_fd);
        m_mac = socket::get_mac_address(m_fd, if_name);
        if (result_code != 0) {
            set_error_state(m_error_code, m_error_message, result_code,
                            build_errno_string("Could not read raw socket pending error", result_code));
            return false;
        }
        return true;
    } catch (std::system_error const& e) {
        set_error_state(m_error_code, m_error_message, e.code().value(), e.what());
    } catch (std::runtime_error const& e) {
        auto const error_code = errno == 0 ? EIO : errno;
        set_error_state(m_error_code, m_error_message, error_code, e.what());
    } catch (...) {
        set_error_state(m_error_code, m_error_message, EIO, "Failed to open SLAC socket");
    }
    m_fd.close();
    return false;
}

void slac_socket::close() {
    set_error_state(m_error_code, m_error_message, 0, {});
    m_fd.close();
}

bool slac_socket::tx(PayloadT const& payload) {
    if (not is_open()) {
        return false;
    }
    auto const frame_size = payload.frame_size();
    if (not payload.is_valid() || frame_size > ETH_FRAME_LEN || frame_size > sizeof(*payload.get_raw_message_ptr())) {
        return false;
    }

    auto const status = ::send(m_fd, payload.get_raw_message_ptr(), frame_size, 0);
    if (status < 0) {
        auto const error_code = errno;
        if (error_code != EAGAIN && error_code != EWOULDBLOCK) {
            set_error_state(m_error_code, m_error_message, error_code,
                            build_errno_string("Failed to send raw socket payload", error_code));
        }
        return false;
    }
    if (static_cast<std::size_t>(status) != frame_size) {
        auto error_code = EIO;
        set_error_state(m_error_code, m_error_message, error_code,
                        "Failed to send complete Homeplug payload: sent " + std::to_string(status) + " of " +
                            std::to_string(frame_size) + " bytes");
        return false;
    }
    set_error_state(m_error_code, m_error_message, 0, {});
    return true;
}

bool slac_socket::rx(PayloadT& buffer) {
    if (not is_open()) {
        return false;
    }
    auto status = ::recv(m_fd, buffer.get_raw_message_ptr(), ETH_FRAME_LEN, 0);
    if (status <= 0) { // -1 is an error, 0 is a connection closed by the peer
        if (status == -1) {
            auto const error_code = errno;
            if (error_code != EAGAIN && error_code != EWOULDBLOCK) {
                set_error_state(m_error_code, m_error_message, error_code,
                                build_errno_string("Failed to receive raw socket payload", error_code));
            }
            return false;
        }
        set_error_state(m_error_code, m_error_message, ENOTCONN, "SLAC raw socket closed by peer");
        return false;
    }
    set_error_state(m_error_code, m_error_message, 0, {});
    buffer.mark_received_length(static_cast<std::size_t>(status));
    return true;
}

int slac_socket::get_fd() const {
    return m_fd;
}

int slac_socket::get_error() const {
    auto const fd_error = is_open() ? socket::get_pending_error(m_fd) : m_error_code;
    if (fd_error != 0) {
        return fd_error;
    }
    return m_error_code;
}

std::string slac_socket::get_error_message() const {
    if (auto const code = get_error()) {
        if (!m_error_message.empty()) {
            return m_error_message;
        }
        return build_errno_string("SLAC socket error", code);
    }
    return {};
}

bool slac_socket::is_open() const {
    return m_fd.is_fd();
}

slac_socket::MacAddress slac_socket::get_mac_address() const {
    return m_mac;
}
} // namespace everest::lib::slac
