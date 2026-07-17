// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/certificate_installation.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/d2/state/payment_details.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {

constexpr char BASE64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                                static_cast<uint32_t>(data[i + 2]);
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 6) & 0x3f]);
        out.push_back(BASE64_ALPHABET[triple & 0x3f]);
    }
    const size_t remaining = data.size() - i;
    if (remaining == 1) {
        const uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

std::vector<uint8_t> base64_decode(const std::string& in) {
    std::array<int8_t, 256> lut{};
    lut.fill(-1);
    for (int i = 0; i < 64; ++i) {
        lut[static_cast<uint8_t>(BASE64_ALPHABET[i])] = static_cast<int8_t>(i);
    }

    std::vector<uint8_t> out;
    out.reserve((in.size() / 4) * 3);
    uint32_t buffer = 0;
    int bits = 0;
    for (const char c : in) {
        if (c == '=' or c == '\n' or c == '\r' or c == ' ') {
            continue;
        }
        const int8_t value = lut[static_cast<uint8_t>(c)];
        if (value < 0) {
            // Invalid character -> reject the whole payload.
            return {};
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return out;
}

} // namespace

void CertificateInstallation::enter() {
    m_ctx.log.enter_state("CertificateInstallation");
}

Result CertificateInstallation::feed(Event ev) {
    // Response injected by the module (CSMS/CPS backend): splice the raw CertificateInstallationRes EXI
    // onto the wire verbatim and continue to PaymentDetails.
    if (ev == Event::CONTROL_MESSAGE) {
        const auto* response = m_ctx.get_control_event<d20::CertificateResponse>();
        if (response == nullptr) {
            return {};
        }
        if (not response->status_accepted or response->exi_response_base64.empty()) {
            m_ctx.log("CertificateInstallation: backend reported failure; terminating session");
            m_ctx.session_stopped = true;
            return {};
        }
        const auto raw = base64_decode(response->exi_response_base64);
        if (raw.empty()) {
            m_ctx.log("CertificateInstallation: failed to base64-decode the backend response");
            m_ctx.session_stopped = true;
            return {};
        }
        m_ctx.respond_raw(raw);
        return m_ctx.create_state<PaymentDetails>();
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // Forward the raw CertificateInstallationReq EXI to the module; the response arrives asynchronously
    // as a CertificateResponse control event handled above. Guard against a re-feed re-forwarding it.
    if (request_forwarded) {
        return {};
    }

    const auto variant = m_ctx.pull_request();
    // This relay handles both PnC certificate exchanges (identical raw-EXI pass-through); the action
    // tells the backend which one it is.
    const auto type = variant->get_type();
    if (type != message_2::Type::CertificateInstallationReq and type != message_2::Type::CertificateUpdateReq) {
        m_ctx.log("CertificateInstallation: expected CertificateInstallationReq/CertificateUpdateReq, "
                  "got type id: %d",
                  type);
        m_ctx.session_stopped = true;
        return {};
    }
    const auto action = (type == message_2::Type::CertificateUpdateReq)
                            ? session::feedback::CertificateExchangeAction::Update
                            : session::feedback::CertificateExchangeAction::Install;

    const auto& exi = variant->get_exi_payload();
    if (exi.empty()) {
        m_ctx.log("CertificateInstallation: empty request EXI payload; terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    request_forwarded = true;
    m_ctx.feedback.certificate_request(base64_encode(exi), action);

    // Park: no response is staged until the module injects the CertificateInstallationRes.
    return {};
}

} // namespace iso15118::d2::state
