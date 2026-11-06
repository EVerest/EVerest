// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <iso15118/message_2/certificate_installation.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace certificate_installation {

// Unsigned: the EV signs it with the OEM provisioning key before sending.
message_2::CertificateInstallationRequest create_request(const dt::SessionId& session_id,
                                                         const std::vector<uint8_t>& oem_prov_cert_der,
                                                         const message_2::RootCertificateId& root_id);

} // namespace certificate_installation

} // namespace iso15118::ev::d2::state
