// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include "common_types.hpp"

namespace iso15118::message_din {

struct CableCheckRequest {
    Header header;
    datatypes::DcEvStatus dc_ev_status;
};

struct CableCheckResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::DcEvseStatus dc_evse_status;
    datatypes::EvseProcessing evse_processing{datatypes::EvseProcessing::Ongoing};
};

} // namespace iso15118::message_din
