// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#ifndef CAN_BUS_HPP
#define CAN_BUS_HPP

#include <atomic>
#include <condition_variable>
#include <deque>
#include <linux/can.h>
#include <mutex>
#include <thread>
#include <vector>

#include <everest/io/can/socket_can.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib::io;

class CanBus {
public:
    CanBus();
    virtual ~CanBus();
    bool open_device(const std::string& dev);
    bool close_device();

protected:
    virtual void rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload) = 0;
    virtual void poll_status_handler() = 0;
    bool _tx(uint32_t can_id, const std::vector<uint8_t>& payload);

    /**
     * @brief Queue a frame for paced transmission on the CAN event thread.
     * @details Call \ref start_paced_tx_cycle after enqueueing a poll batch.
     */
    void enqueue_paced_tx(uint32_t can_id, std::vector<uint8_t> payload);

    /** @brief Discard pending paced frames and disarm the pace timer. */
    void clear_paced_tx_queue();

    /**
     * @brief Send the first queued frame immediately and schedule the rest at the pace interval.
     */
    void start_paced_tx_cycle();

private:
    struct paced_tx_frame {
        uint32_t can_id{0};
        std::vector<uint8_t> payload;
    };

    void pace_tx_handler();
    bool arm_pace_tx_one_shot();
    void disarm_pace_tx_timer();

    std::unique_ptr<can::socket_can> can_bus;
    std::atomic_bool on_error{false};
    event::fd_event_handler ev_handler;
    event::timer_fd recovery_timer;
    event::timer_fd poll_status_timer;
    event::timer_fd pace_tx_timer;
    std::deque<paced_tx_frame> m_pace_tx_queue;
    std::atomic_bool rx_thread_online;
    std::thread rx_thread_handle;
    void rx_thread();
};

#endif // CAN_BUS_HPP
