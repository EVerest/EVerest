// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include "common_types.hpp"

namespace iso15118::message_2 {

struct CableCheckRequest {
    Header header;
    datatypes::DC_EVStatus dc_ev_status;
};

struct CableCheckResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::DC_EVSEStatus dc_evse_status;
    datatypes::EVSEProcessing evse_processing;
};

} // namespace iso15118::message_2
