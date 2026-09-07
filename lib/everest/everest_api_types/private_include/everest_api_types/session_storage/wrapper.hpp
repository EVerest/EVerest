// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/session_storage/API.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "generated/types/session_storage.hpp"
#pragma GCC diagnostic pop

namespace everest::lib::API::V1_0::types::session_storage {

using SessionState_Internal = ::types::session_storage::SessionState;
using SessionState_External = SessionState;

SessionState_Internal to_internal_api(SessionState_External const& val);
SessionState_External to_external_api(SessionState_Internal const& val);

using Transaction_Internal = ::types::session_storage::Transaction;
using Transaction_External = Transaction;

Transaction_Internal to_internal_api(Transaction_External const& val);
Transaction_External to_external_api(Transaction_Internal const& val);

using Session_Internal = ::types::session_storage::Session;
using Session_External = Session;

Session_Internal to_internal_api(Session_External const& val);
Session_External to_external_api(Session_Internal const& val);

using SessionFilter_Internal = ::types::session_storage::SessionFilter;
using SessionFilter_External = SessionFilter;

SessionFilter_Internal to_internal_api(SessionFilter_External const& val);
SessionFilter_External to_external_api(SessionFilter_Internal const& val);

using GetSessionsRequest_Internal = ::types::session_storage::GetSessionsRequest;
using GetSessionsRequest_External = GetSessionsRequest;

GetSessionsRequest_Internal to_internal_api(GetSessionsRequest_External const& val);
GetSessionsRequest_External to_external_api(GetSessionsRequest_Internal const& val);

using SessionList_Internal = ::types::session_storage::SessionList;
using SessionList_External = SessionList;

SessionList_Internal to_internal_api(SessionList_External const& val);
SessionList_External to_external_api(SessionList_Internal const& val);

using SessionIdentifier_Internal = ::types::session_storage::SessionIdentifier;
using SessionIdentifier_External = SessionIdentifier;

SessionIdentifier_Internal to_internal_api(SessionIdentifier_External const& val);
SessionIdentifier_External to_external_api(SessionIdentifier_Internal const& val);

using ClearSessionsResult_Internal = ::types::session_storage::ClearSessionsResult;
using ClearSessionsResult_External = ClearSessionsResult;

ClearSessionsResult_Internal to_internal_api(ClearSessionsResult_External const& val);
ClearSessionsResult_External to_external_api(ClearSessionsResult_Internal const& val);

} // namespace everest::lib::API::V1_0::types::session_storage
