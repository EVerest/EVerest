// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_2 {

struct PaymentDetailsRequest {
    Header header;
    std::string emaid;
    // ContractSignatureCertChain: the contract leaf certificate (DER) and the ordered SubCertificates
    // (leaf-nearest first), both in raw DER form.
    std::vector<uint8_t> contract_certificate;
    std::vector<std::vector<uint8_t>> sub_certificates;
};

struct PaymentDetailsResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::GenChallenge gen_challenge{};
    int64_t evse_timestamp{0};
};

} // namespace iso15118::message_2
