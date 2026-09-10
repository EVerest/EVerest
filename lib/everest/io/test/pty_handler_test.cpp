// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// pty_handler as fd_event_client policy: a false from tx()/rx() that means retry must leave
// get_error() at zero.

#include <everest/io/serial/pty_handler.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using everest::lib::io::serial::pty_handler;

namespace {

// Nobody reads the slave, so writes stall: partial write or EAGAIN, both return false.
bool write_until_would_block(pty_handler& handler, std::size_t& attempts) {
    for (attempts = 0; attempts < 1000; ++attempts) {
        pty_handler::PayloadT payload(1400, 0x55);
        if (not handler.tx(payload)) {
            return not payload.empty();
        }
    }
    return false;
}

} // namespace

TEST(pty_handler_test, a_write_the_slave_has_not_drained_is_a_retry_not_an_error) {
    pty_handler handler;
    ASSERT_TRUE(handler.open());
    ASSERT_EQ(handler.get_error(), 0);

    std::size_t attempts = 0;
    ASSERT_TRUE(write_until_would_block(handler, attempts)) << "the master never reported a would-block";
    EXPECT_GT(attempts, 0U);
    EXPECT_EQ(handler.get_error(), 0);

    // Retry stalls again, still no error.
    pty_handler::PayloadT payload(1400, 0x55);
    EXPECT_FALSE(handler.tx(payload));
    EXPECT_FALSE(payload.empty());
    EXPECT_EQ(handler.get_error(), 0);
}

TEST(pty_handler_test, a_read_with_nothing_pending_is_a_retry_not_an_error) {
    pty_handler handler;
    ASSERT_TRUE(handler.open());

    // Drain the status packets queued by open() until the master would block.
    pty_handler::PayloadT data;
    int reads = 0;
    while (handler.rx(data) and reads < 16) {
        ++reads;
    }
    ASSERT_LT(reads, 16);
    EXPECT_EQ(handler.get_error(), 0);
}

TEST(pty_handler_test, a_status_query_leaves_the_error_alone) {
    pty_handler handler;
    ASSERT_TRUE(handler.open());
    (void)handler.get_status();
    EXPECT_EQ(handler.get_error(), 0);
}
