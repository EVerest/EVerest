// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/detail/session/session.hpp>

namespace iso15118::ev {

void Controller::SessionDeleter::operator()(session::Session* session) const noexcept {
    delete session;
}

} // namespace iso15118::ev
