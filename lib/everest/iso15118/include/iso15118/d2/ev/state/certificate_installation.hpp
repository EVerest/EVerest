// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// Plug-and-Charge provisioning: builds a CertificateInstallationReq signed with the OEM provisioning key,
// verifies the CertificateInstallationRes CPS signature, decrypts the delivered contract private key
// (ECDH/ConcatKDF/AES per ISO 15118-2 7.9.2.4.3) and installs the contract certificate for the following
// PaymentDetails exchange.
struct CertificateInstallation : public StateBase {
    CertificateInstallation(Context& ctx) : StateBase(ctx, StateID::CertificateInstallation) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::ev::state
