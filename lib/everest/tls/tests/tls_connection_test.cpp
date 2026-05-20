// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <arpa/inet.h>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/x509.h>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <thread>

using namespace std::chrono_literals;

namespace {
using result_t = tls::Connection::result_t;
using tls::status_request::ClientStatusRequestV2;

constexpr auto server_root_CN = "00000000";
constexpr auto alt_server_root_CN = "11111111";
constexpr auto WAIT_FOR_SERVER_START_TIMEOUT = 50ms;

void do_poll(std::array<pollfd, 2>& fds, int server_soc, int client_soc) {
    const std::int16_t events = POLLOUT | POLLIN;
    fds[0].fd = server_soc;
    fds[0].events = events;
    fds[0].revents = 0;
    fds[1].fd = client_soc;
    fds[1].events = events;
    fds[1].revents = 0;
    auto poll_res = poll(fds.data(), fds.size(), -1);
    ASSERT_NE(poll_res, -1);
}

tls::Server::OptionalConfig ssl_init() {
    std::cout << "ssl_init" << std::endl;
    auto server_config = std::make_unique<tls::Server::config_t>();
    server_config->cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    server_config->ciphersuites = "";
    auto& ref = server_config->chains.emplace_back();
    ref.certificate_chain_file = "server_chain.pem";
    ref.private_key_file = "server_priv.pem";
    ref.trust_anchor_file = "server_root_cert.pem";
    ref.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    server_config->host = "127.0.0.1";
    server_config->service = "8444";
    server_config->ipv6_only = false;
    server_config->verify_client = false;
    server_config->io_timeout_ms = 500;
    return {{std::move(server_config)}};
}

// ----------------------------------------------------------------------------
// The tests

TEST_F(TlsTest, StartStop) {
    // test shouldn't hang
    start();

    // check TearDown on stopped server is okay
    server.stop();
    server.wait_stopped();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_F(TlsTest, StartConnectDisconnect) {
    // test shouldn't hang
    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, NonBlocking) {
    client_config.io_timeout_ms = 0;
    server_config.io_timeout_ms = 0;
    std::timed_mutex mux;
    mux.lock();

    tls::Server::ConnectionPtr server_connection;
    tls::Client::ConnectionPtr client_connection;

    auto server_handler_fn = [&server_connection, &mux](tls::Server::ConnectionPtr&& connection) {
        server_connection = std::move(connection);
        mux.unlock();
    };
    start(server_handler_fn);

    auto client_handler_fn = [&client_connection](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            client_connection = std::move(connection);
        }
    };
    connect(client_handler_fn);

    // FIXME (aw): this is not a proper solution.  It would be necessary
    // to get an exception or result on whether `start()` function has
    // been successful
    if (not mux.try_lock_for(WAIT_FOR_SERVER_START_TIMEOUT)) {
        GTEST_SKIP();
    }

    // check there is a TCP connection
    ASSERT_TRUE(server_connection);
    ASSERT_TRUE(client_connection);

    int server_soc = server_connection->socket();
    int client_soc = client_connection->socket();
    std::array<pollfd, 2> fds;

    EXPECT_EQ(server_connection->accept(0), result_t::want_read);
    EXPECT_EQ(client_connection->connect(0), result_t::want_read);

    bool s_complete{false};
    bool c_complete{false};
    std::uint32_t s_count{0};
    std::uint32_t c_count{0};

    while (!s_complete && !c_complete) {
        do_poll(fds, server_soc, client_soc);
        if (((fds[0].revents & POLLIN) != 0) || ((fds[0].revents & POLLOUT) != 0)) {
            s_complete = server_connection->accept(0) == result_t::success;
            s_count++;
        }
        if (((fds[1].revents & POLLIN) != 0) || ((fds[1].revents & POLLOUT) != 0)) {
            c_complete = client_connection->connect(0) == result_t::success;
            c_count++;
        }

        ASSERT_EQ(fds[0].revents & POLLHUP, 0);
        ASSERT_EQ(fds[1].revents & POLLHUP, 0);
        ASSERT_EQ(fds[0].revents & POLLERR, 0);
        ASSERT_EQ(fds[1].revents & POLLERR, 0);
    }

    // std::cout << "counts: " << s_count << " " << c_count << std::endl;
    EXPECT_GT(s_count, 0);
    EXPECT_GT(c_count, 0);

    const std::byte data{0xf3};

    std::byte s_buf{0};
    std::size_t s_readbytes{0};
    std::size_t s_writebytes{0};
    std::byte c_buf{0};
    std::size_t c_readbytes{0};
    std::size_t c_writebytes{0};

    EXPECT_EQ(server_connection->read(&s_buf, sizeof(s_buf), s_readbytes, 0), result_t::want_read);
    EXPECT_EQ(client_connection->read(&c_buf, sizeof(c_buf), c_readbytes, 0), result_t::want_read);

    EXPECT_EQ(server_connection->write(&data, sizeof(data), s_writebytes, 0), result_t::success);
    EXPECT_EQ(client_connection->write(&data, sizeof(data), c_writebytes, 0), result_t::success);

    s_complete = false;
    c_complete = false;
    s_count = 0;
    c_count = 0;

    while (!s_complete && !c_complete) {
        do_poll(fds, server_soc, client_soc);
        if ((fds[0].revents & POLLIN) != 0) {
            s_complete = server_connection->read(&s_buf, sizeof(s_buf), s_readbytes, 0) == result_t::success;
            s_count++;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            c_complete = client_connection->read(&c_buf, sizeof(c_buf), c_readbytes, 0) == result_t::success;
            c_count++;
        }

        ASSERT_EQ(fds[0].revents & POLLHUP, 0);
        ASSERT_EQ(fds[1].revents & POLLHUP, 0);
        ASSERT_EQ(fds[0].revents & POLLERR, 0);
        ASSERT_EQ(fds[1].revents & POLLERR, 0);
    }

    EXPECT_EQ(s_readbytes, 1);
    EXPECT_EQ(s_buf, data);
    EXPECT_EQ(c_readbytes, 1);
    EXPECT_EQ(c_buf, data);

    // std::cout << "counts: " << s_count << " " << c_count << std::endl;
    EXPECT_GT(s_count, 0);
    EXPECT_GT(c_count, 0);

    s_complete = false;
    c_complete = false;
    s_count = 0;
    c_count = 0;

    EXPECT_EQ(server_connection->read(&s_buf, sizeof(s_buf), s_readbytes, 0), result_t::want_read);
    EXPECT_EQ(client_connection->shutdown(0), result_t::closed); // closed
    while (!s_complete && !c_complete) {
        do_poll(fds, server_soc, client_soc);
        if (((fds[0].revents & POLLIN) != 0) || ((fds[0].revents & POLLOUT) != 0)) {
            s_complete = server_connection->read(&s_buf, sizeof(s_buf), s_readbytes, 0) == result_t::closed;
            s_count++;
        }
        if (((fds[1].revents & POLLIN) != 0) || ((fds[1].revents & POLLOUT) != 0)) {
            c_complete = client_connection->shutdown(0) == result_t::success;
            c_count++;
        }

        ASSERT_EQ(fds[0].revents & POLLERR, 0);
        ASSERT_EQ(fds[1].revents & POLLERR, 0);
    }

    // std::cout << "counts: " << s_count << " " << c_count << std::endl;
    EXPECT_GT(s_count, 0);
    EXPECT_GT(c_count, 0);
}

TEST_F(TlsTest, NonBlockingClientClose) {
    std::timed_mutex mux;
    mux.lock();

    tls::Server::ConnectionPtr server_connection;
    tls::Client::ConnectionPtr client_connection;

    auto server_handler_fn = [&server_connection, &mux](tls::Server::ConnectionPtr&& connection) {
        if (connection->accept() == result_t::success) {
            server_connection = std::move(connection);
            mux.unlock();
        }
    };
    start(server_handler_fn);

    auto client_handler_fn = [&client_connection](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                client_connection = std::move(connection);
            }
        }
    };
    connect(client_handler_fn);

    if (not mux.try_lock_for(WAIT_FOR_SERVER_START_TIMEOUT)) {
        GTEST_SKIP();
    }
    // check there is a TCP connection
    ASSERT_TRUE(server_connection);
    ASSERT_TRUE(client_connection);

    int server_soc = server_connection->socket();
    int client_soc = client_connection->socket();
    std::array<pollfd, 2> fds;

    bool s_complete{false};
    bool c_complete{false};
    std::uint32_t s_count{0};
    std::uint32_t c_count{0};

    std::byte buf{0};
    std::size_t readbytes{0};

    EXPECT_EQ(server_connection->read(&buf, sizeof(buf), readbytes, 0), result_t::want_read);
    EXPECT_EQ(client_connection->shutdown(0), result_t::closed); // closed
    while (!s_complete && !c_complete) {
        do_poll(fds, server_soc, client_soc);
        if (((fds[0].revents & POLLIN) != 0) || ((fds[0].revents & POLLOUT) != 0)) {
            s_complete = server_connection->read(&buf, sizeof(buf), readbytes, 0) == result_t::closed;
            s_count++;
        }
        if (((fds[1].revents & POLLIN) != 0) || ((fds[1].revents & POLLOUT) != 0)) {
            c_complete = client_connection->shutdown(0) == result_t::closed;
            c_count++;
        }

        ASSERT_EQ(fds[0].revents & POLLERR, 0);
        ASSERT_EQ(fds[1].revents & POLLERR, 0);
    }

    // std::cout << "counts: " << s_count << " " << c_count << std::endl;
    EXPECT_GT(s_count, 0);
    EXPECT_GT(c_count, 0);
}

TEST_F(TlsTest, NonBlockingServerClose) {
    std::timed_mutex mux;
    mux.lock();

    tls::Server::ConnectionPtr server_connection;
    tls::Client::ConnectionPtr client_connection;

    auto server_handler_fn = [&server_connection, &mux](tls::Server::ConnectionPtr&& connection) {
        if (connection->accept() == result_t::success) {
            server_connection = std::move(connection);
            mux.unlock();
        }
    };
    start(server_handler_fn);

    auto client_handler_fn = [&client_connection](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                client_connection = std::move(connection);
            }
        }
    };
    connect(client_handler_fn);

    if (not mux.try_lock_for(WAIT_FOR_SERVER_START_TIMEOUT)) {
        GTEST_SKIP();
    }
    // check there is a TCP connection
    ASSERT_TRUE(server_connection);
    ASSERT_TRUE(client_connection);

    int server_soc = server_connection->socket();
    int client_soc = client_connection->socket();
    std::array<pollfd, 2> fds;

    bool s_complete{false};
    bool c_complete{false};
    std::uint32_t s_count{0};
    std::uint32_t c_count{0};

    std::byte buf{0};
    std::size_t readbytes{0};

    EXPECT_EQ(server_connection->shutdown(0), result_t::closed); // closed
    EXPECT_EQ(client_connection->read(&buf, sizeof(buf), readbytes, 0), result_t::want_read);
    while (!s_complete && !c_complete) {
        do_poll(fds, server_soc, client_soc);
        if (((fds[0].revents & POLLIN) != 0) || ((fds[0].revents & POLLOUT) != 0)) {
            s_complete = server_connection->shutdown(0) == result_t::closed;
            s_count++;
        }
        if (((fds[1].revents & POLLIN) != 0) || ((fds[1].revents & POLLOUT) != 0)) {
            c_complete = client_connection->read(&buf, sizeof(buf), readbytes, 0) == result_t::closed;
            c_count++;
        }

        ASSERT_EQ(fds[0].revents & POLLERR, 0);
        ASSERT_EQ(fds[1].revents & POLLERR, 0);
    }

    // std::cout << "counts: " << s_count << " " << c_count << std::endl;
    EXPECT_GT(s_count, 0);
    EXPECT_GT(c_count, 0);
}

TEST_F(TlsTest, ClientReadTimeout) {
    // test shouldn't hang
    client_config.io_timeout_ms = 50;

    auto client_handler_fn = [this](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                std::byte buffer{};
                std::size_t readbytes{0};
                auto res = connection->read(&buffer, sizeof(buffer), readbytes);
                EXPECT_EQ(readbytes, 0);
                EXPECT_EQ(res, result_t::timeout);
                if (res != result_t::closed) {
                    connection->shutdown();
                }
                connection->shutdown();
            }
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, ClientWriteTimeout) {
    // test shouldn't hang
    client_config.io_timeout_ms = 50;

    bool did_timeout{false};
    std::size_t count{0};
    std::mutex mux;
    mux.lock();

    constexpr std::size_t max_bytes = 1024 * 1024 * 1024;

    auto server_handler_fn = [&mux](tls::Server::ConnectionPtr&& con) {
        if (con->accept() == result_t::success) {
            mux.lock();
            con->shutdown();
        }
    };

    auto client_handler_fn = [this, &mux, &did_timeout, &count](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                std::array<std::byte, 1024> buffer{};
                std::size_t writebytes{0};

                bool exit{false};

                while (!exit) {
                    switch (connection->write(buffer.data(), buffer.size(), writebytes)) {
                    case result_t::success:
                        count += writebytes;
                        exit = count > max_bytes;
                        break;
                    case result_t::timeout:
                        // std::cout << "timeout: " << count << " bytes" << std::endl;
                        did_timeout = true;
                        exit = true;
                        break;
                    case result_t::closed:
                    default:
                        exit = true;
                        break;
                    }
                }
                mux.unlock();
                std::size_t readbytes = 0;
                auto res = connection->read(buffer.data(), buffer.size(), readbytes);
                if (res != result_t::closed) {
                    connection->shutdown();
                }
            }
        }
    };

    start(server_handler_fn);
    connect(client_handler_fn);
    EXPECT_TRUE(did_timeout);
    EXPECT_LE(count, max_bytes);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, ServerReadTimeout) {
    // test shouldn't hang
    bool did_timeout{false};
    std::mutex mux;
    mux.lock();

    auto server_handler_fn = [&mux, &did_timeout](tls::Server::ConnectionPtr&& con) {
        if (con->accept() == result_t::success) {
            std::array<std::byte, 1024> buffer{};
            std::size_t readbytes = 0;
            auto res = con->read(buffer.data(), buffer.size(), readbytes);
            did_timeout = res == result_t::timeout;
            mux.unlock();
            con->shutdown();
        }
    };

    auto client_handler_fn = [this, &mux](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                mux.lock();
                connection->shutdown();
            }
        }
    };

    start(server_handler_fn);
    connect(client_handler_fn);
    EXPECT_TRUE(did_timeout);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, ServerWriteTimeout) {
    // test shouldn't hang
    bool did_timeout{false};
    std::size_t count{0};
    std::mutex mux;
    mux.lock();

    constexpr std::size_t max_bytes = 1024 * 1024 * 1024;

    auto server_handler_fn = [&mux, &did_timeout, &count](tls::Server::ConnectionPtr&& con) {
        if (con->accept() == result_t::success) {
            std::array<std::byte, 1024> buffer{};
            std::size_t writebytes{0};

            bool exit{false};

            while (!exit) {
                switch (con->write(buffer.data(), buffer.size(), writebytes)) {
                case result_t::success:
                    count += writebytes;
                    exit = count > max_bytes;
                    break;
                case result_t::timeout:
                    // std::cout << "timeout: " << count << " bytes" << std::endl;
                    did_timeout = true;
                    exit = true;
                    break;
                case result_t::closed:
                default:
                    exit = true;
                    break;
                }
            }

            mux.unlock();
            std::size_t readbytes = 0;
            auto res = con->read(buffer.data(), buffer.size(), readbytes);
            if (res != result_t::closed) {
                con->shutdown();
            }
        }
    };

    auto client_handler_fn = [this, &mux](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
            }
            mux.lock();
            connection->shutdown();
        }
    };

    start(server_handler_fn);
    connect(client_handler_fn);
    EXPECT_TRUE(did_timeout);
    EXPECT_LE(count, max_bytes);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, delayedConfig) {
    // partial config
    server_config.chains.clear();

    start(ssl_init);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, partialConfig) {
    // partial config - no support for trusted_ca_keys
    for (auto& i : server_config.chains) {
        i.trust_anchor_file = nullptr;
    }

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, TLS13) {
    // test using TLS 1.3
    // there shouldn't be status_request_v2 responses
    // TLS 1.3 still supports status_request however it is handled differently
    // (which is handled within the OpenSSL API)
    server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
    start();
    connect();
    // no status requested
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));

    client_config.status_request = true;
    connect();
    // status_request only
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_set(flags_t::status_request_cb));
    EXPECT_TRUE(is_set(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));

    client_config.status_request = false;
    client_config.status_request_v2 = true;
    connect();
    // status_request_v2 only - ignored by server
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_set(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));

    client_config.status_request = true;
    connect();
    // status_request and status_request_v2
    // status_request_v2 is ignored by server and status_request used
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_set(flags_t::status_request_cb));
    EXPECT_TRUE(is_set(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, NoOcspFiles) {
    // test using TLS 1.2
    for (auto& chain : server_config.chains) {
        chain.ocsp_response_files.clear();
    }

    start();
    connect();
    // no status requested
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_reset(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));

    client_config.status_request = true;
    connect();
    // status_request only
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_set(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));

    client_config.status_request = false;
    client_config.status_request_v2 = true;
    connect();
    // status_request_v2 only
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_set(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));

    client_config.status_request = true;
    connect();
    // status_request and status_request_v2
    // status_request_v2 is preferred over status_request
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(is_set(flags_t::status_request_cb));
    EXPECT_TRUE(is_reset(flags_t::status_request));
    EXPECT_TRUE(is_reset(flags_t::status_request_v2));
}

TEST_F(TlsTest, CertVerify) {
    client_config.verify_locations_file = "alt_server_root_cert.pem";
    start();
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
}

TEST_F(TlsTest, TCKeysNone) {
    // trusted_ca_keys - none match - default certificate should be used
    std::map<std::string, std::string> subject;

    client_config.trusted_ca_keys = true;
    client_config.trusted_ca_keys_data.pre_agreed = true;
    add_ta_cert_hash("client_root_cert.pem");
    add_ta_key_hash("client_root_cert.pem");
    add_ta_name("client_root_cert.pem");

    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                subject = openssl::certificate_subject(connection->peer_certificate());
                connection->shutdown();
            }
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], server_root_CN);
}

TEST_F(TlsTest, TCKeysCert) {
    // trusted_ca_keys - cert hash matches
    std::map<std::string, std::string> subject;

    client_config.trusted_ca_keys = true;
    client_config.verify_locations_file = "alt_server_root_cert.pem";
    add_ta_cert_hash("alt_server_root_cert.pem");

    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                subject = openssl::certificate_subject(connection->peer_certificate());
                connection->shutdown();
            }
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);

    client_config.trusted_ca_keys_data.x509_name.clear();
    add_ta_cert_hash("client_root_cert.pem");
    add_ta_cert_hash("alt_server_root_cert.pem");

    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

TEST_F(TlsTest, TCKeysKey) {
    // trusted_ca_keys - key hash matches
    std::map<std::string, std::string> subject;

    client_config.trusted_ca_keys = true;
    client_config.verify_locations_file = "alt_server_root_cert.pem";
    add_ta_key_hash("alt_server_root_cert.pem");

    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                subject = openssl::certificate_subject(connection->peer_certificate());
                connection->shutdown();
            }
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);

    client_config.trusted_ca_keys_data.x509_name.clear();
    add_ta_key_hash("client_root_cert.pem");
    add_ta_key_hash("alt_server_root_cert.pem");

    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

TEST_F(TlsTest, TCKeysKeyPem) {
    // same as TCKeysKey but using a PEM string trust anchor rather than file
    std::map<std::string, std::string> subject;

    client_config.trusted_ca_keys = true;
    client_config.verify_locations_file = "alt_server_root_cert.pem";
    add_ta_key_hash("alt_server_root_cert.pem");

    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                subject = openssl::certificate_subject(connection->peer_certificate());
                connection->shutdown();
            }
        }
    };

    // convert file to PEM in config
    for (auto& cfg : server_config.chains) {
        const auto certs = ::openssl::load_certificates(cfg.trust_anchor_file);
        std::string pem;
        for (const auto& cert : certs) {
            pem += ::openssl::certificate_to_pem(cert.get());
        }
        // std::cout << cfg.trust_anchor_file << ": " << certs.size() << std::endl;
        ASSERT_FALSE(pem.empty());
        cfg.trust_anchor_file = nullptr;
        cfg.trust_anchor_pem = pem.c_str();
    }

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);

    client_config.trusted_ca_keys_data.x509_name.clear();
    add_ta_key_hash("client_root_cert.pem");
    add_ta_key_hash("alt_server_root_cert.pem");

    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

TEST_F(TlsTest, TCKeysName) {
    // trusted_ca_keys - subject name matches
    std::map<std::string, std::string> subject;

    client_config.trusted_ca_keys = true;
    client_config.verify_locations_file = "alt_server_root_cert.pem";
    add_ta_name("alt_server_root_cert.pem");

    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (connection) {
            if (connection->connect() == result_t::success) {
                this->set(ClientTest::flags_t::connected);
                subject = openssl::certificate_subject(connection->peer_certificate());
                connection->shutdown();
            }
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);

    client_config.trusted_ca_keys_data.x509_name.clear();
    add_ta_name("client_root_cert.pem");
    add_ta_name("alt_server_root_cert.pem");

    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

// based on an example seen in a WireShark log
// (invalid missing the size of trusted_authorities_list)
// 01 identifier_type key_sha1_hash 4cd7290bf592d2c1ba90f56e08946d4c8e99dc38 SHA1Hash
// 01 identifier_type key_sha1_hash 00fae3900795c888a4d4d7bd9fdffa60418ac19f SHA1Hash
int trusted_ca_keys_add_bad(SSL* ctx, unsigned int ext_type, unsigned int context, const unsigned char** out,
                            std::size_t* outlen, X509* cert, std::size_t chainidx, int* alert, void* object) {
    // std::cout << "trusted_ca_keys_add_bad" << std::endl;
    int result{0};
    if ((context == SSL_EXT_CLIENT_HELLO) && (object != nullptr)) {
        constexpr std::uint8_t value[] = {
            0x01, 0x4c, 0xd7, 0x29, 0x0b, 0xf5, 0x92, 0xd2, 0xc1, 0xba, 0x90, 0xf5, 0x6e, 0x08,
            0x94, 0x6d, 0x4c, 0x8e, 0x99, 0xdc, 0x38, 0x01, 0x00, 0xfa, 0xe3, 0x90, 0x07, 0x95,
            0xc8, 0x88, 0xa4, 0xd4, 0xd7, 0xbd, 0x9f, 0xdf, 0xfa, 0x60, 0x41, 0x8a, 0xc1, 0x9f,
        };
        openssl::DER der(&value[0], sizeof(value));
        const auto len = der.size();
        auto* ptr = openssl::DER::dup(der);
        if (ptr != nullptr) {
            *out = ptr;
            *outlen = len;
            result = 1;
        }
    }
    return result;
}

TEST_F(TlsTest, TCKeysInvalid) {
    // trusted_ca_keys - incorrectly formatted extension, connect using defaults
    std::map<std::string, std::string> subject;

    client_config.trusted_ca_keys = true;
    client_config.verify_locations_file = "server_root_cert.pem";

    auto override = tls::Client::default_overrides();
    override.trusted_ca_keys_add = &trusted_ca_keys_add_bad;

    start();
    client.init(client_config, override);
    client.reset();
    // localhost works in some cases but not in the CI pipeline for IPv6
    // use ip6-localhost
    auto connection = client.connect("127.0.0.1", "8444", false, 1000);
    if (connection) {
        if (connection->connect() == result_t::success) {
            set(ClientTest::flags_t::connected);
            subject = openssl::certificate_subject(connection->peer_certificate());
            connection->shutdown();
        }
    }
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], server_root_CN);
}

TEST_F(TlsTest, Suspend) {
    using state_t = tls::Server::state_t;
    start();
    EXPECT_EQ(server.state(), state_t::running);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(server.suspend());
    EXPECT_EQ(server.state(), state_t::init_socket);
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
    EXPECT_EQ(server.state(), state_t::init_socket);
    EXPECT_TRUE(server.update(server_config));
    EXPECT_EQ(server.state(), state_t::init_complete);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(server.state(), state_t::running);
}

TEST_F(TlsTest, SuspendToRunning) {
    using state_t = tls::Server::state_t;
    start();
    EXPECT_EQ(server.state(), state_t::running);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_TRUE(server.suspend());
    EXPECT_EQ(server.state(), state_t::init_socket);
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
    EXPECT_EQ(server.state(), state_t::init_socket);
    EXPECT_TRUE(server.update(server_config));
    EXPECT_EQ(server.state(), state_t::init_complete);
    // should switch to running after a timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(tls::c_serve_timeout_ms + 100));
    EXPECT_EQ(server.state(), state_t::running);
}

TEST_F(TlsTest, EnforceTls13RejectsTls12Client) {
    // Server enforces TLS 1.3; a TLS-1.2-only client must be rejected.
    server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;

    // Force the client to negotiate TLS 1.2 only by clearing TLS 1.3 ciphersuites.
    client_config.ciphersuites = "";

    start();
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
}

TEST_F(TlsTest, EnforceTls13AcceptsTls13ClientWithAes256) {
    // Server enforces TLS 1.3; client offers TLS_AES_256_GCM_SHA384 and connects.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;

    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, NoEnforceTls13EmptyCiphersuitesRejectsTls13Client) {
    // Existing behavior: with enforce_tls_1_3 = false and empty ciphersuites,
    // the server caps at TLS 1.2 and a TLS-1.3-only client cannot connect.
    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = false;

    // Pin the client to TLS 1.3 to force a 1.3-only ClientHello so the
    // 1.2-cap on the server is the reason the handshake fails.
    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;

    start();
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
}

TEST_F(TlsTest, NoEnforceTls13NonEmptyCiphersuitesAcceptsBothVersions) {
    // With enforce_tls_1_3 = false and non-empty ciphersuites the server should
    // accept both TLS 1.2 and TLS 1.3 negotiations.
    server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = false;

    // TLS 1.2 client.
    client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    client_config.ciphersuites = "";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));

    // TLS 1.3 client against the same running server.
    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";

    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, Tls13ClientWithoutCertHandshakeFails) {
    // A TLS 1.3 client that presents no client certificate must be rejected
    // because the dispatcher upgrades verify mode to require a peer cert. In
    // TLS 1.3 the server's fatal alert (certificate_required) arrives after
    // the client's SSL_connect has returned, so we probe the connection with a
    // read to force the alert to surface and confirm the handshake failed.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    // No client certificate: certificate_chain_file / private_key_file remain null.

    start();
    bool handshake_ok{false};
    connect([&handshake_ok](tls::Client::ConnectionPtr& con) {
        if (!con) {
            return;
        }
        if (con->connect() != tls::Connection::result_t::success) {
            return;
        }
        // Surface any pending fatal alert from the server.
        std::byte buf[1]{};
        std::size_t got{0};
        const auto rc = con->read(buf, sizeof(buf), got, 200);
        handshake_ok =
            (rc != tls::Connection::result_t::closed) && (con->state() == tls::Connection::state_t::connected);
    });
    EXPECT_FALSE(handshake_ok);
}

TEST_F(TlsTest, Tls13ClientWithCertHandshakeSucceeds) {
    // A TLS 1.3 client offering a certificate chained to a trust anchor the
    // server has configured must complete the handshake. Setting
    // verify_client = true loads verify_locations_file up-front so the
    // dispatcher's TLS 1.3 verify upgrade is idempotent on this path.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, Tls12ClientWithoutCertHandshakeSucceeds) {
    // Pre-existing TLS 1.2 behavior must be preserved: a TLS 1.2 client with
    // no peer certificate connects when the server does not require one.
    // enforce_tls_1_3 is left at its default (false) and verify_client is
    // false, matching the legacy server configuration.
    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = false;

    client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    client_config.ciphersuites = "";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, PeerCertificateSha512Tls13WithCert) {
    // After a TLS 1.3 handshake with a client cert the server-side accessor
    // must return the SHA-512 digest of the peer's leaf certificate DER.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";

    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool handler_done{false};
    bool accept_ok{false};
    std::optional<std::array<std::uint8_t, 64>> server_digest;

    auto server_handler = [&](tls::Server::ConnectionPtr&& con) {
        if (con && con->accept() == tls::Connection::result_t::success) {
            accept_ok = true;
            server_digest = con->peer_certificate_sha512();
            (void)con->shutdown();
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            handler_done = true;
        }
        result_cv.notify_all();
    };

    start(server_handler);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));

    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait_for(lock, 2s, [&] { return handler_done; });
    }
    ASSERT_TRUE(accept_ok);
    ASSERT_TRUE(server_digest.has_value());

    // Independently compute SHA-512 over the leaf certificate's DER encoding.
    auto leaf_certs = openssl::load_certificates("client_cert.pem");
    ASSERT_FALSE(leaf_certs.empty());
    auto der = openssl::certificate_to_der(leaf_certs.front().get());
    ASSERT_TRUE(static_cast<bool>(der));
    openssl::sha_512_digest_t expected{};
    ASSERT_TRUE(openssl::sha_512(der.get(), der.size(), expected));

    EXPECT_EQ(*server_digest, expected);
}

TEST_F(TlsTest, PeerCertificateSha512Tls12WithoutCert) {
    // TLS 1.2 path with no peer certificate: accessor must return nullopt.
    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = false;
    server_config.verify_client = false;

    client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    client_config.ciphersuites = "";

    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool handler_done{false};
    bool accept_ok{false};
    std::optional<std::array<std::uint8_t, 64>> server_digest;
    bool digest_set{false};

    auto server_handler = [&](tls::Server::ConnectionPtr&& con) {
        if (con && con->accept() == tls::Connection::result_t::success) {
            accept_ok = true;
            server_digest = con->peer_certificate_sha512();
            digest_set = true;
            (void)con->shutdown();
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            handler_done = true;
        }
        result_cv.notify_all();
    };

    start(server_handler);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));

    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait_for(lock, 2s, [&] { return handler_done; });
    }
    ASSERT_TRUE(accept_ok);
    ASSERT_TRUE(digest_set);
    EXPECT_FALSE(server_digest.has_value());
}

TEST_F(TlsTest, WrapAcceptedFdHandshake) {
    // Drives the wrap_accepted_fd factory: the test owns the listen socket and
    // the accept(2) call, hands the resulting fd to the Server, and checks the
    // returned ConnectionPtr completes a TLS handshake against a client thread.
    using state_t = tls::Server::state_t;

    // Local listen socket on an ephemeral port. Created before init() so it
    // can be passed via server_config.socket; otherwise init_socket() would
    // bind the fixture's fixed port and leak that listener until process exit.
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    int reuse = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    // Bypass init_socket() by handing our test-owned listen fd to the Server.
    // This test never calls serve(); we only need init() to set up the SSL_CTX.
    server_config.socket = listen_fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len), 0);
    const std::uint16_t bound_port = ntohs(bound_addr.sin_port);
    const std::string bound_service = std::to_string(bound_port);

    // Client thread: open a TLS 1.2 connection to our listen socket.
    std::thread client_thread([&]() {
        ClientTest local_client;
        local_client.init(client_config);
        auto conn = local_client.connect("127.0.0.1", bound_service.c_str(), false, 1000);
        if (conn) {
            (void)conn->connect();
            (void)conn->shutdown();
        }
    });

    // Accept the incoming TCP connection on the test-owned socket.
    sockaddr_in peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    ASSERT_GE(accepted_fd, 0);

    char ip_buf[INET_ADDRSTRLEN]{};
    char service_buf[NI_MAXSERV]{};
    ASSERT_EQ(::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), service_buf,
                            sizeof(service_buf), NI_NUMERICHOST | NI_NUMERICSERV),
              0);

    // Hand the accepted fd to the factory under test.
    tls::Server::ConnectionPtr server_conn = server.wrap_accepted_fd(accepted_fd, ip_buf, service_buf);
    ASSERT_TRUE(server_conn);

    // Drive the SSL handshake.
    EXPECT_EQ(server_conn->accept(1000), tls::Connection::result_t::success);
    EXPECT_EQ(server_conn->state(), tls::Connection::state_t::connected);
    (void)server_conn->shutdown();

    if (client_thread.joinable()) {
        client_thread.join();
    }
    (void)::close(listen_fd);
}

} // namespace
