// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <ctime>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

namespace dt = message_20::datatypes;

static inline void setup_timestamp(message_20::Header& header) {
    header.timestamp = static_cast<uint64_t>(std::time(nullptr));
}

void setup_header(message_20::Header& header, const SessionId& cur_session) {
    header.session_id = cur_session.get_id();
    setup_timestamp(header);
}

bool check_response_code(dt::ResponseCode response_code) {
    switch (response_code) {
    case dt::ResponseCode::OK:
    case dt::ResponseCode::OK_CertificateExpiresSoon:
    case dt::ResponseCode::OK_NewSessionEstablished:
    case dt::ResponseCode::OK_OldSessionJoined:
    case dt::ResponseCode::OK_PowerToleranceConfirmed:
    case dt::ResponseCode::WARNING_AuthorizationSelectionInvalid:
    case dt::ResponseCode::WARNING_CertificateExpired:
    case dt::ResponseCode::WARNING_CertificateNotYetValid:
    case dt::ResponseCode::WARNING_CertificateRevoked:
    case dt::ResponseCode::WARNING_CertificateValidationError:
    case dt::ResponseCode::WARNING_ChallengeInvalid:
    case dt::ResponseCode::WARNING_EIMAuthorizationFailure:
    case dt::ResponseCode::WARNING_eMSPUnknown:
    case dt::ResponseCode::WARNING_EVPowerProfileViolation:
    case dt::ResponseCode::WARNING_GeneralPnCAuthorizationError:
    case dt::ResponseCode::WARNING_NoCertificateAvailable:
    case dt::ResponseCode::WARNING_NoContractMatchingPCIDFound:
    case dt::ResponseCode::WARNING_PowerToleranceNotConfirmed:
    case dt::ResponseCode::WARNING_ScheduleRenegotiationFailed:
    case dt::ResponseCode::WARNING_StandbyNotAllowed:
    case dt::ResponseCode::WARNING_WPT:
        return true;
    case dt::ResponseCode::FAILED:
    case dt::ResponseCode::FAILED_AssociationError:
    case dt::ResponseCode::FAILED_ContactorError:
    case dt::ResponseCode::FAILED_EVPowerProfileInvalid:
    case dt::ResponseCode::FAILED_EVPowerProfileViolation:
    case dt::ResponseCode::FAILED_MeteringSignatureNotValid:
    case dt::ResponseCode::FAILED_NoEnergyTransferServiceSelected:
    case dt::ResponseCode::FAILED_NoServiceRenegotiationSupported:
    case dt::ResponseCode::FAILED_PauseNotAllowed:
    case dt::ResponseCode::FAILED_PowerDeliveryNotApplied:
    case dt::ResponseCode::FAILED_PowerToleranceNotConfirmed:
    case dt::ResponseCode::FAILED_ScheduleRenegotiation:
    case dt::ResponseCode::FAILED_ScheduleSelectionInvalid:
    case dt::ResponseCode::FAILED_SequenceError:
    case dt::ResponseCode::FAILED_ServiceIDInvalid:
    case dt::ResponseCode::FAILED_ServiceSelectionInvalid:
    case dt::ResponseCode::FAILED_SignatureError:
    case dt::ResponseCode::FAILED_UnknownSession:
    case dt::ResponseCode::FAILED_WrongChargeParameter:
        return false;
    }
    // Out-of-range decode (the enum is a plain integer on the wire).
    return false;
}

} // namespace iso15118::ev::d20
