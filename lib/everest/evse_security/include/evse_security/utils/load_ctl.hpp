// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <evse_security/certificate/x509_wrapper.hpp>
#include <evse_security/evse_types.hpp>

namespace ctl {

using evse_security::X509Wrapper;
using evse_security::CaCertificateType;

enum class Status : long {
    Active     = 0,
    Warning    = 1,
    Deprecated = 2,
};

struct GeneralizedTime {
    std::string value;   // always "YYYYMMDDHHMMSSZ", 15 chars

    static std::optional<GeneralizedTime> parse(std::string_view s);
    bool operator<(const GeneralizedTime& other) const noexcept { return value < other.value; }
};

struct RootCertificate {
    CaCertificateType type;
    Status            status;
    X509Wrapper       cert;
};

struct TrustList {
    std::uint8_t    ctl_version     = 0;
    std::uint16_t   sequence_number = 0;
    GeneralizedTime not_before;
    GeneralizedTime not_after;
    std::vector<RootCertificate> roots;
};

enum class ValidationError {
    None = 0,
    VersionOutOfRange,
    SequenceOutOfRange,
    BadNotBefore,
    BadNotAfter,
    ValidityInverted,
    BadStatus,
};

const char* to_string(ValidationError e) noexcept;
ValidationError validate(const TrustList& ctl) noexcept;
bool is_newer(std::uint16_t previous, std::uint16_t candidate) noexcept;

} // namespace ctl