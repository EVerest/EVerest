// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2021 Pionix GmbH and Contributors to EVerest
#ifndef EVEREST_TIMER_HPP
#define EVEREST_TIMER_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>
#include <date/date.h>
#include <date/tz.h>

namespace Everest {
template <typename TimerClock = date::utc_clock> class Timer {
private:
    std::unique_ptr<boost::asio::basic_waitable_timer<TimerClock>> timer = nullptr;
    std::function<void()> timer_callback;
    std::function<void(const boost::system::error_code& error)> callback_wrapper;
    std::chrono::nanoseconds interval_nanoseconds = std::chrono::nanoseconds(0);
    std::atomic<bool> running = false;

    boost::asio::io_context io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work;
    std::unique_ptr<std::thread> timer_thread = nullptr;

    std::mutex mutex;

public:
    /// This timer will initialize a boost::asio::io_context
    Timer() :
        work(boost::asio::make_work_guard(this->io_context)),
        timer_thread(std::make_unique<std::thread>([this]() { this->io_context.run(); })) {
        this->timer = std::make_unique<boost::asio::basic_waitable_timer<TimerClock>>(this->io_context);
    }

    explicit Timer(const std::function<void()>& callback) :
        timer_callback(callback),
        work(boost::asio::make_work_guard(this->io_context)),
        timer_thread(std::make_unique<std::thread>([this]() { this->io_context.run(); })) {
        this->timer = std::make_unique<boost::asio::basic_waitable_timer<TimerClock>>(this->io_context);
    }

    explicit Timer(boost::asio::io_context* io_context) :
        timer(std::make_unique<boost::asio::basic_waitable_timer<TimerClock>>(*io_context)),
        work(boost::asio::make_work_guard(*io_context)) {
    }

    Timer(boost::asio::io_context* io_context, const std::function<void()>& callback) :
        timer(std::make_unique<boost::asio::basic_waitable_timer<TimerClock>>(*io_context)),
        timer_callback(callback),
        work(boost::asio::make_work_guard(*io_context)) {
    }

    ~Timer() {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->timer) {
            // stop asio timer
            this->timer->cancel();

            if (this->timer_thread) {
                this->io_context.stop();
                this->timer_thread->join();
            }
        }
    }

    /// Executes the given callback at the given timepoint
    template <class Clock, class Duration = typename Clock::duration>
    void at(const std::function<void()>& callback, const std::chrono::time_point<Clock, Duration>& time_point) {
        std::lock_guard<std::mutex> lock(this->mutex);

        this->stop_internal();
        this->timer_callback = callback;

        this->at_internal(time_point);
    }

    /// Executes a previously configured callback at the given timepoint
    template <class Clock, class Duration = typename Clock::duration>
    void at(const std::chrono::time_point<Clock, Duration>& time_point) {
        std::lock_guard<std::mutex> lock(this->mutex);

        this->stop_internal();
        this->at_internal<Clock, Duration>(time_point);
    }

    /// Executes the given callback periodically from now in the given interval
    template <class Rep, class Period>
    void interval(const std::function<void()>& callback, const std::chrono::duration<Rep, Period>& interval) {
        std::lock_guard<std::mutex> lock(this->mutex);

        this->stop_internal();
        this->timer_callback = callback;

        this->interval_internal(interval);
    }

    /// Executes a previously configured callback periodically from now in the given interval
    template <class Rep, class Period> void interval(const std::chrono::duration<Rep, Period>& interval) {
        std::lock_guard<std::mutex> lock(this->mutex);

        this->stop_internal();
        this->interval_internal(interval);
    }

    /// Executes the given callback once after the given interval
    template <class Rep, class Period>
    void timeout(const std::function<void()>& callback, const std::chrono::duration<Rep, Period>& interval) {
        std::lock_guard<std::mutex> lock(this->mutex);

        this->stop_internal();
        this->timer_callback = callback;

        this->timeout_internal(interval);
    }

    /// Executes a previously configured callback once after the given interval
    template <class Rep, class Period> void timeout(const std::chrono::duration<Rep, Period>& interval) {
        std::lock_guard<std::mutex> lock(this->mutex);

        this->stop_internal();
        this->timeout_internal(interval);
    }

    /// Stops timer from executing its callback
    void stop() {
        std::lock_guard<std::mutex> lock(this->mutex);

        stop_internal();
    }

    /// Indicates if the timer is running
    bool is_running() {
        std::lock_guard<std::mutex> lock(this->mutex);

        return running;
    }

private:
    template <class Clock, class Duration = typename Clock::duration>
    void at_internal(const std::chrono::time_point<Clock, Duration>& time_point) {
        if (this->timer_callback == nullptr) {
            return;
        }

        if (this->timer) {
            running = true;

            // use asio timer
            this->timer->expires_at(time_point);
            this->timer->async_wait([this](const boost::system::error_code& e) {
                if (e) {
                    return;
                }

                this->timer_callback();
                running = false;
            });
        }
    }

    template <class Rep, class Period> void interval_internal(const std::chrono::duration<Rep, Period>& interval) {
        this->interval_nanoseconds = interval;
        if (interval_nanoseconds == std::chrono::nanoseconds(0)) {
            return;
        }

        if (this->timer_callback == nullptr) {
            return;
        }

        if (this->timer) {
            running = true;

            // use asio timer
            this->callback_wrapper = [this](const boost::system::error_code& error) {
                if (error) {
                    running = false;
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(this->mutex);
                    this->timer->expires_after(this->interval_nanoseconds);
                    this->timer->async_wait(this->callback_wrapper);
                }

                this->timer_callback();
            };

            this->timer->expires_after(this->interval_nanoseconds);
            this->timer->async_wait(this->callback_wrapper);
        }
    }

    template <class Rep, class Period> void timeout_internal(const std::chrono::duration<Rep, Period>& interval) {
        if (this->timer_callback == nullptr) {
            return;
        }

        if (this->timer) {
            running = true;

            // use asio timer
            this->timer->expires_after(interval);
            this->timer->async_wait([this](const boost::system::error_code& error) {
                if (error) {
                    running = false;
                    return;
                }

                this->timer_callback();
                running = false;
            });
        }
    }

    void stop_internal() {
        if (this->timer) {
            // asio based timer
            this->timer->cancel();
        }

        running = false;
    }
};

using SteadyTimer = Timer<date::utc_clock>;
using SystemTimer = Timer<date::utc_clock>;
} // namespace Everest

#endif // EVEREST_TIMER_HPP
