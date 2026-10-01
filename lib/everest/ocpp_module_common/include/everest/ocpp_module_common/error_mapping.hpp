// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/ocpp_module_common/error_handling.hpp>

#include <ocpp/v16/charge_point_state_machine.hpp>
#include <ocpp/v16/ocpp_enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <utils/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace ocpp_module_common {

/// \brief Converts an EVerest error into an OCPP 1.6 ErrorInfo.
///
/// An implementation handles some set of errors and reports an error outside that set by returning
/// std::nullopt, so a caller can ask several implementations in turn.
///
/// \code
/// const MrecErrorMapping mapping;
/// if (const auto info = mapping.try_convert(error); info.has_value()) {
///     charge_point.on_error(evse_id, info.value());
/// }
/// \endcode
struct ErrorMappingV16 {
    virtual ~ErrorMappingV16() = default;

    /// \returns the converted ErrorInfo, or std::nullopt if \p error is not one this mapping handles
    virtual std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const = 0;
};

/// \brief Converts an EVerest error into OCPP 2.x EventData.
///
/// An implementation handles some set of errors and reports an error outside that set by returning
/// std::nullopt, so a caller can ask several implementations in turn.
///
/// \code
/// const MrecErrorMapping mapping;
/// if (const auto data = mapping.try_convert(error, cleared, event_id); data.has_value()) {
///     charge_point.on_event({data.value()});
/// }
/// \endcode
struct ErrorMappingV2X {
    virtual ~ErrorMappingV2X() = default;

    /// \param cleared whether the error was cleared rather than raised
    /// \param event_id identifies the event; the caller chooses the numbering
    /// \returns the converted EventData, or std::nullopt if \p error is not one this mapping handles
    virtual std::optional<ocpp::v2::EventData> try_convert(const Everest::error::Error& error, bool cleared,
                                                           std::int32_t event_id) const = 0;
};

/// \brief Builds the ErrorInfo fields that follow from the error alone: its uuid and timestamp, an
///        error code of OtherError, and not a fault.
///
/// Useful as a starting point for an \ref ErrorMappingV16 implementation, which then overwrites the
/// fields it has something better to say about.
ocpp::v16::ErrorInfo make_v16_error_info(const Everest::error::Error& error);

/// \brief Builds the EventData fields that follow from the error alone, leaving techCode unset.
///
/// Useful as a starting point for an \ref ErrorMappingV2X implementation, which then sets techCode.
ocpp::v2::EventData make_v2_event_data(const Everest::error::Error& error, bool cleared, std::int32_t event_id);

/// \brief Maps the MREC (ChargeX) error set to OCPP 1.6 and OCPP 2.x.
///
/// Owns one table of the MREC errors. Each entry carries the OCPP 1.6 ChargePointErrorCode and the
/// CX code, which OCPP 1.6 reports as the vendorErrorCode and OCPP 2.x as the techCode, so both
/// protocol versions are described by the same table.
///
/// The two versions match an error type differently: OCPP 1.6 accepts a type that *contains* a
/// known key, OCPP 2.x only a type equal to one.
///
/// \code
/// const MrecErrorMapping mapping;              // 2.x tech codes come from the built-in table
/// const MrecErrorMapping other{tech_codes};    // 2.x tech codes come from the given table
/// \endcode
class MrecErrorMapping : public ErrorMappingV16, public ErrorMappingV2X {
public:
    /// \brief An OCPP 1.6 error code paired with the CX code both protocol versions report.
    using Entry = std::pair<ocpp::v16::ChargePointErrorCode, std::string>;
    using Table = std::unordered_map<Everest::error::ErrorType, Entry>;

    MrecErrorMapping();

    /// \param tech_codes error type to techCode, consulted for OCPP 2.x in place of \ref entries.
    ///        Must outlive this object.
    explicit MrecErrorMapping(const MREC_ERROR_MAP_TYPE& tech_codes);

    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override;
    std::optional<ocpp::v2::EventData> try_convert(const Everest::error::Error& error, bool cleared,
                                                   std::int32_t event_id) const override;

    /// \returns the MREC errors, keyed by EVerest error type
    static const Table& entries();

    /// \returns the vendor id identifying these codes as MREC
    static const std::string& vendor_id();

    /// \returns \ref entries reduced to error type and techCode, for callers that want only the
    ///          codes OCPP 2.x reports. Built once, on first call.
    static const MREC_ERROR_MAP_TYPE& tech_codes();

private:
    /// null when OCPP 2.x lookups should use \ref entries
    const MREC_ERROR_MAP_TYPE* m_tech_codes{nullptr};
};

/// \brief Maps the errors that OCPP 1.6 names directly to their ChargePointErrorCode.
///
/// Owns the table of those errors. Accepts an error type that *contains* a known key.
///
/// \code
/// const OcppErrorMappingV16 mapping;
/// const auto info = mapping.try_convert(error);
/// \endcode
class OcppErrorMappingV16 : public ErrorMappingV16 {
public:
    using Table = std::unordered_map<Everest::error::ErrorType, ocpp::v16::ChargePointErrorCode>;

    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override;

    /// \returns the errors this mapping handles, keyed by EVerest error type
    static const Table& entries();
};

/// \brief Maps the aggregated evse_manager/Inoperative error, reporting it as a fault.
///
/// That error names no cause of its own, so whatever the raising module left in the message and
/// description is forwarded: the message as "caused_by:<message>" in info, the description as the
/// vendorErrorCode.
///
/// Accepts only an error type equal to evse_manager/Inoperative.
class InoperativeErrorMappingV16 : public ErrorMappingV16 {
public:
    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override;
};

/// \brief Maps any error to OCPP 1.6 without knowing anything about it, reporting the origin as
///        info, the message as the vendor id and a vendorErrorCode derived from the type.
///
/// Handles every error, so \ref try_convert never returns std::nullopt.
class DefaultErrorMappingV16 : public ErrorMappingV16 {
public:
    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override;

    /// \returns whether \p error should be reported as a fault
    static bool is_fault(const Everest::error::Error& error);

    /// \returns everything after the first '/' of the error type, or the whole type when it has no
    ///          '/', followed by '/' and the sub type
    static std::string vendor_error_code(const Everest::error::Error& error);
};

/// \brief Maps any error to OCPP 2.x without knowing anything about it, reporting the error type as
///        the techCode.
///
/// Handles every error, so \ref try_convert never returns std::nullopt.
class DefaultErrorMappingV2X : public ErrorMappingV2X {
public:
    std::optional<ocpp::v2::EventData> try_convert(const Everest::error::Error& error, bool cleared,
                                                   std::int32_t event_id) const override;
};

} // namespace ocpp_module_common
