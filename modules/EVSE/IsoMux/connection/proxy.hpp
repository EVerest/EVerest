// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 chargebyte GmbH
// Copyright (C) 2022 Contributors to EVerest

#ifndef ISOMUX_PROXY_H
#define ISOMUX_PROXY_H

#include <arpa/inet.h>
#include <cstddef>
#include <netinet/in.h>
#include <unistd.h>

#include "../tools.hpp"

/*!
 * \brief connect to a local V2G server
 * \param port port to connect to
 * \param proxy_if_name network interface whose IPv6 link-local address is used
 *        as the connection target.  Pass nullptr (default) to fall back to ::1.
 * \return 0 on failure, otherwise the socket
 */
inline int proxy_connect(uint16_t port, const char* proxy_if_name = nullptr) {

    int sock_fd = -1;
    struct sockaddr_in6 server_addr {};

    /* Create socket for communication with server */
    sock_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd == -1) {
        perror("socket()");
        return -1;
    }

    server_addr.sin6_family = AF_INET6;

    if (proxy_if_name != nullptr && proxy_if_name[0] != '\0') {
        /* Connect to the link-local address of the dedicated proxy interface */
        if (get_interface_ipv6_address(proxy_if_name, ADDR6_TYPE_LINKLOCAL, &server_addr) != 0) {
            perror("proxy_connect: get_interface_ipv6_address()");
            close(sock_fd);
            return -1;
        }
    } else {
        /* Fall back to loopback */
        inet_pton(AF_INET6, "::1", &server_addr.sin6_addr);
    }

    server_addr.sin6_port = htons(port);

    /* Try to do TCP handshake with server */
    int ret = connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret == -1) {
        perror("connect()");
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}

#endif /* ISOMUX_PROXY_H */
