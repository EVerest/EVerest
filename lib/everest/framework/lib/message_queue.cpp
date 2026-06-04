// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <sys/eventfd.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <optional>
#include <utility>

#include <utils/message_queue.hpp>

namespace Everest {

TransportCallbackQueue::TransportCallbackQueue() : wake_event(0, EFD_NONBLOCK) {
}

bool TransportCallbackQueue::enqueue(Message message) {
    auto notify = false;
    {
        auto handle = this->state.handle();
        if (handle->closed) {
            return false;
        }

        notify = handle->queue.empty() && !handle->draining;
        handle->queue.push(std::move(message));
    }

    return !notify || this->wake_event.notify();
}

bool TransportCallbackQueue::enqueue(std::unique_ptr<Message> message) {
    if (message == nullptr) {
        return false;
    }
    return this->enqueue(std::move(*message));
}

std::size_t TransportCallbackQueue::drain(const DrainCallback& callback) {
    if (!callback) {
        return 0;
    }

    this->wake_event.read();

    {
        auto handle = this->state.handle();
        if (handle->draining) {
            return 0;
        }
        handle->draining = true;
    }

    struct DrainGuard {
        TransportCallbackQueue& queue;
        ~DrainGuard() {
            auto notify = false;
            {
                auto handle = queue.state.handle();
                handle->draining = false;
                notify = !handle->queue.empty();
            }

            if (notify) {
                queue.wake_event.notify();
            }
        }
    } drain_guard{*this};

    std::size_t drained = 0;
    while (true) {
        auto next_message = std::optional<Message>{};
        {
            auto handle = this->state.handle();
            next_message = handle->queue.pop();
            if (!next_message) {
                break;
            }
        }

        callback(*next_message);
        ++drained;
    }

    return drained;
}

void TransportCallbackQueue::close() {
    {
        auto handle = this->state.handle();
        if (handle->closed) {
            return;
        }
        handle->closed = true;
    }

    this->wake_event.notify();
}

bool TransportCallbackQueue::closed() const {
    auto handle = this->state.handle();
    return handle->closed;
}

everest::lib::io::event::event_fd_base* TransportCallbackQueue::event_fd() {
    return &this->wake_event;
}

const everest::lib::io::event::event_fd_base* TransportCallbackQueue::event_fd() const {
    return &this->wake_event;
}

int TransportCallbackQueue::event_fd_raw() const {
    return this->wake_event.get_raw_fd();
}

bool TransportCallbackQueue::register_events(everest::lib::io::event::fd_event_handler& handler,
                                             const DrainCallback& callback) {
    return handler.register_event_handler(&this->wake_event, [this, callback]() { this->drain(callback); });
}

bool TransportCallbackQueue::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    return handler.unregister_event_handler(&this->wake_event);
}

} // namespace Everest
