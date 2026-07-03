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

TEST_F(TlsTest, Tls13MultiChainCertificateAuthorities) {
    // End-to-end test that the TLS 1.3 chain-selection path picks the chain
    // whose trust anchor's subject DN appears in the client's
    // certificate_authorities extension. Two chains are configured by the
    // fixture: chains[0] (leaf CN 00000000, root 'Root Trust Anchor') and
    // chains[1] (alt leaf CN 11111111, root 'Alternate Root Trust Anchor').
    // The client publishes only the alt root's DN; the server must reply with
    // the alt leaf (CN 11111111).
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";
    // Verify against the alt root because that is the chain we expect the
    // server to send back.
    client_config.verify_locations_file = "alt_server_root_cert.pem";

    std::map<std::string, std::string> subject;
    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (!connection) {
            return;
        }
        // Inject the alt root DN into the certificate_authorities extension on
        // the client SSL handle before the handshake runs. This is the signal
        // the server's TLS 1.3 chain dispatcher reads via
        // SSL_get0_peer_CA_list to pick chains[1].
        //
        // Connection::ssl_context() is marked [[deprecated]] for callers
        // outside the library (it is only kept for IsoMux); the surrounding
        // pragmas suppress the deprecation warning here because the test
        // legitimately needs the raw SSL* to drive SSL_add1_to_CA_list.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        SSL* ssl = connection->ssl_context();
#pragma GCC diagnostic pop
        ASSERT_NE(ssl, nullptr);
        auto alt_roots = openssl::load_certificates("alt_server_root_cert.pem");
        ASSERT_FALSE(alt_roots.empty());
        ASSERT_EQ(SSL_add1_to_CA_list(ssl, alt_roots.front().get()), 1);

        if (connection->connect() == result_t::success) {
            this->set(ClientTest::flags_t::connected);
            subject = openssl::certificate_subject(connection->peer_certificate());
            (void)connection->shutdown();
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

TEST_F(TlsTest, Tls13NoMatchServesDefault) {
    // End-to-end test of the TLS 1.3 no-match path: the client advertises a
    // certificate_authorities DN the server holds no chain for (the client
    // root, C=DE/L=Frankfurt/CN=Root Trust Anchor). select_by_dn_list returns
    // no chain and the server must keep the default chain already configured
    // on the SSL_CTX - chains[0], leaf CN 00000000 - instead of aborting the
    // handshake.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";
    // Verify against chains[0]'s root because the default chain is expected.
    client_config.verify_locations_file = "server_root_cert.pem";

    std::map<std::string, std::string> subject;
    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (!connection) {
            return;
        }
        // Inject a DN matching neither server chain's trust anchor. See
        // Tls13MultiChainCertificateAuthorities for the ssl_context() pragma
        // rationale.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        SSL* ssl = connection->ssl_context();
#pragma GCC diagnostic pop
        ASSERT_NE(ssl, nullptr);
        auto foreign_roots = openssl::load_certificates("client_root_cert.pem");
        ASSERT_FALSE(foreign_roots.empty());
        ASSERT_EQ(SSL_add1_to_CA_list(ssl, foreign_roots.front().get()), 1);

        if (connection->connect() == result_t::success) {
            this->set(ClientTest::flags_t::connected);
            subject = openssl::certificate_subject(connection->peer_certificate());
            (void)connection->shutdown();
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], server_root_CN);
}

TEST_F(TlsTest, Tls13NoMatchSkipsUnverifiedDefault) {
    // chains[0] is deliberately broken: its chain file holds only the leaf, so
    // verify_chain() cannot build a path to the trust anchor (missing
    // intermediate) and the chain is excluded at config time. The default
    // served to no-match clients must be the first chain that PASSED
    // verification - chains[1], alt leaf CN 11111111 - not the broken
    // chains[0]; otherwise every no-match client is served a chain it cannot
    // build a path for.
    server_config.chains[0].certificate_chain_file = "server_cert.pem";
    server_config.chains[0].ocsp_response_files = {"ocsp_response.der"};
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";
    // Verify against chains[1]'s root because the first verified chain is
    // expected to be served.
    client_config.verify_locations_file = "alt_server_root_cert.pem";

    std::map<std::string, std::string> subject;
    auto client_handler_fn = [this, &subject](tls::Client::ConnectionPtr& connection) {
        if (!connection) {
            return;
        }
        // Inject a DN matching neither server chain's trust anchor. See
        // Tls13MultiChainCertificateAuthorities for the ssl_context() pragma
        // rationale.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        SSL* ssl = connection->ssl_context();
#pragma GCC diagnostic pop
        ASSERT_NE(ssl, nullptr);
        auto foreign_roots = openssl::load_certificates("client_root_cert.pem");
        ASSERT_FALSE(foreign_roots.empty());
        ASSERT_EQ(SSL_add1_to_CA_list(ssl, foreign_roots.front().get()), 1);

        if (connection->connect() == result_t::success) {
            this->set(ClientTest::flags_t::connected);
            subject = openssl::certificate_subject(connection->peer_certificate());
            (void)connection->shutdown();
        }
    };

    start();
    connect(client_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

TEST_F(TlsTest, MixedVersionServerSelectsAltChain) {
    // One server offering both TLS 1.2 (cipher_list) and TLS 1.3
    // (ciphersuites). The SSL_version dispatch in the certificate callback
    // must route a TLS 1.2 trusted_ca_keys client and a TLS 1.3
    // certificate_authorities client to the same alt chain (leaf CN 11111111).
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";

    std::map<std::string, std::string> subject;
    int negotiated_version{0};

    // TLS 1.2 leg: trusted_ca_keys advertising the alt root; an empty
    // ciphersuites string caps the client at TLS 1.2.
    client_config.ciphersuites = "";
    client_config.trusted_ca_keys = true;
    client_config.verify_locations_file = "alt_server_root_cert.pem";
    add_ta_cert_hash("alt_server_root_cert.pem");

    auto tls12_handler_fn = [this, &subject, &negotiated_version](tls::Client::ConnectionPtr& connection) {
        if (!connection) {
            return;
        }
        if (connection->connect() == result_t::success) {
            this->set(ClientTest::flags_t::connected);
            subject = openssl::certificate_subject(connection->peer_certificate());
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            negotiated_version = SSL_version(connection->ssl_context());
#pragma GCC diagnostic pop
            (void)connection->shutdown();
        }
    };

    start();
    connect(tls12_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(negotiated_version, TLS1_2_VERSION);
    EXPECT_EQ(subject["CN"], alt_server_root_CN);

    // TLS 1.3 leg against the same running server: certificate_authorities
    // advertising the alt root's DN.
    subject.clear();
    negotiated_version = 0;
    client_config.trusted_ca_keys = false;
    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;

    auto tls13_handler_fn = [this, &subject, &negotiated_version](tls::Client::ConnectionPtr& connection) {
        if (!connection) {
            return;
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        SSL* ssl = connection->ssl_context();
#pragma GCC diagnostic pop
        ASSERT_NE(ssl, nullptr);
        auto alt_roots = openssl::load_certificates("alt_server_root_cert.pem");
        ASSERT_FALSE(alt_roots.empty());
        ASSERT_EQ(SSL_add1_to_CA_list(ssl, alt_roots.front().get()), 1);

        if (connection->connect() == result_t::success) {
            this->set(ClientTest::flags_t::connected);
            subject = openssl::certificate_subject(connection->peer_certificate());
            negotiated_version = SSL_version(ssl);
            (void)connection->shutdown();
        }
    };

    connect(tls13_handler_fn);
    EXPECT_TRUE(is_set(flags_t::connected));
    EXPECT_EQ(negotiated_version, TLS1_3_VERSION);
    EXPECT_EQ(subject["CN"], alt_server_root_CN);
}

} // namespace
