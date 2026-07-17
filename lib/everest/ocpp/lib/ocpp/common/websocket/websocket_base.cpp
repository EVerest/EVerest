// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2023 Pionix GmbH and Contributors to EVerest
#include <random>

#include <everest/logging.hpp>
#include <ocpp/common/websocket/websocket_base.hpp>
#include <websocketpp_utils/base64.hpp>
namespace ocpp {

bool should_attempt_reconnect(int attempts_made, int max_connection_attempts) {
    // -1 indicates to always attempt to reconnect
    return max_connection_attempts == -1 or attempts_made < max_connection_attempts;
}

long get_reconnect_backoff_ms(int attempt_number, long previous_backoff_ms, int retry_backoff_wait_minimum_s,
                              int retry_backoff_repeat_times, int retry_backoff_random_range_s) {
    // OCPP 2.0.1 part 4 section 5.3: after RetryBackOffRepeatTimes doublings the wait stays flat. The first
    // attempt (attempt_number == 1) is the initial wait, so doubling stops once attempt_number
    // exceeds retry_backoff_repeat_times + 1.
    if (attempt_number > retry_backoff_repeat_times + 1) {
        return previous_backoff_ms;
    }

    int random_number_s = 0;
    if (retry_backoff_random_range_s > 0) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distr(0, retry_backoff_random_range_s);
        random_number_s = distr(gen);
    }

    if (attempt_number <= 1) {
        return (static_cast<long>(retry_backoff_wait_minimum_s) + random_number_s) * 1000L;
    }
    return (previous_backoff_ms * 2) + (static_cast<long>(random_number_s) * 1000L);
}

WebsocketBase::WebsocketBase() :
    m_is_connected(false),
    connected_callback(nullptr),
    stopped_connecting_callback(nullptr),
    message_callback(nullptr),
    reconnect_timer(nullptr),
    connection_attempts(1),
    ping_cleared(true),
    ping_elapsed_s(0),
    pong_elapsed_s(0),
    reconnect_backoff_ms(0),
    shutting_down(false),
    reconnect_suppressed(false) {

    set_connection_options_base(connection_options);

    this->ping_timer = std::make_unique<Everest::SteadyTimer>();
    const auto auth_key = connection_options.authorization_key;
    if (auth_key.has_value() and auth_key.value().length() < 16) {
        EVLOG_warning << "AuthorizationKey with only " << auth_key.value().length()
                      << " characters has been configured";
    }
}

WebsocketBase::~WebsocketBase() {
    try {
        this->cancel_reconnect_timer();
    } catch (...) {
        EVLOG_error << "Exception during dtor call of reconnect timer cancellation";
        return;
    }
}

void WebsocketBase::set_connection_options_base(const WebsocketConnectionOptions& connection_options) {
    this->connection_options = connection_options;
}

void WebsocketBase::register_connected_callback(const std::function<void(OcppProtocolVersion protocol)>& callback) {
    this->connected_callback = callback;
}

void WebsocketBase::register_disconnected_callback(const std::function<void()>& callback) {
    this->disconnected_callback = callback;
}

void WebsocketBase::register_stopped_connecting_callback(
    const std::function<void(const WebsocketCloseReason reason)>& callback) {
    this->stopped_connecting_callback = callback;
}

void WebsocketBase::register_message_callback(const std::function<void(const std::string& message)>& callback) {
    this->message_callback = callback;
}

void WebsocketBase::register_connection_failed_callback(const std::function<void(ConnectionFailedReason)>& callback) {
    this->connection_failed_callback = callback;
}

bool WebsocketBase::initialized() {
    if (this->connected_callback == nullptr) {
        EVLOG_error << "Not properly initialized: please register connected callback.";
        return false;
    }
    if (this->stopped_connecting_callback == nullptr) {
        EVLOG_error << "Not properly initialized: please closed_callback.";
        return false;
    }
    if (this->message_callback == nullptr) {
        EVLOG_error << "Not properly initialized: please register message callback.";
        return false;
    }

    return true;
}

void WebsocketBase::disconnect(const WebsocketCloseReason code) {
    if (!this->initialized()) {
        EVLOG_error << "Cannot disconnect a websocket that was not initialized";
        return;
    }

    {
        const std::lock_guard<std::mutex> lk(this->reconnect_mutex);
        if (code == WebsocketCloseReason::Normal) {
            this->shutting_down = true;
        }

        if (this->reconnect_timer) {
            this->reconnect_timer.get()->cancel();
        }
    }

    if (this->ping_timer) {
        this->ping_timer->stop();
    }

    EVLOG_info << "Disconnecting websocket...";
    this->close(code, "");
}

void WebsocketBase::suppress_reconnect() {
    this->reconnect_suppressed = true;
}

void WebsocketBase::clear_reconnect_suppression() {
    this->reconnect_suppressed = false;
}

bool WebsocketBase::should_reconnect() const {
    if (this->reconnect_suppressed) {
        return false;
    }
    return should_attempt_reconnect(this->connection_attempts, this->connection_options.max_connection_attempts);
}

bool WebsocketBase::is_connected() {
    return this->m_is_connected;
}

std::optional<std::string> WebsocketBase::getAuthorizationHeader() {
    std::optional<std::string> auth_header = std::nullopt;
    const auto authorization_key = this->connection_options.authorization_key;
    if (authorization_key.has_value()) {
        EVLOG_debug << "AuthorizationKey present, encoding authentication header";
        const std::string plain_auth_header =
            this->connection_options.csms_uri.get_chargepoint_id() + ":" + authorization_key.value();

        // TODO (ioan): replace with libevse-security usage
        auth_header.emplace(std::string("Basic ") + ocpp::base64_encode(plain_auth_header));

        EVLOG_debug << "Basic Auth header: " << auth_header.value();
    }

    return auth_header;
}

void WebsocketBase::log_on_fail(const std::error_code& ec, const boost::system::error_code& transport_ec,
                                const int http_status) {
    EVLOG_error << "Failed to connect to websocket server"
                << ", error_code: " << ec.value() << ", reason: " << ec.message()
                << ", HTTP response code: " << http_status << ", category: " << ec.category().name()
                << ", transport error code: " << transport_ec.value()
                << ", Transport error category: " << transport_ec.category().name();
}

long WebsocketBase::get_reconnect_interval() {
    // Delegate to the shared section 5.3 backoff formula so the per-profile attempt loop here and the
    // ConnectivityManager cross-attempt/fallback scheduling stay in lockstep. connection_attempts is
    // 1 on the first try, matching get_reconnect_backoff_ms()'s attempt_number convention.
    this->reconnect_backoff_ms = static_cast<int>(get_reconnect_backoff_ms(
        this->connection_attempts, this->reconnect_backoff_ms, this->connection_options.retry_backoff_wait_minimum_s,
        this->connection_options.retry_backoff_repeat_times, this->connection_options.retry_backoff_random_range_s));
    return this->reconnect_backoff_ms;
}

void WebsocketBase::cancel_reconnect_timer() {
    const std::lock_guard<std::mutex> lk(this->reconnect_mutex);
    if (this->reconnect_timer) {
        this->reconnect_timer.get()->cancel();
    }
    this->reconnect_timer = nullptr;
}

void WebsocketBase::set_websocket_ping_interval(std::int32_t ping_interval_s, std::int32_t pong_timeout_s) {
    static constexpr std::int32_t PING_TIMER_INTERVAL = 1;

    if (this->ping_timer) {
        this->ping_timer->stop();
    }

    if (ping_interval_s > 0) {
        EVLOG_debug << "Started a ping interval of: " << ping_interval_s
                    << " s with a pong timeout of: " << pong_timeout_s << " s";

        this->ping_timer->interval(
            [this]() {
                if (false == ping_cleared.load()) { // We have a ping to which we require a response
                    pong_elapsed_s += PING_TIMER_INTERVAL;

                    // We have waited more than we should for the pong response
                    if (pong_elapsed_s >= this->connection_options.pong_timeout_s) {
                        on_pong_timeout("Pong not received from server!");

                        // Always clear ping value on a fail
                        ping_cleared.store(true);

                        // Reset the elapsed time
                        ping_elapsed_s = pong_elapsed_s = 0;

                        // Clear the ping timer
                        if (this->ping_timer) {
                            this->ping_timer->stop();
                        }
                    }
                } else { // We need to see if we have to send the ping
                    ping_elapsed_s += PING_TIMER_INTERVAL;

                    if (ping_elapsed_s >= this->connection_options.ping_interval_s) {
                        // Set the ping flag before sending it out
                        ping_cleared.store(false);
                        this->ping();

                        // Reset the elapsed time
                        ping_elapsed_s = pong_elapsed_s = 0;
                    }
                }
            },
            // Tick each second to see if we reached a timeout
            std::chrono::seconds(PING_TIMER_INTERVAL));
    }

    this->connection_options.ping_interval_s = ping_interval_s;
    this->connection_options.pong_timeout_s = pong_timeout_s;
}

void WebsocketBase::set_authorization_key(const std::string& authorization_key) {
    this->connection_options.authorization_key = authorization_key;
}

void WebsocketBase::on_pong_timeout(std::string msg) {
    EVLOG_info << "Reconnecting because of a pong timeout after " << this->connection_options.pong_timeout_s << "s"
               << " and with reason: " << msg;
    this->close(WebsocketCloseReason::ServiceRestart, "Pong timeout"); // application code will handle the reconnect
}

} // namespace ocpp
