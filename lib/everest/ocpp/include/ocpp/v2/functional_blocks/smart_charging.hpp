// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <ocpp/v2/dynamic_schedule_manager.hpp>
#include <ocpp/v2/message_handler.hpp>

#include <ocpp/v2/evse.hpp>

namespace ocpp::v2 {
struct FunctionalBlockContext;
class SmartChargingHandlerInterface;

using StopTransactionCallback =
    std::function<RequestStartStopStatusEnum(const std::int32_t evse_id, const ReasonEnum& stop_reason)>;

struct LimitsSetpointsForOperationMode;

struct GetChargingProfilesRequest;
struct SetChargingProfileRequest;
struct SetChargingProfileResponse;
struct GetCompositeScheduleResponse;
struct GetCompositeScheduleRequest;
struct ClearChargingProfileResponse;
struct ClearChargingProfileRequest;
struct ReportChargingProfilesRequest;
struct NotifyEVChargingNeedsRequest;
struct NotifyEVChargingNeedsResponse;

/// \brief Different types of limits and setpoints, used in the limits_setpoints_per_operation_mode map.
enum class LimitSetpointType {
    Limit,
    DischargeLimit,
    Setpoint,
    SetpointReactive
};

enum class ProfileValidationResultEnum {
    Valid,
    EvseDoesNotExist,
    ExistingChargingStationExternalConstraints,
    InvalidProfileType,
    TxProfileMissingTransactionId,
    TxProfileEvseIdNotGreaterThanZero,
    TxProfileTransactionNotOnEvse,
    TxProfileEvseHasNoActiveTransaction,
    TxProfileConflictingStackLevel,
    ChargingProfileNoChargingSchedulePeriods,
    ChargingProfileFirstStartScheduleIsNotZero,
    ChargingProfileMissingRequiredStartSchedule,
    ChargingProfileExtraneousStartSchedule,
    ChargingProfileRateLimitExceeded,
    ChargingProfileIdSmallerThanMaxExternalConstraintsId,
    ChargingProfileUnsupportedPurpose,
    ChargingProfileUnsupportedKind,
    ChargingProfileNotDynamic,
    ChargingProfileDynamicMustHaveSinglePeriod,
    ChargingProfileDynamicMustHaveSingleSchedule,
    ChargingScheduleChargingRateUnitUnsupported,
    ChargingScheduleNonFiniteValue,
    ChargingSchedulePriorityExtranousDuration,
    ChargingScheduleRandomizedDelay,
    ChargingScheduleUnsupportedLocalTime,
    ChargingScheduleUnsupportedRandomizedDelay,
    ChargingScheduleUnsupportedLimitAtSoC,
    ChargingScheduleUnsupportedEvseSleep,
    ChargingSchedulePeriodsOutOfOrder,
    ChargingSchedulePeriodInvalidPhaseToUse,
    ChargingSchedulePeriodUnsupportedNumberPhases,
    ChargingSchedulePeriodExtraneousPhaseValues,
    ChargingSchedulePeriodPhaseToUseACPhaseSwitchingUnsupported,
    ChargingSchedulePeriodPriorityChargingNotChargingOnly,
    ChargingSchedulePeriodUnsupportedOperationMode,
    ChargingSchedulePeriodOperationModeNotInSupportedList,
    ChargingSchedulePeriodLocalLoadBalancingNotSupported,
    ChargingSchedulePeriodUnsupportedLimitSetpoint,
    ChargingSchedulePeriodNoPhaseForDC,
    ChargingSchedulePeriodNoFreqWattCurve,
    ChargingSchedulePeriodSignDifference,
    ChargingSchedulePeriodSetpointOutOfRange,
    ChargingSchedulePeriodPhaseConflict,
    ChargingStationMaxProfileCannotBeRelative,
    ChargingStationMaxProfileEvseIdGreaterThanZero,
    DuplicateTxDefaultProfileFound,
    DuplicateProfileValidityPeriod,
    RequestStartTransactionNonTxProfile,
    ChargingProfileEmptyChargingSchedules,
    ChargingSchedulePeriodNonFiniteValue
};

/// \brief This is used to associate charging profiles with a source.
/// Based on the source a different validation path can be taken.
enum class AddChargingProfileSource {
    SetChargingProfile,
    RequestStartTransactionRequest
};

///
/// \brief validates requirements that apply only to the ChargingStationMaxProfile \p profile
/// according to the specification
///
ProfileValidationResultEnum validate_charging_station_max_profile(const ChargingProfile& profile, std::int32_t evse_id);

namespace conversions {
/// \brief Converts the given ProfileValidationResultEnum \p e to human readable string
/// \returns a string representation of the ProfileValidationResultEnum
std::string profile_validation_result_to_string(ProfileValidationResultEnum e);

/// \brief Converts the given ProfileValidationResultEnum \p e to a OCPP reasonCode.
/// \returns a reasonCode
std::string profile_validation_result_to_reason_code(ProfileValidationResultEnum e);
} // namespace conversions

std::ostream& operator<<(std::ostream& os, const ProfileValidationResultEnum validation_result);

class SmartChargingInterface : public MessageHandlerInterface {
public:
    ~SmartChargingInterface() override = default;

    /// \brief Gets composite schedules for all evse_ids (including 0) for the given \p duration and \p unit . If no
    /// valid profiles are given for an evse for the specified period, the composite schedule will be empty for this
    /// evse.
    /// \param duration of the request from. Composite schedules will be retrieved from now to (now + duration)
    /// \param unit of the period entries of the composite schedules
    /// \return vector of composite schedules, one for each evse_id including 0.
    virtual std::vector<EnhancedCompositeSchedule> get_all_composite_schedules(const std::int32_t duration,
                                                                               const ChargingRateUnitEnum& unit) = 0;

    ///
    /// \brief for the given \p transaction_id removes the associated charging profile.
    ///
    virtual void delete_transaction_tx_profiles(const std::string& transaction_id) = 0;

    ///
    /// \brief validates the given \p profile according to the specification,
    /// adding it to our stored list of profiles if valid.
    ///
    virtual SetChargingProfileResponse conform_validate_and_add_profile(
        ChargingProfile& profile, std::int32_t evse_id,
        CiString<20> charging_limit_source = ChargingLimitSourceEnumStringType::CSO,
        AddChargingProfileSource source_of_request = AddChargingProfileSource::SetChargingProfile) = 0;

    ///
    /// \brief validates the given \p profile according to the specification.
    /// If a profile does not have validFrom or validTo set, we conform the values
    /// to a representation that fits the spec.
    ///
    virtual ProfileValidationResultEnum conform_and_validate_profile(
        ChargingProfile& profile, std::int32_t evse_id,
        AddChargingProfileSource source_of_request = AddChargingProfileSource::SetChargingProfile) = 0;

    /// \brief Gets a composite schedule based on the given \p request
    /// \param request specifies different options for the request
    /// \return EnhancedCompositeScheduleResponse containing the status of the operation and the composite schedule if
    /// the operation was successful
    virtual EnhancedCompositeScheduleResponse get_composite_schedule(const GetCompositeScheduleRequest& request) = 0;

    /// \brief Gets a composite schedule based on the given parameters.
    /// \note This will ignore TxDefaultProfiles and TxProfiles if no transaction is active on \p evse_id
    /// \param evse_id Evse to get the schedule for
    /// \param duration How long the schedule should be
    /// \param unit ChargingRateUnit to thet the schedule for
    /// \return the composite schedule if the operation was successful, otherwise nullopt
    virtual std::optional<EnhancedCompositeSchedule>
    get_composite_schedule(std::int32_t evse_id, std::chrono::seconds duration, ChargingRateUnitEnum unit) = 0;

    /// \brief Initiates a NotifyEvChargingNeeds.req message to the CSMS
    /// \param req the request to send
    virtual void notify_ev_charging_needs_req(const NotifyEVChargingNeedsRequest& req) = 0;
};

class SmartCharging : public SmartChargingInterface {
private: // Members
    const FunctionalBlockContext& context;
    std::function<void()> set_charging_profiles_callback;
    std::map<ChargingProfilePurposeEnum, DateTime> last_charging_profile_update;
    StopTransactionCallback stop_transaction_callback;

protected: // Members
    /// \brief K28 dynamic-profile state: pull/expire deadlines, async pull-response handlers, and
    /// the adaptive timer. Engaged only when the device model advertises SupportsDynamicProfiles, so
    /// stations without Dynamic support pay no thread/timer cost. Declared last so it destructs first:
    /// its timer joins the io_context thread before the rest of SmartCharging tears down.
    std::optional<DynamicScheduleManager> dynamic_schedule_manager;

public:
    /// \brief Construct the SmartCharging functional block.
    /// \param functional_block_context        Shared context (device model, database, dispatcher).
    /// \param set_charging_profiles_callback  Invoked whenever the set of valid charging profiles
    ///                                         changes and consumers must recompute composite
    ///                                         schedules. Also fired from the timer thread on
    ///                                         K28.FR.13 expiry and K28.FR.06 push apply.
    /// \param stop_transaction_callback       Invoked from the NotifyEVChargingNeeds response path
    ///                                         when the schedule mandates transaction termination
    ///                                         (K18.FR.23 / K19.FR.16).
    /// \note On construction, rebuilds pull- and expiry-deadline tracking from persisted profiles
    /// (K28.FR.10) and arms the adaptive timer.
    SmartCharging(const FunctionalBlockContext& functional_block_context,
                  std::function<void()> set_charging_profiles_callback,
                  StopTransactionCallback stop_transaction_callback);

    ~SmartCharging() override = default;

    void handle_message(const ocpp::EnhancedMessage<MessageType>& message) override;
    EnhancedCompositeScheduleResponse get_composite_schedule(const GetCompositeScheduleRequest& request) override;
    std::optional<EnhancedCompositeSchedule> get_composite_schedule(std::int32_t evse_id, std::chrono::seconds duration,
                                                                    ChargingRateUnitEnum unit) override;
    std::vector<EnhancedCompositeSchedule> get_all_composite_schedules(const std::int32_t duration,
                                                                       const ChargingRateUnitEnum& unit) override;

    void delete_transaction_tx_profiles(const std::string& transaction_id) override;

    SetChargingProfileResponse conform_validate_and_add_profile(
        ChargingProfile& profile, std::int32_t evse_id,
        CiString<20> charging_limit_source = ChargingLimitSourceEnumStringType::CSO,
        AddChargingProfileSource source_of_request = AddChargingProfileSource::SetChargingProfile) override;
    ProfileValidationResultEnum conform_and_validate_profile(
        ChargingProfile& profile, std::int32_t evse_id,
        AddChargingProfileSource source_of_request = AddChargingProfileSource::SetChargingProfile) override;
    void notify_ev_charging_needs_req(const NotifyEVChargingNeedsRequest& req) override;

protected:
    ///
    /// \brief Calculates the composite schedule for the given \p valid_profiles and the given \p connector_id
    ///
    EnhancedCompositeSchedule calculate_composite_schedule(const ocpp::DateTime& start_time,
                                                           const ocpp::DateTime& end_time, const std::int32_t evse_id,
                                                           ChargingRateUnitEnum charging_rate_unit, bool is_offline,
                                                           bool simulate_transaction_active);

    ///
    /// \brief validates the existence of the given \p evse_id according to the specification
    ///
    ProfileValidationResultEnum validate_evse_exists(std::int32_t evse_id) const;

    ///
    /// \brief validates the given \p profile and associated \p evse_id according to the specification
    ///
    ProfileValidationResultEnum validate_tx_default_profile(const ChargingProfile& profile, std::int32_t evse_id) const;

    ///
    /// \brief validates the given \p profile according to the specification
    ///
    ProfileValidationResultEnum validate_tx_profile(
        const ChargingProfile& profile, std::int32_t evse_id,
        AddChargingProfileSource source_of_request = AddChargingProfileSource::SetChargingProfile) const;

    ///
    /// \brief Validates the given profile according to the specification.
    /// \param profile  Profile to validate.
    /// \param evse_id  Evse id this charging profile belongs to.
    /// \return ProfileValidationResultEnum::Valid if valid.
    ///
    ProfileValidationResultEnum validate_priority_charging_profile(const ChargingProfile& profile,
                                                                   std::int32_t evse_id) const;

    /// \brief validates that the given \p profile has valid charging schedules.
    /// If a profiles charging schedule period does not have a valid numberPhases,
    /// we set it to the default value (3).
    ProfileValidationResultEnum validate_profile_schedules(ChargingProfile& profile,
                                                           std::optional<EvseInterface*> evse_opt = std::nullopt) const;

    ///
    /// \brief Q09.FR.01: whether \p operation_mode is listed in V2XChargingCtrlr.SupportedOperationModes for
    ///        the given EVSE. The variable is per-EVSE (component evse = *, there is no Charging-Station-level
    ///        instance), so callers check the profile's EVSE, or every EVSE for a station-wide profile.
    /// \return true for ChargingOnly or when \p operation_mode is listed; false when the variable is absent
    ///         (no V2X advertised) or the mode is not listed.
    ///
    bool is_operation_mode_supported_by_evse(OperationModeEnum operation_mode, std::int32_t evse_id) const;

    ///
    /// \brief V2X.05: Ensure every charging schedule period has setpoint within [dischargeLimit, limit].
    ///
    /// Stored profiles keep their clamping behavior in the composite schedule, so this must not
    /// run from validate_profile_schedules which is reused during composite-schedule calculation.
    ///
    /// \param profile  Charging profile to validate.
    /// \return ProfileValidationResultEnum::Valid when all setpoints lie in range (or not set),
    ///         otherwise ChargingSchedulePeriodSetpointOutOfRange.
    ///
    ProfileValidationResultEnum validate_setpoint_within_limit_range(const ChargingProfile& profile) const;

    ///
    /// \brief Reject SetChargingProfile requests that violate the V2X.09 branch of V2X.10:
    ///        a non-TxProfile carrying any \c dischargeLimit_L2 / \c _L3,
    ///        \c setpoint_L2 / \c _L3, or \c setpointReactive_L2 / \c _L3 in any
    ///        chargingSchedulePeriod must be rejected with reasonCode `PhaseConflict`.
    ///
    /// The V2X.08 branch (EV omitted \c maxDischargePower_L2 / \c _L3 in
    /// NotifyEVChargingNeedsRequest) requires per-EVSE caching of EV V2X parameters
    /// and is intentionally deferred.
    ///
    /// \param profile  Charging profile to validate.
    /// \return Valid when the profile is a TxProfile, or when no per-phase fields are
    ///         present; otherwise ChargingSchedulePeriodPhaseConflict.
    ///
    ProfileValidationResultEnum validate_phase_conflict(const ChargingProfile& profile) const;

    ///
    /// \brief Checks a given \p profile does not have an id that conflicts with an existing profile
    /// of type ChargingStationExternalConstraints
    ///
    ProfileValidationResultEnum verify_no_conflicting_external_constraints_id(const ChargingProfile& profile) const;

    ///
    /// \brief Adds a given \p profile and associated \p evse_id to our stored list of profiles
    ///
    SetChargingProfileResponse add_profile(ChargingProfile& profile, std::int32_t evse_id,
                                           CiString<20> charging_limit_source = ChargingLimitSourceEnumStringType::CSO);

    /// \brief Clears profiles using \p request and reports the ids actually deleted in
    /// \p cleared_ids (exactly the rows the DB removed; no mirrored filter).
    ClearChargingProfileResponse clear_profiles(const ClearChargingProfileRequest& request,
                                                std::vector<std::int32_t>& cleared_ids);

    ///
    /// \brief Gets the charging profiles for the given \p request
    ///
    std::vector<ReportedChargingProfile> get_reported_profiles(const GetChargingProfilesRequest& request) const;

    /// \brief Retrieves all profiles that should be considered for calculating the composite schedule. Only profiles
    /// that belong to the given \p evse_id and that are not contained in \p purposes_to_ignore are included in the
    /// response.
    ///
    std::vector<ChargingProfile>
    get_valid_profiles(std::int32_t evse_id, const std::vector<ChargingProfilePurposeEnum>& purposes_to_ignore = {});

    /// \brief Return valid (non-expired, conforming) profiles for \p evse_id.
    /// \param evse_id            EVSE the profiles must belong to.
    /// \param purposes_to_ignore Profile purposes to exclude from the result (e.g. when the caller
    ///                            is computing a composite limit for a specific purpose subset).
    /// \return Filtered profile list. Beyond the existing purpose filter and offline-validity check
    ///         (Q11/Q12), K28.FR.13 Dynamic profiles whose \c chargingSchedule[0].duration has
    ///         elapsed since \c dynUpdateTime are skipped. The boundary check uses
    ///         \c now\ >=\ deadline so it agrees with the manager timer's
    ///         \c deadline\ <=\ now at the exact boundary instant.
    /// \note Profiles without \c dynUpdateTime defer to bootstrap pull and are NOT expiry-filtered.
    std::vector<ChargingProfile>
    get_valid_profiles_for_evse(std::int32_t evse_id,
                                const std::vector<ChargingProfilePurposeEnum>& purposes_to_ignore = {});

private: // Functions
    /* OCPP message requests */
    void report_charging_profile_req(const std::int32_t request_id, const std::int32_t evse_id,
                                     const CiString<20> source, const std::vector<ChargingProfile>& profiles,
                                     const bool tbc);
    void report_charging_profile_req(const ReportChargingProfilesRequest& req);

    /* OCPP message handlers */
    void handle_set_charging_profile_req(Call<SetChargingProfileRequest> call);
    void handle_clear_charging_profile_req(Call<ClearChargingProfileRequest> call);
    void handle_get_charging_profiles_req(Call<GetChargingProfilesRequest> call);
    void handle_get_composite_schedule_req(Call<GetCompositeScheduleRequest> call);
    void handle_notify_ev_charging_needs_response(const EnhancedMessage<MessageType>& call_result);

    EnhancedCompositeScheduleResponse get_composite_schedule_internal(const GetCompositeScheduleRequest& request,
                                                                      bool simulate_transaction_active = true);

    ///
    /// \brief Checks a given \p candidate_profile and associated \p evse_id validFrom and validTo range
    /// This method assumes that the existing candidate_profile will have dates set for validFrom and validTo
    ///
    bool is_overlapping_validity_period(const ChargingProfile& candidate_profile, std::int32_t candidate_evse_id) const;

    std::vector<ChargingProfile> get_evse_specific_tx_default_profiles() const;
    std::vector<ChargingProfile> get_station_wide_tx_default_profiles() const;
    std::vector<ChargingProfile> get_charging_station_max_profiles() const;

    CurrentPhaseType get_current_phase_type(const std::optional<EvseInterface*> evse_opt) const;

    ///
    /// \brief Verify rate limit, only for OCPP 2.1
    ///
    /// When Charging Station receives a SetChargingProfileRequest for a ChargingProfileType with a
    /// chargingProfilePurpose that is to be stored persistently AND the previous SetChargingProfileRequest for this
    /// chargingProfilePurpose was less than ChargingProfileUpdate RateLimit seconds ago, Charging Station MAY respond
    /// with SetChargingProfileResponse with status = Rejected and reasonCode = "RateLimitExceeded" (K01.FR.56).
    ///
    /// \param profile  Charging profile
    /// \return ProfileValidationResultEnum::Valid when rate limit was not exceeded, or OCPP 2.0.1.
    ///         ProfileValidationResultEnum::ChargingProfileRateLimitExceeded when rate limit was exceeded.
    ///
    ProfileValidationResultEnum verify_rate_limit(const ChargingProfile& profile);

    ///
    /// \brief Validate ChargingProfile regarding the offline time, only for OCPP 2.1
    ///
    /// When the offline time is higher than maxOfflineDuration, the profile is invalid. When
    /// invalidAfterOfflineDuration is set to true, the profile should never be used again an can be cleared (Q11).
    ///
    /// \param profile  Charging profile
    /// \return Pair of valid and clear flags.
    ///
    std::pair<bool, bool> validate_profile_with_offline_time(const ChargingProfile& profile);

    ///
    /// \brief Check if DCInputPhaseControl is enabled for this evse id.
    ///
    /// \note This function can also be used for evse id 0, it will then check all existing evse's for this variable.
    ///
    /// \param evse_id  The evse id. Can also be 0.
    /// \return True if evse has DCInputPhaseControl enabled.
    ///
    bool has_dc_input_phase_control(const std::int32_t evse_id) const;

    ///
    /// \brief Check if DCInputPhaseControl is enabled for this evse id.
    /// \param evse_id  The evse id. Should not be 0.
    /// \return True if evse has DCInputPhaseControl enabled.
    ///
    bool evse_has_dc_input_phase_control(const std::int32_t evse_id) const;
};

///
/// \brief Check if limits and checkpoints of an operation mode are correct.
///
/// Check if all required limits and setpoints are set and if there are limits and / or setpoints that should not be
/// there.
///
/// \param limits_setpoints The information about required and optional limits and setpoints.
/// \param limit        Limit or setpoint (for phase 1 if L2 and L3 are set, otherwise for all phases).
/// \param limit_L2     Limit or setpoint phase 2.
/// \param limit_L3     Limit or setpoint phase 3.
/// \return True if all limits and setpoints are set / not set according to limits_setpoints struct.
///
bool are_limits_and_setpoints_of_operation_mode_correct(const LimitsSetpointsForOperationMode& limits_setpoints,
                                                        const ocpp::v2::LimitSetpointType& type,
                                                        const std::optional<double>& limit,
                                                        const std::optional<double>& limit_L2,
                                                        const std::optional<double>& limit_L3);

///
/// \brief Check if operation mode for the charging profile purpose is correct.
///
/// See 2.1 spec: Table 95. operationMode for various ChargingProfilePurposes
///
/// \param operation_mode   The operation mode.
/// \param purpose          The charging profile purpose.
/// \return True if this operation mode is allowed.
///
bool check_operation_modes_for_charging_profile_purposes(const OperationModeEnum& operation_mode,
                                                         const ChargingProfilePurposeEnum& purpose);

///
/// \brief Check if limits and checkpoints of an operation mode are correct.
///
/// Check if all required limits and setpoints are set and if there are limits and / or setpoints that should not be
/// there.
///
/// \param charging_schedule_period The charging schedule period.
/// \return True when all limits and limits are set / not set according to the spec.
///
bool check_limits_and_setpoints(const ChargingSchedulePeriod& charging_schedule_period);

///
/// \brief Check if all setpoint signs are equal (for all phases positive or for all phases negative).
///
/// This check assumes that if setpoint is not set, setpoint_L2 and setpoint_L3 are not set as well.
///
/// \param charging_schedule_period The schedule period to check the setpoints for.
/// \return true if all signs are equal.
///
bool all_setpoints_signs_equal(const ChargingSchedulePeriod& charging_schedule_period);
} // namespace ocpp::v2
