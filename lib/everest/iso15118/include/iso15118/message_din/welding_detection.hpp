// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include "common_types.hpp"

namespace iso15118::message_din {

struct WeldingDetectionRequest {
    Header header;
    datatypes::DcEvStatus dc_ev_status;
};

struct WeldingDetectionResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::DcEvseStatus dc_evse_status;
    double evse_present_voltage{0};
};

} // namespace iso15118::message_din
