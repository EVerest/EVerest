// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// Plug & Charge provisioning: signs a CertificateInstallationReq with the OEM provisioning key, verifies
// the CPS signature on the response, decrypts the delivered contract private key (ISO 15118-2 7.9.2.4.3)
// and installs the contract certificate for the following PaymentDetails exchange.
struct CertificateInstallation : public StateBase {
    CertificateInstallation(Context& ctx) : StateBase(ctx, StateID::CertificateInstallation) {
    }

    void enter() final;
    Result feed(Event) final;
};

// Advertises the first certificate of the configured V2G root, signs the CertificateInstallationReq
// with the OEM provisioning key, sends it and creates the state awaiting it. An unreadable root, one
// without a usable certificate ID, or a failed signature stops the session with nothing sent.
Result to_certificate_installation(Context& ctx);

} // namespace iso15118::ev::d2::state
