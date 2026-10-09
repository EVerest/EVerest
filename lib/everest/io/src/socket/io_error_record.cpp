// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/io/socket/io_error_record.hpp>
#include <everest/io/socket/socket.hpp>

namespace everest::lib::io::socket {

void io_error_record::note(int error) {
    m_code = send_failure_code(error);
}

void io_error_record::clear() {
    m_code = 0;
}

int io_error_record::report(int fd) const {
    auto const pending = get_pending_error(fd);
    return pending != 0 ? pending : m_code;
}

} // namespace everest::lib::io::socket
