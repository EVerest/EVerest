// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <everest/io/utilities/generic_error_state.hpp>

#include <cerrno>
#include <string.h>

namespace everest::lib::io::utilities {

bool generic_error_state::set_error_status(int error_code) {
    m_current_error = error_code;
    auto const failed = error_code != 0;
    // The up-edge, so it reads the previous state and has to be evaluated before the state is
    // updated. on_error() covers fresh as well as failed, which is what makes a first connect
    // report code 0 even though no error was ever seen.
    m_clear_error_pending = (not failed) and on_error();
    m_connection_state = failed ? connection_state::failed : connection_state::connected;
    return not failed;
}

bool generic_error_state::clear_error_pending() const {
    return m_clear_error_pending;
}

bool generic_error_state::on_error() const {
    return m_connection_state != connection_state::connected;
}

connection_state generic_error_state::current_connection_state() const {
    return m_connection_state;
}

void generic_error_state::reset_connection_state() {
    m_connection_state = connection_state::fresh;
}

int generic_error_state::current_error() const {
    return m_current_error;
}

void generic_error_state::call_error_handler(cb_error& handler, std::string const& msg) const {
    if (handler) {
        // The callback must never see 0: consumers branch on 'code != 0', so a 0 report would be
        // dropped while the state stays failed. Unreachable today, kept as a backstop.
        auto error = m_current_error != 0 ? m_current_error : ECONNRESET;
        handler(error, msg.empty() ? std::string{strerror(error)} : msg);
    }
}

void generic_error_state::clear_error_handler(cb_error& handler) {
    if (handler) {
        handler(0, strerror(0));
    }
    set_error_cleared();
}

void generic_error_state::set_error_cleared() {
    m_clear_error_pending = false;
}

} // namespace everest::lib::io::utilities
