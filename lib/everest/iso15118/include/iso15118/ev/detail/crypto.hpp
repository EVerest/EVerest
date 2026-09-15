// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <openssl/x509.h>

// X.509 checks the EVCC makes that libtls does not offer.
namespace iso15118::ev::crypto {

// [V2G2-875]: the SECC leaf identifies a charge point operator via a DomainComponent=="CPO" RDN.
bool has_cpo_domain_component(const X509* cert);

} // namespace iso15118::ev::crypto
