// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>

// Value-added services (VAS) of external providers in ISO 15118-2. The module-facing feedback callbacks
// (get_vas_parameters / selected_vas_services) speak the ISO 15118-20 parameter datatypes for both
// protocol generations; these helpers translate them to and from the ISO 15118-2 wire datatypes.
namespace iso15118::d2 {

namespace m2dt = message_2::datatypes;
namespace m20dt = message_20::datatypes;

// True when \p service_id is one of the offered external VAS services.
bool is_offered_vas(const m2dt::ServiceList& offered, uint16_t service_id);

// ServiceDetailRes ServiceParameterList from the provider's parameter sets. ISO 15118-2 carries at most 5
// parameter sets of 16 parameters each (the -20 datatypes allow 32 x 32); the excess is dropped with a
// warning. A RationalNumber becomes a PhysicalValue in W (the VAS interface carries no unit; EvseV2G
// parity). Returns nullopt for an empty list so the optional element is left out of the response.
std::optional<m2dt::ServiceParameterList> to_iso2_parameter_list(const m20dt::ServiceParameterList& in);

// The external VAS entries of a PaymentServiceSelectionReq SelectedServiceList, in the form the
// selected_vas_services feedback takes. A SelectedService without ParameterSetID is reported with
// parameter set 0 (EvseV2G parity).
m20dt::VasSelectedServiceList selected_vas_services(const m2dt::SelectedServiceList& selected,
                                                    const m2dt::ServiceList& offered);

} // namespace iso15118::d2
