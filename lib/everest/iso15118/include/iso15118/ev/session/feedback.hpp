// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <variant>

#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/ev/d20/session.hpp>
#include <iso15118/message/type.hpp>

namespace iso15118::ev::d20::session {

namespace dt = message_20::datatypes;

namespace feedback {

struct Callbacks {
    std::function<void(const EVSESessionInfo&)> evse_session_info;
};

} // namespace feedback

class Feedback {
public:
    Feedback(feedback::Callbacks);

    void evse_session_info(const EVSESessionInfo&) const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::ev::d20::session
