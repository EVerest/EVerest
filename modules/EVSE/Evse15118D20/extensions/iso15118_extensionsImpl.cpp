// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "iso15118_extensionsImpl.hpp"

namespace module {
namespace extensions {

void iso15118_extensionsImpl::init() {
}

void iso15118_extensionsImpl::ready() {
}

void iso15118_extensionsImpl::handle_set_get_certificate_response(
    types::iso15118::ResponseExiStreamStatus& certificate_response) {
    // ISO 15118-2 Plug-and-Charge CertificateInstallation relay: hand the backend's
    // CertificateInstallationRes to the charger impl, which injects it into libiso15118 (the SECC only
    // relays the raw EXI stream; it does not decode it). Handler registered by the charger impl once its
    // libiso15118 controller exists.
    if (mod->on_certificate_response) {
        mod->on_certificate_response(certificate_response);
    } else {
        EVLOG_warning << "Received set_get_certificate_response but no CertificateInstallation relay is "
                         "armed (no active ISO 15118-2 PnC session)";
    }
}

} // namespace extensions
} // namespace module
