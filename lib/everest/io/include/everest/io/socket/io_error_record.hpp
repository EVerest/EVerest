// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

namespace everest::lib::io::socket {

/**
 * @brief The cause a socket policy reports for its connection.
 * @details The pending socket error (SO_ERROR) first, otherwise the errno of the last read or
 *          write that failed with one. Back-pressure and a spurious wake (see
 *          \ref is_send_backpressure) are not causes. The record describes the last operation
 *          only: a read or write that goes through clears it, a read that fails replaces what a
 *          send left, and a policy clears it wherever it takes or drops a descriptor, so a socket
 *          that owns a descriptor never carries a stale reason.
 */
class io_error_record {
public:
    /**
     * @brief Note the outcome of a read or write.
     * @param[in] error 0 when it went through, otherwise its errno
     */
    void note(int error);

    /**
     * @brief Forget the record: a descriptor was taken or dropped.
     */
    void clear();

    /**
     * @brief The cause to report for \p fd.
     * @return The pending error of \p fd, otherwise the recorded one
     */
    int report(int fd) const;

private:
    int m_code{0};
};

} // namespace everest::lib::io::socket
