// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/io/utilities/event_client_async_policy.hpp>
#include <everest/io/utilities/generic_error_state.hpp>
#include <everest/util/async/monitor.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>
#include <future>
#include <memory>
#include <queue>
#include <thread>
#include <type_traits>

namespace everest::lib::io::event {
class fd_event_handler;

/**
 * @enum action_status
 * @brief Possible outcomes of actions. Internal usage only.
 */
enum class action_status {
    empty,
    success,
    fail
};

/**
 * @struct client_status_internal
 * Accumulation fo client status for internal usage
 */
struct client_status_internal {
    /** filedescriptor */
    int fd{-1};
    /** status */
    bool ok{false};
    /** generation */
    std::uint64_t generation{0};
};

/**
 * Implements the type independant part of the fd_event_client. This includes all event handling
 * with \ref fd_event_handler. Not to be used on its own.
 */
class generic_fd_event_client_impl : public fd_event_sync_interface, protected utilities::generic_error_state {
public:
    /**
     * @var cb_error
     * @brief Prototype for an error callback
     */
    using cb_error = generic_error_state::cb_error;
    /**
     * @var action
     * @brief Prototype for action_status
     */
    using action = std::function<action_status()>;
    /**
     * @var error_status
     * @brief Prototype for error_status
     */
    using error_status = std::function<int()>;

    /**
     * @var ready_action
     * @brief Prototype for the callback to be called on client ready
     */
    using ready_action = std::function<void()>;

    /**
     * @brief Access to the internal event handler
     * @details Call \ref sync on read (POLLIN/EPOLLIN).
     * Override if an additional layer of event handler is necessary.
     * @return The file descriptor of the internal event handler
     */
    int get_poll_fd() override;

    /**
     * @name Syncing the internal event handler
     * Description
     * @{
     */

    /**
     * @brief Sync without a timeout. May not be called in any registered callback.
     * @details Blocks until an event occurs. Override if any special handling for sync is needed.
     * @return Result of sync operation
     */
    sync_status sync() override;

    /**
     * @brief Sync with timeout. May not be called in any registered callback
     * @details Blocks until an event occurs or the timeout is reached
     * @param[in] timeout
     * @return Result of sync operation
     */
    template <class Rep, class Period> sync_status sync(std::chrono::duration<Rep, Period> timeout) {
        return sync_impl(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    }

    /**
     * @brief Sync with timeout. May not be called in any registered callback
     * @details Blocks until an event occurs ot the timeout is reached
     * @return Result of sync operation
     */
    sync_status sync_impl(int timeout_ms);
    /**
     * @}
     */

    /**
     * @brief Register an error handler
     * @details The error handler is called when an error occurs or is cleared
     * @param[in] handler The callback to be used as error handler
     */
    void set_error_handler(cb_error const& handler);

    /**
     * @brief Register a function, that is called once the client is ready
     * @details This callback is performed immediately, if the client is in a ready state
     *          or the client is ready. The action is persistent and called whenever the client
     *          is ready after reset.
     * @param[in] item The callback
     */
    void set_on_ready_action(std::function<void()>&& item);

protected:
    /**
     * @brief Constructor.
     * @details Functionality passed in by functors
     */
    generic_fd_event_client_impl(action const& send_one, action const& receive_one, action const& reset_client,
                                 error_status const& get_error);
    ~generic_fd_event_client_impl();

    /**
     * @brief Handling of RX operations
     * @return True on success, false otherwise
     */
    bool rx_handler();

    /**
     * @brief Handling of TX operations
     * @details Writing one queued message at a time to the file descriptor \p fd.
     *          When there are no more queued messages, notifications for writing [(E)POLLOUT] are disabled.
     * @param[in] fd The filedescriptor for writing
     * @return True on success, false otherwise
     */
    bool tx_handler(int fd);

    /**
     * @brief Handle errors errors.
     * @details This triggers a reset of the client and calls the \p error_handler
     *          with the current error.
     */
    void error_handler();

    /**
     * @brief Setup io event handling.
     * @details This registers a generic event handler for reading, writing and error handling.
     *          Errors and reading will be handled continuously, writing on demand.
     * @param[in] fd Filedescriptor to be monitored.
     */
    void setup_io_event_handler(int fd);

    /**
     * @brief Register setup of I/O event handling for client ready
     * @details This prepares the I/O event handling to be setup once the client is ready.
     *          This includes preparation of the I/O event handler and checking for errors
     *          as well as adding the on_ready_action to the action queue.
     */
    void prepare_io_event_handler();

    /**
     * @brief Setup error event handling.
     * @details This registers the error event_status event with the event handler, which is independent of
     * the io event handling.
     */
    bool setup_error_event_handler();

    /**
     * @brief Remove event handler
     * @details Stop event monitoring and event handling for \p fd
     * @param[in] fd The file descriptor
     * @return True on success, false otherwise
     */
    bool unregister_source(int fd);

    /**
     * @brief Set the internal error status and notify the error_status event handler
     * @param[in] error_code The current error code
     * @return False on error, true otherwise
     */
    bool set_error_status_and_notify(int error_code);

    /**
     * @brief To be called on client ready (internally)
     * @details This set's the client_status and notifies the internal event
     * @param[in] generation The connection generation used for stale-result suppression.
     * @param[in] ok Client status. 'true' on ready/success, 'false' otherwise
     * @param[in] fd The filedescriptor of the client's socket
     */
    void on_client_ready(std::uint64_t generation, bool ok, int fd);

    /**
     * @brief Set the generation accepted by m_connected_event_fd.
     * @param[in] generation Current valid connection generation.
     */
    void set_connected_generation(std::uint64_t generation);

    /**
     * @brief Add an action to the action queue of the event handler
     * @details This adds a new action to the action queue of the event handler. Action's are called
     *          after all event handling is done. For this reason it is safe register/unregister for
     *          events and other do other things, that effect the event handler within an action.
     * @param[in] item
     */
    void add_action(std::function<void()>&& item);

    /**
     * @brief Register an action-only callback for an event descriptor owned by this class.
     * @param[in] handler The callback to invoke when the event descriptor notifies.
     */
    void register_async_connect_event_handler(event::event_fd* event_fd, std::function<void()> handler);

    /// @cond
    std::unique_ptr<event::fd_event_handler> m_event_handler;
    event::event_fd m_io_event_fd;
    event::event_fd m_error_status_event_fd;
    event::event_fd m_connected_event_fd;

    cb_error m_error;
    action m_send_one;
    action m_receive_one;
    action m_reset_client;
    error_status m_get_error;
    util::monitor<client_status_internal> m_client_status;
    ready_action m_on_ready_action;
    std::atomic_uint64_t m_connected_generation{0};
    /// @endcond
};

/**
 * Implements the type dependant part of the fd_event_client. Best to be constructed via \ref fd_event_client
 * Refer to it also for the contraints of the @tparam ClientPolicy
 * @tparam ClientInterface Provides the interface used for rx callbacks. This is the policy definition
 * \code{.cpp}
 *   class ClientInterfacePolicy {
 *   public:
 *       using cb_rx = std::function<void(payload const& payload, interface& device)>; // type of rx callback
 *       virtual ~interface() = default;
 *       virtual bool tx(payload const& payload) = 0; // definition fo payload funcion
 *   };
 * \endcode
 */
template <class ClientPolicy, class ClientInterfacePolicy>
class generic_fd_event_client : public ClientInterfacePolicy, public generic_fd_event_client_impl {
public:
    /**
     * @var cb_rx
     * @brief Prototype of the receive callback
     */
    using cb_rx = typename ClientInterfacePolicy::cb_rx;

    /**
     * @var ClientPayloadT
     * @brief Type of the payload to be handled by the client
     */
    using ClientPayloadT = typename ClientPolicy::PayloadT;

    /**
     * @brief Construction of the generic event handler
     * @details All parameters will be forwarded to the open(...) function of the \p ClientPolicy
     */
    template <class... ArgsT>
    generic_fd_event_client(ArgsT... args) :
        generic_fd_event_client_impl([this]() { return send_one(); }, [this]() { return receive_one(); },
                                     [this]() { return reset_client(); },
                                     [this]() { return m_handle ? m_handle->get_error() : 0; }) {
        m_async_connect_state = std::make_shared<async_connect_state>();
        register_async_connect_event_handler(&m_async_connect_state->ready_event,
                                             [this]() { process_async_connect_results(); });
        init<ClientPolicy>(args...);
    }

    ~generic_fd_event_client() override {
        invalidate_async_connect_state();
    }

    /**
     * @brief Register a callback for RX.
     * @details This handler will be called with all new data available
     * @param[in] handler The callback used as RX handler
     */
    void set_rx_handler(cb_rx const& handler) {
        add_action([this, handler]() { m_rx = std::move(handler); });
    }

    /**
     * @brief Send data
     * @details This buffers the incoming data and notifies. It will be send as soon as the file descriptor
     * is ready for transmission (POLLOUT / EPOLLOUT)
     * @param[in] payload The data to be transmitted.
     * @return False if client is \ref on_error. False otherwise
     */
    bool tx(ClientPayloadT const& payload) override {
        if (on_error()) {
            return false;
        }
        m_tx_buffer.emplace(payload);
        m_io_event_fd.notify();
        return true;
    }

    /**
     * @brief Reset the client.
     * @details Use this function to reset the client if on error or any other reason.
     * This destructs the client as defined by \p ClientPolicy and opens it again.
     * @return True if the client could be successfully created. False otherwise
     */
    bool reset() {
        auto generation = ++m_connect_generation;
        set_connected_generation(generation);
        add_action([this]() { reset_impl(); });
        return true;
    }

    /**
     * @brief Get the current error state
     * @return True if on error, false otherwise
     */
    bool on_error() {
        return generic_error_state::on_error();
    }

    /**
     * @brief Access to the raw client.
     * @details The reference is bound to the lifetime of this object. The object pointed to
     * changes with each call to 'reset' and may be 'nullptr'
     * @return Reference to the raw client.
     */
    std::unique_ptr<ClientPolicy> const& get_raw_handler() {
        return m_handle;
    }

private:
    template <class T, class... ArgsT> std::enable_if_t<utilities::event_client_async_policy_v<T>> init(ArgsT... args) {
        m_open_device = [this, args...]() {
            auto setup_ok = m_handle->setup(args...);
            if (setup_ok) {
                auto connect_generation = m_connect_generation.load();
                auto state = m_async_connect_state;
                auto handle = std::move(m_handle);
                std::thread([state = std::move(state), handle = std::move(handle), connect_generation]() mutable {
                    bool ok = false;
                    int fd = -1;
                    try {
                        handle->connect([&](bool result_ok, int result_fd) {
                            ok = result_ok;
                            fd = result_fd;
                        });
                    } catch (...) {
                        ok = false;
                        fd = -1;
                    }

                    if (not state or not state->active.load(std::memory_order_acquire)) {
                        return;
                    }

                    auto inserted = state->connect_results.emplace(
                        async_connect_result{connect_generation, ok, fd, std::move(handle)});
                    if (inserted != 0) {
                        state->ready_event.notify();
                    }
                }).detach();
            } else {
                auto generation = m_connect_generation.load();
                on_client_ready(generation, false, -1);
            }
        };
        reset();
    }

    template <class T, class... ArgsT>
    std::enable_if_t<not utilities::event_client_async_policy_v<T>> init(ArgsT... args) {
        m_open_device = [this, args...]() {
            auto result = m_handle->open(args...);
            auto generation = m_connect_generation.load();
            on_client_ready(generation, result, m_handle->get_fd());
        };
        reset_impl();
    }

    void reset_impl() {
        reset_client();
        setup_error_event_handler();
        init_device();
        m_tx_buffer = {};
        prepare_io_event_handler();
    }

    action_status send_one() {
        if (m_tx_buffer.empty()) {
            return action_status::empty;
        }
        auto& elem = m_tx_buffer.front();
        auto success = m_handle->tx(elem);
        if (success) {
            m_tx_buffer.pop();
            return action_status::success;
        }
        return action_status::fail;
    }

    action_status receive_one() {
        auto status = m_handle->rx(m_data);
        if (status and m_rx) {
            m_rx(m_data, *this);
            return action_status::success;
        }
        return action_status::fail;
    }

    action_status reset_client() {
        auto generation = ++m_connect_generation;
        set_connected_generation(generation);
        int fd = -1;
        {
            auto client_status = m_client_status.handle();
            fd = client_status->fd;
            client_status->ok = false;
            client_status->fd = -1;
        }
        unregister_source(fd);
        unregister_source(m_io_event_fd.get_raw_fd());
        {
            auto client_status = m_client_status.handle();
            client_status->ok = false;
            client_status->fd = -1;
        }
        m_handle.reset();
        return action_status::success;
    }

    void process_async_connect_results() {
        if (not m_async_connect_state) {
            return;
        }
        auto current_generation = m_connect_generation.load();
        while (auto result = m_async_connect_state->connect_results.try_pop()) {
            if (result->generation != current_generation) {
                continue;
            }
            m_handle = std::move(result->handle);
            on_client_ready(result->generation, result->ok, result->fd);
        }
    }

    void invalidate_async_connect_state() {
        ++m_connect_generation;
        if (m_async_connect_state) {
            m_async_connect_state->active.store(false, std::memory_order_release);
            m_async_connect_state->connect_results.stop();
            m_async_connect_state.reset();
        }
    }

    bool init_device() {
        m_handle = std::make_unique<ClientPolicy>();
        m_open_device();
        return true;
    }

    struct async_connect_result {
        std::uint64_t generation;
        bool ok;
        int fd;
        std::unique_ptr<ClientPolicy> handle;
    };

    struct async_connect_state {
        util::thread_safe_queue<async_connect_result> connect_results;
        event::event_fd ready_event;
        std::atomic_bool active{true};
    };

    std::unique_ptr<ClientPolicy> m_handle{nullptr};
    std::shared_ptr<async_connect_state> m_async_connect_state;
    cb_rx m_rx;
    std::function<void()> m_open_device;
    std::queue<ClientPayloadT> m_tx_buffer;
    ClientPayloadT m_data;
    std::atomic_uint64_t m_connect_generation{0};
};

/**
 * fd_event_client bootstraps the creation of of the event client by creating the interface needed
 * for \ref generic_fd_event_client and then specifying the actual client type via this template.
 * @tparam ClientPolicy Handles specific tasks on the filedescriptor, e.g. open, connect, send, receive,...
 * When ClientPolicy offers the optional functions setup/connect these are used for an async connection
 * process instead of the syncronous and potentially blocking call to open.
 * The policy is the same as for \ref generic_fd_event_client.
 * \code{.cpp}
 * class ClientPolicy{
 * public:
 *   // callback = std::function<void(bool success, int filedescriptor)>
 *   using PayloadT = [type of data]  // the type of the payload
 *   ClientPolicy();                  // must be default contructiable
 *   bool open(ArgsT... args);        // Opens the device. Parameters given by fd_event_client's constructor.
 *                                    // Return true on success, false otherwise
 *   bool setup(ArgsT... args);       // [optional] Setup the device. Parameters given by fd_event_client's constructor.
 *                                    // This function is the first step in a two part connection process.
 *                                    // It may not block for any subtantial amount of time.
 *                                    // Return true on success, false otherwise
 *   void connect(callback [const&]); // [optional] Bring the device in a ready state. This is the second step in a two
 * part
 *                                    // connection process. This part shall bundle a blocking operations and call the
 * callback
 *                                    // with the correct status when done. A filedescriptor of -1 is considered
 * invalid.
 *                                    // This function may be called from a separate thread.
 *   bool tx(PayloadT [const]& data); // transmit a dataset. Return true on success, false otherwise
 *                                    // data could be passed as non-const reference. The reference remains valid as
 * long
 *                                    // false is returned. Useful for partial writes.
 *   bool rx(PayloadT& data);         // receive a dataset. Make no assumptions about the content of data
 *                                    // It may contain any data. rx is responsible to bring data a consistent state.
 *                                    // Return true on success, false otherwise
 *   int get_fd();                    // returns the file discriptor to be monitored
 *   int get_error();                 // return the current error, 0 with no error.
 * };
 * // ClientPolicy owns the file descriptor
 * \endcode
 * \details A specific client is created this way
 * \code{.cpp}
 * using specific_client = event::fd_event_client<specific_policy>::type;
 *
 * \endcode
 */
template <class ClientPolicy> class fd_event_client {
public:
    /**
     * @var payload
     * @brief The type of the payload is infered from the policy \p ClientPolicy
     */
    using payload = typename ClientPolicy::PayloadT;

    /**
     * This is the interface to the client as seen in the RX callback.
     */
    class interface {
    public:
        /**
         * @var cb_rx
         * @brief Prototype for the callback function to be registered for RX with the client
         */
        using cb_rx = std::function<void(payload const& payload, interface& device)>;
        virtual ~interface() = default;
        /**
         * @brief Interface function for TX.
         * @details Implemented by generic_fd_event_client
         */
        virtual bool tx(payload const& payload) = 0;
    };

    /**
     * @var type
     * @brief Type of the specific client
     */
    using type = generic_fd_event_client<ClientPolicy, interface>;

    /**
     * @var callback_rx
     * @brief Type of the RX callback
     */
    using callback_rx = typename interface::cb_rx;

    /**
     * @var callback_error
     * @brief Type of the error callback
     */
    using callback_error = typename type::cb_error;
};

} // namespace everest::lib::io::event
