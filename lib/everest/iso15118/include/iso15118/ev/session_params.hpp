// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

namespace iso15118::ev {

// What an engine needs to know about the session it is running in, beyond the protocol itself.
struct EvSessionParams {
    // True when the data path is TLS. The Controller writes it once the SDP response has named the
    // transport security, which is after the Session exists and before any engine reads it.
    bool secure_transport{false};
};

} // namespace iso15118::ev
