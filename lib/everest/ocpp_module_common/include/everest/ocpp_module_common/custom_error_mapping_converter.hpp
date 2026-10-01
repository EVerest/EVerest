// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/ocpp_module_common/custom_error_mapping.hpp>
#include <everest/ocpp_module_common/error_mapping.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ocpp_module_common::custom_error_mapping {

/// \brief Reports errors as a custom error mapping file describes them.
///
/// Meant to be asked first in a chain of mappings. An error without an entry, or whose entry has no
/// section for the asked protocol version, is not handled, so the rest of the chain reports it as before.
/// For an error with an entry, the result of the base mappings (the rest of the chain) is taken and only
/// the fields the entry sets are overwritten. Whether an OCPP 1.6 error is a fault is never changed.
///
/// The aggregated evse_manager/Inoperative error is not handled; it keeps its own mapping.
///
/// \code
/// const MrecErrorMapping mrec;
/// const DefaultErrorMappingV16 fallback_v16;
/// const DefaultErrorMappingV2X fallback_v2;
/// const CustomErrorMappingConverter custom{result.error_mapping, {&mrec, &fallback_v16}, {&mrec, &fallback_v2}};
/// \endcode
class CustomErrorMappingConverter : public ErrorMappingV16, public ErrorMappingV2X {
public:
    /// \param mapping the loaded entries; must not be null
    /// \param base_v16 the OCPP 1.6 mappings to build on, asked in order; must outlive this object
    /// \param base_v2 the OCPP 2.x mappings to build on, asked in order; must outlive this object
    CustomErrorMappingConverter(std::shared_ptr<const CustomErrorMapping> mapping,
                                std::vector<const ErrorMappingV16*> base_v16,
                                std::vector<const ErrorMappingV2X*> base_v2);

    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override;
    std::optional<ocpp::v2::EventData> try_convert(const Everest::error::Error& error, bool cleared,
                                                   std::int32_t event_id) const override;

    const CustomErrorMapping& mapping() const;

private:
    const Entry* entry_for(const Everest::error::Error& error) const;

    std::shared_ptr<const CustomErrorMapping> m_mapping;
    std::vector<const ErrorMappingV16*> m_base_v16;
    std::vector<const ErrorMappingV2X*> m_base_v2;
};

} // namespace ocpp_module_common::custom_error_mapping
