// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_MESSAGE_QUEUE_HPP
#define UTILS_MESSAGE_QUEUE_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <everest/io/event/event_fd.hpp>
#include <everest/util/async/monitor.hpp>
#include <everest/util/queue/simple_queue.hpp>
#include <nlohmann/json.hpp>

#include <utils/types.hpp>

namespace everest::lib::io::event {
class fd_event_handler;
}

namespace Everest {
/// \brief Contains a payload and the topic it was received on
struct Message {
    std::string topic;   ///< The topic where this message originated from
    std::string payload; ///< The message payload

    Message(const std::string& topic_, const std::string& payload_) : topic(topic_), payload(payload_) {
    }
};

struct ParsedMessage {
    std::string topic;
    json data;
};

/// \brief Threadless FIFO queue for transport callback messages.
///
/// Transport callbacks enqueue raw Message objects here. The queue wakes an owning fd_event_handler via event_fd and
/// only dispatches messages when the owner explicitly calls drain(). It does not own a worker thread; callbacks run on
/// the thread that drains the queue, normally the transport event-loop thread. Transport implementations are expected
/// to hand parsed messages to MessageHandler so framework handler execution remains delegated there.
class TransportCallbackQueue {
public:
    using DrainCallback = std::function<void(const Message&)>;

    TransportCallbackQueue();
    ~TransportCallbackQueue() = default;

    TransportCallbackQueue(const TransportCallbackQueue&) = delete;
    TransportCallbackQueue& operator=(const TransportCallbackQueue&) = delete;

    /// \brief Enqueue \p message.
    /// \return false when the queue has been closed.
    bool enqueue(Message message);

    /// \brief Compatibility helper for transport callbacks that currently allocate Message objects.
    bool enqueue(std::unique_ptr<Message> message);

    /// \brief Drain queued messages in FIFO order on the calling thread.
    /// \return number of messages delivered to \p callback.
    std::size_t drain(const DrainCallback& callback);

    /// \brief Stop accepting new messages and wake any event loop that is polling the queue fd.
    void close();

    /// \brief Returns true after close() has been called.
    bool closed() const;

    /// \brief Queue wake event. Suitable for fd_event_handler registration.
    everest::lib::io::event::event_fd_base* event_fd();
    const everest::lib::io::event::event_fd_base* event_fd() const;
    int event_fd_raw() const;

    /// \brief Register the queue wake fd and drain messages with \p callback when readable.
    bool register_events(everest::lib::io::event::fd_event_handler& handler, const DrainCallback& callback);

    /// \brief Unregister the queue wake fd from \p handler.
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler);

private:
    struct State {
        everest::lib::util::simple_queue<Message> queue;
        bool closed{false};
        bool draining{false};
    };

    mutable everest::lib::util::monitor<State> state;
    everest::lib::io::event::event_fd_base wake_event;
};

} // namespace Everest

#endif // UTILS_MESSAGE_QUEUE_HPP
