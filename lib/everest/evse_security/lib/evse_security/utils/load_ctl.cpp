// SPDX-License-Identifier: Apache-2.0
#include <evse_security/utils/load_ctl.hpp>

#include <cctype>

namespace ctl {

std::optional<GeneralizedTime> GeneralizedTime::parse(std::string_view s) {
    if (s.size() != 15 || s.back() != 'Z') {
        return std::nullopt;
    }
    for (char c : s.substr(0, 14)) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
    }
    return GeneralizedTime{std::string{s}};
}

ValidationError validate(const TrustList& ctl) noexcept {
    if (ctl.ctl_version > 255)       return ValidationError::VersionOutOfRange;
    if (ctl.sequence_number > 65535) return ValidationError::SequenceOutOfRange;
    if (!GeneralizedTime::parse(ctl.not_before.value)) return ValidationError::BadNotBefore;
    if (!GeneralizedTime::parse(ctl.not_after.value))  return ValidationError::BadNotAfter;
    if (!(ctl.not_before < ctl.not_after))             return ValidationError::ValidityInverted;

    for (const auto& r : ctl.roots) {
        switch (r.status) {
        case Status::Active:
        case Status::Warning:
        case Status::Deprecated: break;
        default: return ValidationError::BadStatus;
        }
    }
    return ValidationError::None;
}

bool is_newer(std::uint16_t previous, std::uint16_t candidate) noexcept {
    return candidate > previous;
}

const char* to_string(ValidationError e) noexcept {
    switch (e) {
        case ValidationError::None:              return "ok";
        case ValidationError::VersionOutOfRange: return "ctlVersion out of range (0..255)";
        case ValidationError::SequenceOutOfRange:return "sequenceNumber out of range (0..65535)";
        case ValidationError::BadNotBefore:      return "notBefore is not YYYYMMDDHHMMSSZ";
        case ValidationError::BadNotAfter:       return "notAfter is not YYYYMMDDHHMMSSZ";
        case ValidationError::ValidityInverted:  return "notBefore >= notAfter";
        case ValidationError::BadStatus:         return "unknown ctlStatus";
    }
    return "unknown";
}

} // namespace ctl