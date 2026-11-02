// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest

#include <chrono>
#include <everest/logging.hpp>
#include <gtest/gtest.h>
#include <set>
#include <sys/socket.h>
#include <thread>

#include "../helpers/WebSocketTestClient.hpp"
#include "../server/WebsocketServer.hpp"

using namespace server;

namespace server {
class WebSocketServerTestAccess {
public:
    static constexpr std::size_t message_limit() {
        return WebSocketServer::MAX_PENDING_MESSAGES_PER_CLIENT;
    }
    static constexpr std::size_t payload_limit() {
        return WebSocketServer::MAX_PENDING_PAYLOAD_BYTES_PER_CLIENT;
    }
    static bool set_send_buffer_size(WebSocketServer& server, const TransportInterface::ClientId& id, int bytes) {
        std::lock_guard<std::mutex> lock(server.m_clients_mutex);
        return setsockopt(lws_get_socket_fd(server.m_clients.at(id).wsi), SOL_SOCKET, SO_SNDBUF, &bytes,
                          sizeof(bytes)) == 0;
    }
    static bool queue_empty(WebSocketServer& server, const TransportInterface::ClientId& id) {
        std::lock_guard<std::mutex> lock(server.m_clients_mutex);
        const auto it = server.m_clients.find(id);
        return it != server.m_clients.end() && it->second.outgoing_messages.empty() &&
               it->second.pending_payload_bytes == 0;
    }
    static std::size_t queued_message_count(WebSocketServer& server, const TransportInterface::ClientId& id) {
        std::lock_guard<std::mutex> lock(server.m_clients_mutex);
        return server.m_clients.at(id).outgoing_messages.size();
    }
};
} // namespace server

class WebSocketServerTest : public ::testing::Test {
protected:
    static constexpr std::chrono::seconds CLIENT_HANDSHAKE_TIMEOUT{2};
    static constexpr std::chrono::seconds CLIENT_REGISTRATION_TIMEOUT{2};
    static constexpr std::chrono::seconds CLIENT_DISCONNECTION_TIMEOUT{2};
    static constexpr std::chrono::seconds MESSAGE_RECEIVE_TIMEOUT{2};
    static constexpr std::chrono::seconds LARGE_MESSAGE_RECEIVE_TIMEOUT{5};

    std::unique_ptr<WebSocketServer> ws_server;
    int test_port = 8080;

    void SetUp() override {
        ws_server = std::make_unique<WebSocketServer>(false, test_port, "lo");
        lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

        ws_server->on_client_connected = [this](const TransportInterface::ClientId& client_id,
                                                const server::TransportInterface::Address& address) {
            // Handle client connected logic here
            std::unique_lock<std::mutex> lock(cv_mutex);
            try {
                connected_clients.push_back(client_id);
                service_paused = pause_on_connect;
                cv.notify_all();
                cv.wait(lock, [this] { return !pause_on_connect; });
                service_paused = false;
            } catch (const std::exception& e) {
                EVLOG_error << "Exception occurred while handling client connected: " << e.what();
            }
        };

        ws_server->on_client_disconnected = [this](const TransportInterface::ClientId& client_id) {
            // Handle client disconnected logic here
            std::lock_guard<std::mutex> lock(cv_mutex);
            try {
                connected_clients.erase(std::remove(connected_clients.begin(), connected_clients.end(), client_id),
                                        connected_clients.end());
                cv.notify_all();
            } catch (const std::exception& e) {
                EVLOG_error << "Exception occurred while handling client disconnected: " << e.what();
            }
        };

        ws_server->on_data_available = [this](const TransportInterface::ClientId& client_id,
                                              const server::TransportInterface::Data& data) {
            // Handle data available logic here
            std::lock_guard<std::mutex> lock(cv_mutex);
            try {
                received_data[client_id] = std::string(data.begin(), data.end());
                cv.notify_all();
            } catch (const std::exception& e) {
                EVLOG_error << "Exception occurred while handling data available: " << e.what();
            }
        };

        ws_server->start_server();
    }

    std::vector<TransportInterface::ClientId> get_connected_clients() {
        std::lock_guard<std::mutex> lock(cv_mutex);
        return connected_clients;
    }

    // Connected client id's
    std::vector<TransportInterface::ClientId> connected_clients;

    // Condition variable to wait requests
    std::condition_variable cv;
    std::mutex cv_mutex;
    bool pause_on_connect = false;
    bool service_paused = false;

    void pause_server_on_connect() {
        std::lock_guard<std::mutex> lock(cv_mutex);
        pause_on_connect = true;
    }

    bool wait_for_service_pause() {
        constexpr std::chrono::seconds SERVICE_PAUSE_TIMEOUT{2};
        std::unique_lock<std::mutex> lock(cv_mutex);
        return cv.wait_for(lock, SERVICE_PAUSE_TIMEOUT, [this] { return service_paused; });
    }

    void resume_server() {
        std::lock_guard<std::mutex> lock(cv_mutex);
        pause_on_connect = false;
        cv.notify_all();
    }

    bool wait_for_client_count(std::size_t count, std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(cv_mutex);
        return cv.wait_for(lock, timeout, [this, count] { return connected_clients.size() == count; });
    }

    template <typename Predicate> bool wait_for_queue_progress(Predicate predicate) {
        constexpr std::chrono::seconds QUEUE_PROGRESS_TIMEOUT{2};
        constexpr std::chrono::milliseconds QUEUE_POLL_INTERVAL{1};
        const auto deadline = std::chrono::steady_clock::now() + QUEUE_PROGRESS_TIMEOUT;
        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(QUEUE_POLL_INTERVAL);
        }
        return true;
    }

    // Received data with client id
    std::unordered_map<TransportInterface::ClientId, std::string> received_data;

    void TearDown() override {
        resume_server();
        ws_server->stop_server();
    }
};

// Test: Start and stop WebSocket server
TEST_F(WebSocketServerTest, WebSocketServerStarts) {
    ASSERT_TRUE(ws_server->running());
    TearDown();
    ASSERT_FALSE(ws_server->running());
}

// Test: Connect WebSocket client to server
TEST_F(WebSocketServerTest, ClientCanConnect) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.wait_until_connected(std::chrono::milliseconds(100)));
}

// Test: Connect several WebSocket clients to server
TEST_F(WebSocketServerTest, MultipleClientsCanConnect) {
    WebSocketTestClient client1("localhost", test_port);
    WebSocketTestClient client2("localhost", test_port);
    WebSocketTestClient client3("localhost", test_port);

    ASSERT_TRUE(client1.connect());
    ASSERT_TRUE(client2.connect());
    ASSERT_TRUE(client3.connect());

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    ASSERT_TRUE(client1.is_connected());
    ASSERT_TRUE(client2.is_connected());
    ASSERT_TRUE(client3.is_connected());

    ASSERT_TRUE(ws_server->connections_count() == 3);
}

// Test: Client can send data to server
TEST_F(WebSocketServerTest, ClientCanSendAndReceiveData) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.wait_until_connected(std::chrono::milliseconds(100)));

    client.send("Hello World!");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait_for(lock, std::chrono::seconds(1), [&] { return !received_data.empty(); });
    lock.unlock();

    ASSERT_EQ(ws_server->connections_count(), 1);
    ASSERT_EQ(received_data[(get_connected_clients()[0])], "Hello World!");
}

// Test: Server can send data to client
TEST_F(WebSocketServerTest, ServerCanSendDataToClient) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.wait_until_connected(std::chrono::milliseconds(100)));

    std::string message = "Hello from server!";
    ws_server->send_data(get_connected_clients()[0], std::vector<uint8_t>(message.begin(), message.end()));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string received_data = client.wait_for_data(std::chrono::seconds(1), false);

    ASSERT_FALSE(received_data.empty());
    ASSERT_EQ(received_data, "Hello from server!");
}

TEST_F(WebSocketServerTest, ConcurrentSendersDeliverEveryQueuedMessage) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.wait_until_connected(std::chrono::milliseconds(100)));

    constexpr int sender_count = 4;
    constexpr int messages_per_sender = 25;
    const auto client_id = get_connected_clients().front();
    std::vector<std::thread> senders;
    for (int sender = 0; sender < sender_count; ++sender) {
        senders.emplace_back([&, sender] {
            for (int message = 0; message < messages_per_sender; ++message) {
                const auto payload = std::to_string(sender) + ":" + std::to_string(message);
                ws_server->send_data(client_id, std::vector<uint8_t>(payload.begin(), payload.end()));
            }
        });
    }
    for (auto& sender : senders) {
        sender.join();
    }

    std::set<std::string> received;
    for (int i = 0; i < sender_count * messages_per_sender; ++i) {
        const auto message = client.wait_for_data(MESSAGE_RECEIVE_TIMEOUT, false);
        ASSERT_FALSE(message.empty()) << "Timed out waiting for queued message " << i;
        received.insert(message);
    }
    ASSERT_EQ(received.size(), sender_count * messages_per_sender);
}

// Test: Server kills client connection
TEST_F(WebSocketServerTest, ServerCanKillClientConnection) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.wait_until_connected(std::chrono::milliseconds(100)));

    client.send("Hello World!");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    client.wait_for_data(std::chrono::seconds(1));

    ASSERT_EQ(ws_server->connections_count(), 1);
    ASSERT_EQ(received_data[get_connected_clients()[0]], "Hello World!");

    ws_server->kill_client_connection(get_connected_clients()[0], "Test kill");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(ws_server->connections_count(), 0);
    ASSERT_FALSE(client.is_connected());
}

class WebSocketServerQueueTest : public WebSocketServerTest, public ::testing::WithParamInterface<bool> {
protected:
    bool connect_paused(WebSocketTestClient& client) {
        pause_server_on_connect();
        return client.connect() && wait_for_service_pause() && client.wait_until_connected(CLIENT_HANDSHAKE_TIMEOUT);
    }

    void enqueue(const TransportInterface::ClientId& id, const TransportInterface::Data& data) {
        if (GetParam()) {
            ws_server->send_data(id, data);
        } else {
            ws_server->send_data(data);
        }
    }
};

TEST_P(WebSocketServerQueueTest, AcceptsMessageCountBoundaryAndPreservesOrder) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(connect_paused(client));
    const auto id = get_connected_clients().back();
    for (std::size_t i = 0; i < WebSocketServerTestAccess::message_limit(); ++i) {
        const auto payload = std::to_string(i);
        enqueue(id, TransportInterface::Data(payload.begin(), payload.end()));
    }
    resume_server();
    for (std::size_t i = 0; i < WebSocketServerTestAccess::message_limit(); ++i) {
        ASSERT_EQ(client.wait_for_data(MESSAGE_RECEIVE_TIMEOUT, false), std::to_string(i));
    }
    ASSERT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
}

TEST_P(WebSocketServerQueueTest, AcceptsPayloadByteBoundaryAndReleasesAccounting) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(connect_paused(client));
    const auto id = get_connected_clients().back();
    const TransportInterface::Data payload(WebSocketServerTestAccess::payload_limit(), 'a');
    enqueue(id, payload);
    resume_server();
    ASSERT_EQ(client.wait_for_data(LARGE_MESSAGE_RECEIVE_TIMEOUT, false), std::string(payload.begin(), payload.end()));
    ASSERT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
    enqueue(id, payload);
    ASSERT_EQ(client.wait_for_data(LARGE_MESSAGE_RECEIVE_TIMEOUT, false), std::string(payload.begin(), payload.end()));
}

TEST_P(WebSocketServerQueueTest, ClosesOnMessageCountOverflowIncludingEmptyMessages) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(connect_paused(client));
    const auto id = get_connected_clients().back();
    for (std::size_t i = 0; i <= WebSocketServerTestAccess::message_limit(); ++i) {
        enqueue(id, {});
    }
    ASSERT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
    ws_server->kill_client_connection(id, "Repeated close request");
    enqueue(id, {'x'});
    ASSERT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
    resume_server();
    ASSERT_TRUE(wait_for_client_count(0, CLIENT_DISCONNECTION_TIMEOUT));
    EXPECT_EQ(ws_server->connections_count(), 0);
}

TEST_P(WebSocketServerQueueTest, ClosesOnPayloadByteOverflow) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(connect_paused(client));
    const auto id = get_connected_clients().back();
    const TransportInterface::Data payload(WebSocketServerTestAccess::payload_limit() / 2, 'a');
    enqueue(id, payload);
    enqueue(id, payload);
    enqueue(id, {'x'});
    ASSERT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
    resume_server();
    ASSERT_TRUE(wait_for_client_count(0, CLIENT_DISCONNECTION_TIMEOUT));
}

TEST_P(WebSocketServerQueueTest, RejectsOversizedMessageAndAllowsFreshConnection) {
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(connect_paused(client));
    const auto id = get_connected_clients().back();
    enqueue(id, TransportInterface::Data(WebSocketServerTestAccess::payload_limit() + 1, 'a'));
    ASSERT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
    resume_server();
    ASSERT_TRUE(wait_for_client_count(0, CLIENT_DISCONNECTION_TIMEOUT));

    WebSocketTestClient replacement("localhost", test_port);
    ASSERT_TRUE(replacement.connect());
    ASSERT_TRUE(wait_for_client_count(1, CLIENT_REGISTRATION_TIMEOUT));
    ASSERT_TRUE(replacement.wait_until_connected(CLIENT_HANDSHAKE_TIMEOUT));
    const TransportInterface::Data payload(WebSocketServerTestAccess::payload_limit(), 'b');
    enqueue(get_connected_clients().back(), payload);
    ASSERT_EQ(replacement.wait_for_data(LARGE_MESSAGE_RECEIVE_TIMEOUT, false),
              std::string(payload.begin(), payload.end()));
}

INSTANTIATE_TEST_SUITE_P(SendPaths, WebSocketServerQueueTest, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return info.param ? "Targeted" : "Broadcast";
                         });

TEST_F(WebSocketServerTest, BroadcastOverflowClosesOnlyTheOverloadedClient) {
    WebSocketTestClient healthy("localhost", test_port);
    ASSERT_TRUE(healthy.connect());
    ASSERT_TRUE(wait_for_client_count(1, CLIENT_REGISTRATION_TIMEOUT));
    ASSERT_TRUE(healthy.wait_until_connected(CLIENT_HANDSHAKE_TIMEOUT));

    pause_server_on_connect();
    WebSocketTestClient overloaded("localhost", test_port);
    ASSERT_TRUE(overloaded.connect());
    ASSERT_TRUE(wait_for_service_pause());
    const auto overloaded_id = get_connected_clients().back();
    for (std::size_t i = 0; i < WebSocketServerTestAccess::message_limit(); ++i) {
        ws_server->send_data(overloaded_id, {'x'});
    }
    ws_server->send_data(TransportInterface::Data{'h'});
    resume_server();
    ASSERT_TRUE(wait_for_client_count(1, CLIENT_DISCONNECTION_TIMEOUT));
    ASSERT_EQ(healthy.wait_for_data(MESSAGE_RECEIVE_TIMEOUT, false), "h");
    EXPECT_EQ(ws_server->connections_count(), 1);
}

TEST_F(WebSocketServerTest, PausedReaderReceivesBufferedMessagesExactlyOnce) {
    constexpr std::chrono::milliseconds EXTRA_MESSAGE_OBSERVATION_TIMEOUT{50};
    pause_server_on_connect();
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(wait_for_service_pause());
    ASSERT_TRUE(client.wait_until_connected(CLIENT_HANDSHAKE_TIMEOUT));
    const auto id = get_connected_clients().back();
    constexpr int SOCKET_SEND_BUFFER_BYTES = 4096;
    ASSERT_TRUE(WebSocketServerTestAccess::set_send_buffer_size(*ws_server, id, SOCKET_SEND_BUFFER_BYTES));
    client.stop_lws_service_thread();
    const TransportInterface::Data first(WebSocketServerTestAccess::payload_limit() / 2, 'a');
    const TransportInterface::Data second(first.size(), 'b');
    ws_server->send_data(id, first);
    ws_server->send_data(id, second);
    resume_server();
    ASSERT_TRUE(
        wait_for_queue_progress([&] { return WebSocketServerTestAccess::queued_message_count(*ws_server, id) == 1; }));
    client.start_lws_service_thread();
    ASSERT_EQ(client.wait_for_data(LARGE_MESSAGE_RECEIVE_TIMEOUT, false), std::string(first.begin(), first.end()));
    ASSERT_EQ(client.wait_for_data(LARGE_MESSAGE_RECEIVE_TIMEOUT, false), std::string(second.begin(), second.end()));
    EXPECT_TRUE(WebSocketServerTestAccess::queue_empty(*ws_server, id));
    EXPECT_FALSE(client.wait_for_response(EXTRA_MESSAGE_OBSERVATION_TIMEOUT));
}

TEST_F(WebSocketServerTest, ForcedCloseDoesNotWaitForPausedReader) {
    pause_server_on_connect();
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(wait_for_service_pause());
    ASSERT_TRUE(client.wait_until_connected(CLIENT_HANDSHAKE_TIMEOUT));
    const auto id = get_connected_clients().back();
    constexpr int SOCKET_SEND_BUFFER_BYTES = 4096;
    ASSERT_TRUE(WebSocketServerTestAccess::set_send_buffer_size(*ws_server, id, SOCKET_SEND_BUFFER_BYTES));
    client.stop_lws_service_thread();
    ws_server->send_data(id, TransportInterface::Data(WebSocketServerTestAccess::payload_limit(), 'a'));
    resume_server();
    ASSERT_TRUE(wait_for_queue_progress([&] { return WebSocketServerTestAccess::queue_empty(*ws_server, id); }));
    ws_server->kill_client_connection(id, "Non-reading client");
    ASSERT_TRUE(wait_for_client_count(0, CLIENT_DISCONNECTION_TIMEOUT));
    EXPECT_EQ(ws_server->connections_count(), 0);
}

TEST_F(WebSocketServerTest, ShutdownClearsQueuedMessagesAndPendingCloses) {
    pause_server_on_connect();
    WebSocketTestClient client("localhost", test_port);
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(wait_for_service_pause());
    const auto id = get_connected_clients().back();
    ws_server->send_data(id, TransportInterface::Data(WebSocketServerTestAccess::payload_limit(), 'a'));
    ws_server->kill_client_connection(id, "Shutdown");
    resume_server();
    ASSERT_TRUE(ws_server->stop_server());
    EXPECT_EQ(ws_server->connections_count(), 0);
    EXPECT_TRUE(wait_for_client_count(0, CLIENT_DISCONNECTION_TIMEOUT));
    ASSERT_TRUE(ws_server->start_server());
    WebSocketTestClient replacement("localhost", test_port);
    ASSERT_TRUE(replacement.connect());
    ASSERT_TRUE(wait_for_client_count(1, CLIENT_REGISTRATION_TIMEOUT));
    ws_server->send_data(get_connected_clients().back(), {'x'});
    EXPECT_EQ(replacement.wait_for_data(MESSAGE_RECEIVE_TIMEOUT, false), "x");
}
