// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

// ISO 15118-2 Plug-and-Charge CertificateInstallation RELAY. The SECC does not decode or verify the
// message: on the CertificateInstallationReq it forwards the raw request EXI (base64) to the module via
// the certificate_request feedback (which publishes it on the iso15118_extensions interface for the
// CSMS/CPS backend), then parks. When the backend answers, the module injects the raw
// CertificateInstallationRes EXI back as a CertificateResponse control event; this state splices those
// bytes onto the wire verbatim and continues to PaymentDetails. Mirrors EvseV2G's
// handle_iso_certificate_installation. Reached from PaymentDetails when a CertificateInstallationReq
// arrives after PaymentServiceSelection(Contract).
struct CertificateInstallation : public StateBase {
    CertificateInstallation(Context& ctx) : StateBase(ctx, StateID::CertificateInstallation) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool request_forwarded{false};
};

} // namespace iso15118::d2::state
