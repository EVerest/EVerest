// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <ctime>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/session.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

namespace dt = message_20::datatypes;

static inline void setup_timestamp(message_20::Header& header) {
    header.timestamp = static_cast<uint64_t>(std::time(nullptr));
}

// TODO(SL): Is this necessary for the ev?
bool validate_and_setup_header(message_20::Header& header, const Session& cur_session,
                               const decltype(message_20::Header::session_id)& res_session_id) {

    setup_header(header, cur_session);
    return (cur_session.get_id() == res_session_id);
}

void setup_header(message_20::Header& header, const Session& cur_session) {
    header.session_id = cur_session.get_id();
    setup_timestamp(header);
}

// FIXME(SL): Refactor later
bool check_response_code(dt::ResponseCode response_code) {
    switch (response_code) {
    case dt::ResponseCode::OK:
        [[fallthrough]];
    case dt::ResponseCode::OK_CertificateExpiresSoon:
        [[fallthrough]];
    case dt::ResponseCode::OK_NewSessionEstablished:
        [[fallthrough]];
    case dt::ResponseCode::OK_OldSessionJoined:
        [[fallthrough]];
    case dt::ResponseCode::OK_PowerToleranceConfirmed:
        return true;
    case dt::ResponseCode::WARNING_AuthorizationSelectionInvalid:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_CertificateExpired:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_CertificateNotYetValid:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_CertificateRevoked:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_CertificateValidationError:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_ChallengeInvalid:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_EIMAuthorizationFailure:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_eMSPUnknown:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_EVPowerProfileViolation:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_GeneralPnCAuthorizationError:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_NoCertificateAvailable:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_NoContractMatchingPCIDFound:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_PowerToleranceNotConfirmed:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_ScheduleRenegotiationFailed:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_StandbyNotAllowed:
        [[fallthrough]];
    case dt::ResponseCode::WARNING_WPT:
        return true; // TODO(SL): Check if this is correct
    case dt::ResponseCode::FAILED:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_AssociationError:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_ContactorError:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_EVPowerProfileInvalid:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_EVPowerProfileViolation:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_MeteringSignatureNotValid:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_NoEnergyTransferServiceSelected:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_NoServiceRenegotiationSupported:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_PauseNotAllowed:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_PowerDeliveryNotApplied:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_PowerToleranceNotConfirmed:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_ScheduleRenegotiation:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_ScheduleSelectionInvalid:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_SequenceError:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_ServiceIDInvalid:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_ServiceSelectionInvalid:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_SignatureError:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_UnknownSession:
        [[fallthrough]];
    case dt::ResponseCode::FAILED_WrongChargeParameter:
        [[fallthrough]];
    default:
        return false;
    }
}

} // namespace iso15118::ev::d20
