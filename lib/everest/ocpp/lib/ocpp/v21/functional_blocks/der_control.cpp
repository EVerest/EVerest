// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v21/functional_blocks/der_control.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/common/utils.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/database_handler.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/evse_manager.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

#include <everest/database/exceptions.hpp>
#include <everest/logging.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ocpp::v21 {

using namespace v2;

namespace {

constexpr std::size_t MAX_CURVE_POINTS = 256;
constexpr float MAX_DURATION_SECONDS = 86400.0F * 365.0F;          // one year
constexpr std::int64_t MAX_SCHEDULE_HORIZON_SECONDS = 86400 * 365; // one year

/// \brief Cap on total persisted DER controls to bound DER_CONTROLS table growth.
constexpr std::size_t MAX_DER_CONTROLS = 1000;

/// \brief Interval at which the scheduled-control timer checks for controls whose startTime has
/// arrived or whose duration has expired.
constexpr std::chrono::seconds SCHEDULED_CONTROL_CHECK_INTERVAL{30};

bool duration_is_sane(std::optional<float> d) {
    if (!d.has_value()) {
        return true;
    }
    return std::isfinite(d.value()) && d.value() >= 0.0F && d.value() <= MAX_DURATION_SECONDS;
}

bool is_finite_curve(const DERCurve& curve) {
    if (!ocpp::is_finite_or_unset(curve.responseTime) || !ocpp::is_finite_or_unset(curve.duration)) {
        return false;
    }
    for (const auto& point : curve.curveData) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return false;
        }
    }
    if (curve.hysteresis.has_value()) {
        const auto& h = curve.hysteresis.value();
        if (!ocpp::is_finite_or_unset(h.hysteresisHigh) || !ocpp::is_finite_or_unset(h.hysteresisLow) ||
            !ocpp::is_finite_or_unset(h.hysteresisDelay) || !ocpp::is_finite_or_unset(h.hysteresisGradient)) {
            return false;
        }
    }
    if (curve.reactivePowerParams.has_value()) {
        const auto& r = curve.reactivePowerParams.value();
        if (!ocpp::is_finite_or_unset(r.vRef) || !ocpp::is_finite_or_unset(r.autonomousVRefTimeConstant)) {
            return false;
        }
    }
    if (curve.voltageParams.has_value()) {
        const auto& v = curve.voltageParams.value();
        if (!ocpp::is_finite_or_unset(v.hv10MinMeanValue) || !ocpp::is_finite_or_unset(v.hv10MinMeanTripDelay)) {
            return false;
        }
    }
    return true;
}

// Rejects NaN/Inf before persistence so they cannot round-trip into a ReportDERControl.
bool are_control_floats_finite(const SetDERControlRequest& req) {
    if (req.curve.has_value() && !is_finite_curve(req.curve.value())) {
        return false;
    }
    if (req.freqDroop.has_value()) {
        const auto& fd = req.freqDroop.value();
        if (!std::isfinite(fd.overFreq) || !std::isfinite(fd.underFreq) || !std::isfinite(fd.overDroop) ||
            !std::isfinite(fd.underDroop) || !std::isfinite(fd.responseTime) ||
            !ocpp::is_finite_or_unset(fd.duration)) {
            return false;
        }
    }
    if (req.fixedVar.has_value()) {
        const auto& fv = req.fixedVar.value();
        if (!std::isfinite(fv.setpoint) || !ocpp::is_finite_or_unset(fv.duration)) {
            return false;
        }
    }
    if (req.fixedPFAbsorb.has_value()) {
        const auto& pf = req.fixedPFAbsorb.value();
        if (!std::isfinite(pf.displacement) || !ocpp::is_finite_or_unset(pf.duration)) {
            return false;
        }
    }
    if (req.fixedPFInject.has_value()) {
        const auto& pf = req.fixedPFInject.value();
        if (!std::isfinite(pf.displacement) || !ocpp::is_finite_or_unset(pf.duration)) {
            return false;
        }
    }
    if (req.limitMaxDischarge.has_value()) {
        const auto& lmd = req.limitMaxDischarge.value();
        if (!ocpp::is_finite_or_unset(lmd.pctMaxDischargePower) || !ocpp::is_finite_or_unset(lmd.duration)) {
            return false;
        }
        if (lmd.powerMonitoringMustTrip.has_value() && !is_finite_curve(lmd.powerMonitoringMustTrip.value())) {
            return false;
        }
    }
    if (req.enterService.has_value()) {
        const auto& es = req.enterService.value();
        if (!std::isfinite(es.highVoltage) || !std::isfinite(es.lowVoltage) || !std::isfinite(es.highFreq) ||
            !std::isfinite(es.lowFreq) || !ocpp::is_finite_or_unset(es.delay) ||
            !ocpp::is_finite_or_unset(es.randomDelay) || !ocpp::is_finite_or_unset(es.rampRate)) {
            return false;
        }
    }
    if (req.gradient.has_value()) {
        const auto& g = req.gradient.value();
        if (!std::isfinite(g.gradient) || !std::isfinite(g.softGradient)) {
            return false;
        }
    }
    return true;
}

bool mode_list_contains(const std::string& list, const std::string& wanted) {
    const auto tokens = ocpp::split_string(list, ',', true);
    return std::any_of(tokens.begin(), tokens.end(), [&](const std::string& token) { return token == wanted; });
}

int count_populated_control_fields(const SetDERControlRequest& req) {
    int count = 0;
    if (req.curve.has_value())
        count++;
    if (req.enterService.has_value())
        count++;
    if (req.fixedPFAbsorb.has_value())
        count++;
    if (req.fixedPFInject.has_value())
        count++;
    if (req.fixedVar.has_value())
        count++;
    if (req.freqDroop.has_value())
        count++;
    if (req.gradient.has_value())
        count++;
    if (req.limitMaxDischarge.has_value())
        count++;
    return count;
}

int32_t get_priority_from_request(const SetDERControlRequest& req) {
    if (req.freqDroop.has_value())
        return req.freqDroop->priority;
    if (req.curve.has_value())
        return req.curve->priority;
    if (req.enterService.has_value())
        return req.enterService->priority;
    if (req.fixedPFAbsorb.has_value())
        return req.fixedPFAbsorb->priority;
    if (req.fixedPFInject.has_value())
        return req.fixedPFInject->priority;
    if (req.fixedVar.has_value())
        return req.fixedVar->priority;
    if (req.gradient.has_value())
        return req.gradient->priority;
    if (req.limitMaxDischarge.has_value())
        return req.limitMaxDischarge->priority;
    return 0;
}

std::optional<std::string> get_start_time_from_request(const SetDERControlRequest& req) {
    if (req.freqDroop.has_value() && req.freqDroop->startTime.has_value())
        return req.freqDroop->startTime->to_rfc3339();
    if (req.curve.has_value() && req.curve->startTime.has_value())
        return req.curve->startTime->to_rfc3339();
    if (req.fixedPFAbsorb.has_value() && req.fixedPFAbsorb->startTime.has_value())
        return req.fixedPFAbsorb->startTime->to_rfc3339();
    if (req.fixedPFInject.has_value() && req.fixedPFInject->startTime.has_value())
        return req.fixedPFInject->startTime->to_rfc3339();
    if (req.fixedVar.has_value() && req.fixedVar->startTime.has_value())
        return req.fixedVar->startTime->to_rfc3339();
    if (req.limitMaxDischarge.has_value() && req.limitMaxDischarge->startTime.has_value())
        return req.limitMaxDischarge->startTime->to_rfc3339();
    return std::nullopt;
}

std::optional<float> get_duration_from_request(const SetDERControlRequest& req) {
    if (req.freqDroop.has_value() && req.freqDroop->duration.has_value())
        return req.freqDroop->duration;
    if (req.curve.has_value() && req.curve->duration.has_value())
        return req.curve->duration;
    if (req.fixedPFAbsorb.has_value() && req.fixedPFAbsorb->duration.has_value())
        return req.fixedPFAbsorb->duration;
    if (req.fixedPFInject.has_value() && req.fixedPFInject->duration.has_value())
        return req.fixedPFInject->duration;
    if (req.fixedVar.has_value() && req.fixedVar->duration.has_value())
        return req.fixedVar->duration;
    if (req.limitMaxDischarge.has_value() && req.limitMaxDischarge->duration.has_value())
        return req.limitMaxDischarge->duration;
    return std::nullopt;
}

bool has_start_time_or_duration(const SetDERControlRequest& req) {
    return get_start_time_from_request(req).has_value() || get_duration_from_request(req).has_value();
}

// Unhandled enum value falls through to fail-closed.
bool validate_yunit(DERControlEnum control_type, const DERCurve& curve) {
    auto unit = curve.yUnit;
    switch (control_type) {
    // R04.FR.50
    case DERControlEnum::FreqWatt:
        return unit == DERUnitEnum::PctMaxW || unit == DERUnitEnum::PctWAvail;

    // R04.FR.51
    case DERControlEnum::HFMustTrip:
    case DERControlEnum::HFMayTrip:
    case DERControlEnum::HVMustTrip:
    case DERControlEnum::HVMomCess:
    case DERControlEnum::HVMayTrip:
    case DERControlEnum::LFMustTrip:
    case DERControlEnum::LVMustTrip:
    case DERControlEnum::LVMomCess:
    case DERControlEnum::LVMayTrip:
    case DERControlEnum::PowerMonitoringMustTrip:
        return unit == DERUnitEnum::Not_Applicable;

    // R04.FR.52
    case DERControlEnum::VoltVar:
        return unit == DERUnitEnum::PctMaxVar || unit == DERUnitEnum::PctVarAvail;

    // R04.FR.53
    case DERControlEnum::VoltWatt:
        return unit == DERUnitEnum::PctMaxW || unit == DERUnitEnum::PctWAvail;

    // R04.FR.54
    case DERControlEnum::WattPF:
        return unit == DERUnitEnum::Not_Applicable;

    // R04.FR.55
    case DERControlEnum::WattVar:
        return unit == DERUnitEnum::PctMaxVar || unit == DERUnitEnum::PctVarAvail;

    case DERControlEnum::EnterService:
    case DERControlEnum::FreqDroop:
    case DERControlEnum::FixedPFAbsorb:
    case DERControlEnum::FixedPFInject:
    case DERControlEnum::FixedVar:
    case DERControlEnum::Gradients:
    case DERControlEnum::LimitMaxDischarge:
        return true;
    }
    return false;
}

} // anonymous namespace

DERControl::DERControl(const v2::FunctionalBlockContext& context,
                       std::optional<DERActiveDirectivesCallback> active_directives_callback) :
    context(context), active_directives_callback(std::move(active_directives_callback)) {
    // Lambda holds a stopping handle for callback duration so destructor can wait for in-flight runs.
    this->scheduled_control_timer.interval(
        [this]() {
            auto handle = this->stopping.handle();
            if (*handle) {
                return;
            }
            this->check_scheduled_controls();
        },
        SCHEDULED_CONTROL_CHECK_INTERVAL);

    this->emit_active_directives();
}

DERControl::~DERControl() {
    // 1. Set stopping flag so any subsequent callback short-circuits.
    {
        auto handle = this->stopping.handle();
        *handle = true;
    }
    // 2. Cancel the timer.
    this->scheduled_control_timer.stop();
    // 3. Re-acquire the handle to drain any callback already running on the io thread.
    //    Required because ChargePoint frees DatabaseHandler before DERControl.
    (void)this->stopping.handle();
}

void DERControl::handle_message(const ocpp::EnhancedMessage<v2::MessageType>& message) {
    const auto& json_message = message.message;
    if (message.messageType == v2::MessageType::SetDERControl) {
        this->handle_set_der_control(json_message);
    } else if (message.messageType == v2::MessageType::GetDERControl) {
        this->handle_get_der_control(json_message);
    } else if (message.messageType == v2::MessageType::ClearDERControl) {
        this->handle_clear_der_control(json_message);
    } else {
        throw v2::MessageTypeNotImplementedException(message.messageType);
    }
}

bool DERControl::is_control_type_supported(v2::DERControlEnum control_type) const {
    const auto control_type_str = v2::conversions::dercontrol_enum_to_string(control_type);
    const auto& evse_manager = this->context.evse_manager;

    for (int32_t evse_id = 1; evse_id <= static_cast<int32_t>(evse_manager.get_number_of_evses()); evse_id++) {
        auto dc_modes_cv =
            DERComponentVariables::get_dc_component_variable(evse_id, DERComponentVariables::ModesSupported);
        auto dc_modes = this->context.device_model.get_optional_value<std::string>(dc_modes_cv);
        if (dc_modes.has_value() && mode_list_contains(dc_modes.value(), control_type_str)) {
            return true;
        }

        auto ac_modes_cv =
            DERComponentVariables::get_ac_component_variable(evse_id, DERComponentVariables::ModesSupported);
        auto ac_modes = this->context.device_model.get_optional_value<std::string>(ac_modes_cv);
        if (ac_modes.has_value() && mode_list_contains(ac_modes.value(), control_type_str)) {
            return true;
        }
    }
    return false;
}

bool DERControl::validate_control_fields(const SetDERControlRequest& req) const {
    // R04.FR.17: Reject if multiple control fields are set
    if (count_populated_control_fields(req) != 1) {
        return false;
    }

    // Bound priority, duration, and curveData size before we touch the DB.
    if (get_priority_from_request(req) < 0) {
        return false;
    }
    if (!duration_is_sane(get_duration_from_request(req))) {
        return false;
    }
    if (req.curve.has_value() && req.curve->curveData.size() > MAX_CURVE_POINTS) {
        return false;
    }

    // Reject non-finite (NaN / +/-Inf) float values on any control variant. Without this
    // a CSMS-supplied NaN survives into CONTROL_JSON, serializes as JSON null, and
    // is later echoed back on ReportDERControl.
    if (!are_control_floats_finite(req)) {
        return false;
    }

    // Reject startTime beyond a bounded schedule horizon. Otherwise a zombie scheduled
    // control with startTime in year 2999 would persist until explicit Clear.
    const auto start_time_str = get_start_time_from_request(req);
    if (start_time_str.has_value()) {
        try {
            const auto start_tp = ocpp::DateTime(start_time_str.value()).to_time_point();
            const auto horizon_tp =
                ocpp::DateTime().to_time_point() + std::chrono::seconds(MAX_SCHEDULE_HORIZON_SECONDS);
            if (start_tp > horizon_tp) {
                return false;
            }
        } catch (const std::exception&) {
            // Unparseable startTime is itself invalid.
            return false;
        }
    }

    // R04.FR.16: Validate that the correct field is set for the controlType
    switch (req.controlType) {
    case DERControlEnum::FixedPFAbsorb:
        return req.fixedPFAbsorb.has_value();
    case DERControlEnum::FixedPFInject:
        return req.fixedPFInject.has_value();
    case DERControlEnum::FixedVar:
        return req.fixedVar.has_value();
    case DERControlEnum::LimitMaxDischarge:
        // R04.FR.56: If powerMonitoringMustTrip curve is present, its yUnit must be Not_Applicable
        if (req.limitMaxDischarge.has_value() && req.limitMaxDischarge->powerMonitoringMustTrip.has_value()) {
            if (!validate_yunit(DERControlEnum::PowerMonitoringMustTrip,
                                req.limitMaxDischarge->powerMonitoringMustTrip.value())) {
                return false;
            }
        }
        return req.limitMaxDischarge.has_value();
    case DERControlEnum::FreqDroop:
        return req.freqDroop.has_value();
    case DERControlEnum::EnterService:
        return req.enterService.has_value();
    case DERControlEnum::Gradients:
        return req.gradient.has_value();
    // All curve-based types require the curve field + R04.FR.50-55 yUnit validation
    case DERControlEnum::FreqWatt:
    case DERControlEnum::HFMustTrip:
    case DERControlEnum::HFMayTrip:
    case DERControlEnum::HVMustTrip:
    case DERControlEnum::HVMomCess:
    case DERControlEnum::HVMayTrip:
    case DERControlEnum::LFMustTrip:
    case DERControlEnum::LVMustTrip:
    case DERControlEnum::LVMomCess:
    case DERControlEnum::LVMayTrip:
    case DERControlEnum::PowerMonitoringMustTrip:
    case DERControlEnum::VoltVar:
    case DERControlEnum::VoltWatt:
    case DERControlEnum::WattPF:
    case DERControlEnum::WattVar:
        return req.curve.has_value() && validate_yunit(req.controlType, req.curve.value());
    }
    return false;
}

void DERControl::handle_set_der_control(ocpp::Call<SetDERControlRequest> call) {
    const auto& request = call.msg;
    SetDERControlResponse response;

    try {
        // R04.FR.01: Check if controlType is supported
        if (!this->is_control_type_supported(request.controlType)) {
            response.status = DERControlStatusEnum::NotSupported;
            ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // R04.FR.16-17: Validate control fields match controlType
        if (!this->validate_control_fields(request)) {
            response.status = DERControlStatusEnum::Rejected;
            response.statusInfo = StatusInfo();
            response.statusInfo->reasonCode = "InvalidValue";
            response.statusInfo->additionalInfo = "ControlFieldMismatch";
            ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // R04.FR.13: Default controls cannot have startTime or duration
        if (request.isDefault && has_start_time_or_duration(request)) {
            response.status = DERControlStatusEnum::Rejected;
            response.statusInfo = StatusInfo();
            response.statusInfo->reasonCode = "InvalidValue";
            response.statusInfo->additionalInfo = "DefaultWithStartTimeOrDuration";
            ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // R04.FR.15: EnterService and Gradients can only be default (not scheduled)
        if (!request.isDefault &&
            (request.controlType == DERControlEnum::EnterService || request.controlType == DERControlEnum::Gradients)) {
            response.status = DERControlStatusEnum::Rejected;
            response.statusInfo = StatusInfo();
            response.statusInfo->reasonCode = "InvalidValue";
            response.statusInfo->additionalInfo = "EnterServiceOrGradientsMustBeDefault";
            ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // Bound DER_CONTROLS table growth. An UPDATE of an existing row does not
        // increase row count, so it is always allowed.
        if (this->context.database_handler.count_der_controls() >= MAX_DER_CONTROLS &&
            !this->context.database_handler.get_der_control(request.controlId.get()).has_value()) {
            EVLOG_warning << "DER control table at cap (" << MAX_DER_CONTROLS
                          << "), rejecting new controlId=" << request.controlId.get();
            response.status = DERControlStatusEnum::Rejected;
            response.statusInfo = StatusInfo();
            response.statusInfo->reasonCode = "InvalidValue";
            response.statusInfo->additionalInfo = "DERControlLimitExceeded";
            ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        const int32_t priority = get_priority_from_request(request);
        const auto start_time = get_start_time_from_request(request);
        const auto duration = get_duration_from_request(request);

        std::vector<CiString<36>> superseded_ids;
        bool new_is_superseded = false;

        // FR.06 immediate vs FR.07 deferred supersede branches on the existing row's active state.
        // Immediate-supersedes are buffered so FR.08 (new row itself superseded) can cancel them.
        std::optional<std::string> deferred_supersede_target;
        std::vector<std::string> pending_immediate_supersedes;
        const auto now = ocpp::DateTime();
        const bool has_future_start =
            !request.isDefault && start_time.has_value() && ocpp::DateTime(start_time.value()) > now;
        const bool start_time_has_arrived =
            !request.isDefault && start_time.has_value() && ocpp::DateTime(start_time.value()) <= now;

        const auto control_type_str = v2::conversions::dercontrol_enum_to_string(request.controlType);

        // DB writes inside one transaction; notifications dispatched post-commit so CSMS sees durable state only.
        auto transaction = this->context.database_handler.begin_transaction();

        // Query existing controls of the same type and isDefault
        auto existing_jsons = this->context.database_handler.get_der_controls_matching_criteria(
            std::optional<bool>(request.isDefault), std::optional<std::string>(control_type_str), std::nullopt);

        // R04.FR.02-08: Handle priority-based superseding for existing controls
        for (const auto& existing_json : existing_jsons) {
            try {
                auto existing = json::parse(existing_json);
                const auto& existing_request = existing.at("request");
                auto existing_id = existing_request.at("controlId").get<std::string>();
                auto existing_is_default = existing_request.at("isDefault").get<bool>();
                auto existing_priority = existing.at("priority").get<int32_t>();
                // Strict at().get<bool>(): corruption surfaces to per-row catch, not silent false.
                auto existing_is_superseded = existing.at("isSuperseded").get<bool>();

                if (existing_id == request.controlId.get()) {
                    continue; // Same controlId is an update, not a supersede
                }
                if (existing_is_superseded) {
                    continue; // Already out of play; can neither supersede nor be superseded
                }

                // Determine whether `existing` is currently active. Defaults are not
                // "scheduled" in the time-windowed sense, so they never qualify for
                // deferred supersede: a new default with strictly lower priority value
                // immediately replaces an existing default.
                bool existing_is_currently_active = false;
                if (!existing_is_default && existing.contains("startTime")) {
                    ocpp::DateTime existing_start(existing.at("startTime").get<std::string>());
                    if (existing_start.to_time_point() <= now.to_time_point()) {
                        // Missing/non-numeric/non-finite duration => indefinite (still active).
                        const auto duration_it = existing.find("duration");
                        if (duration_it == existing.end() || !duration_it->is_number()) {
                            existing_is_currently_active = true;
                        } else {
                            const auto existing_duration = duration_it->get<float>();
                            if (std::isfinite(existing_duration)) {
                                const auto expiry = ocpp::DateTime(
                                    existing_start.to_time_point() +
                                    std::chrono::milliseconds(static_cast<int64_t>(existing_duration * 1000.0F)));
                                existing_is_currently_active = expiry.to_time_point() > now.to_time_point();
                            } else {
                                existing_is_currently_active = true;
                            }
                        }
                    }
                }

                // Strictly lower priority value overrules (R04.FR.03/.06); ties do not.
                if (priority < existing_priority) {
                    if (existing_is_currently_active && has_future_start) {
                        // R04.FR.07: deferred flip at new.startTime; CSMS told via later NotifyDERStartStop.
                        if (!deferred_supersede_target.has_value()) {
                            deferred_supersede_target = existing_id;
                        }
                    } else {
                        // R04.FR.03/.06: buffer; cancelled below if R04.FR.08 fires.
                        pending_immediate_supersedes.emplace_back(existing_id);
                    }
                } else if (priority > existing_priority) {
                    // R04.FR.08: new goes in already-superseded; response echoes new controlId.
                    new_is_superseded = true;
                }
            } catch (const json::exception& e) {
                EVLOG_warning << "Failed to parse existing DER control JSON: " << e.what();
            }
        }

        // R04.FR.08: cancel side effects of a new row that will never run.
        if (new_is_superseded) {
            pending_immediate_supersedes.clear();
            deferred_supersede_target.reset();
        } else {
            for (const auto& existing_id : pending_immediate_supersedes) {
                this->context.database_handler.update_der_control_superseded(existing_id, true);
                superseded_ids.emplace_back(existing_id);
            }
        }

        // Cap supersededIds to OCPP 2.1 §1.75.2/§1.48.1 0..24 before both DB persist and wire response,
        // so CONTROL_JSON.displacedIds and the FR.22 expiry-notify stay schema-valid.
        constexpr size_t MAX_SUPERSEDED_IDS = 24;
        if (superseded_ids.size() > MAX_SUPERSEDED_IDS) {
            superseded_ids.resize(MAX_SUPERSEDED_IDS);
        }

        // Top-level fields canonicalize values regardless of variant; displacedIds drives FR.22 expiry-notify.
        json control_json;
        control_json["priority"] = priority;
        control_json["isSuperseded"] = new_is_superseded;
        if (start_time.has_value()) {
            control_json["startTime"] = start_time.value();
        }
        if (duration.has_value()) {
            control_json["duration"] = duration.value();
        }
        control_json["request"] = json(request);
        json displaced_ids_json = json::array();
        for (const auto& id : superseded_ids) {
            displaced_ids_json.push_back(id.get());
        }
        control_json["displacedIds"] = displaced_ids_json;

        // Persist to database
        this->context.database_handler.insert_or_update_der_control(request.controlId.get(), request.isDefault,
                                                                    control_type_str, new_is_superseded, priority,
                                                                    start_time, duration, control_json.dump());

        // UPSERT preserves PENDING_SUPERSEDE_ID; rewrite or clear explicitly every Set.
        if (deferred_supersede_target.has_value()) {
            this->context.database_handler.set_der_control_pending_supersede(request.controlId.get(),
                                                                             deferred_supersede_target.value());
        } else {
            this->context.database_handler.clear_der_control_pending_supersede(request.controlId.get());
        }

        // Mark STARTED_NOTIFIED in-transaction so a crash before dispatch is recoverable (CSMS retry);
        // a crash after is harmless (DB already truthful).
        const bool will_notify_immediate_start = start_time_has_arrived && !new_is_superseded;
        if (will_notify_immediate_start) {
            this->context.database_handler.mark_der_control_started_notified(request.controlId.get());
        }

        transaction->commit();

        response.status = DERControlStatusEnum::Accepted;
        if (new_is_superseded) {
            // R04.FR.08
            response.supersededIds = std::vector<CiString<36>>{request.controlId};
        } else if (!superseded_ids.empty()) {
            response.supersededIds = superseded_ids;
        }

        ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);

        if (will_notify_immediate_start) {
            std::optional<std::vector<CiString<36>>> superseded_opt;
            if (!superseded_ids.empty()) {
                superseded_opt = superseded_ids;
            }
            this->send_notify_start_stop(request.controlId, true, ocpp::DateTime(), superseded_opt);
        }

        this->emit_active_directives();
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_error << "DER control database error in SetDERControl: " << e.what();
        response.status = DERControlStatusEnum::Rejected;
        response.statusInfo = StatusInfo();
        response.statusInfo->reasonCode = "InternalError";
        ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
    }
}

void DERControl::handle_get_der_control(ocpp::Call<GetDERControlRequest> call) {
    const auto& request = call.msg;
    GetDERControlResponse response;

    try {
        // R04.FR.36: If controlType specified and not supported → NotSupported
        if (request.controlType.has_value() && !this->is_control_type_supported(request.controlType.value())) {
            response.status = DERControlStatusEnum::NotSupported;
            ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // Build filter criteria from request
        std::optional<std::string> control_type_filter;
        if (request.controlType.has_value()) {
            control_type_filter = v2::conversions::dercontrol_enum_to_string(request.controlType.value());
        }
        std::optional<std::string> control_id_filter;
        if (request.controlId.has_value()) {
            control_id_filter = request.controlId->get();
        }

        // Wrap the DB read in a transaction so the GET handler observes a
        // consistent snapshot even if the scheduled-check timer is running on
        // another thread. SQLite's default isolation already prevents readers
        // from seeing uncommitted writes, but an explicit transaction also
        // pins the snapshot for any future read added to this handler.
        std::vector<std::string> matching;
        {
            auto transaction = this->context.database_handler.begin_transaction();
            matching = this->context.database_handler.get_der_controls_matching_criteria(
                request.isDefault, control_type_filter, control_id_filter);
            transaction->commit();
        }

        // R04.FR.30: No matching controls → NotFound
        if (matching.empty()) {
            response.status = DERControlStatusEnum::NotFound;
            ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // R04.FR.33-35, R04.FR.37: Return Accepted and send ReportDERControl
        response.status = DERControlStatusEnum::Accepted;
        ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);

        // Send report with matching controls
        this->send_report(request.requestId, matching);
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_error << "DER control database error in GetDERControl: " << e.what();
        response.status = DERControlStatusEnum::Rejected;
        response.statusInfo = StatusInfo();
        response.statusInfo->reasonCode = "InternalError";
        ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
    }
}

void DERControl::handle_clear_der_control(ocpp::Call<ClearDERControlRequest> call) {
    const auto& request = call.msg;
    ClearDERControlResponse response;

    try {
        // R04.FR.46: If controlId is specified, clear that specific control, but
        // R04.FR.42 requires the caller-supplied isDefault to match too. A controlId
        // that exists only under the opposite isDefault must return NotFound.
        if (request.controlId.has_value()) {
            bool deleted = this->context.database_handler.delete_der_control_by_id_and_default(request.controlId->get(),
                                                                                               request.isDefault);
            response.status = deleted ? DERControlStatusEnum::Accepted : DERControlStatusEnum::NotFound;
            ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            if (deleted) {
                this->emit_active_directives();
            }
            return;
        }

        // R04.FR.43: If controlType specified and not supported (no controlId) → NotSupported
        if (request.controlType.has_value() && !this->is_control_type_supported(request.controlType.value())) {
            response.status = DERControlStatusEnum::NotSupported;
            ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
            this->context.message_dispatcher.dispatch_call_result(call_result);
            return;
        }

        // R04.FR.44: No controlType, no controlId → clear all matching isDefault
        // R04.FR.45: controlType set, no controlId → clear by type and isDefault
        std::optional<std::string> control_type_filter;
        if (request.controlType.has_value()) {
            control_type_filter = v2::conversions::dercontrol_enum_to_string(request.controlType.value());
        }

        int deleted_count = this->context.database_handler.delete_der_controls_matching_criteria(request.isDefault,
                                                                                                 control_type_filter);

        // R04.FR.44 (no controlType, no controlId) is unconditionally Accepted, even
        // when there is nothing to delete. NotFound is reserved for FR.41
        // (controlType-with-no-match) and FR.42 (controlId-with-no-match).
        if (control_type_filter.has_value()) {
            response.status = (deleted_count > 0) ? DERControlStatusEnum::Accepted : DERControlStatusEnum::NotFound;
        } else {
            response.status = DERControlStatusEnum::Accepted;
        }

        ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);

        if (deleted_count > 0) {
            this->emit_active_directives();
        }
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_error << "DER control database error in ClearDERControl: " << e.what();
        response.status = DERControlStatusEnum::Rejected;
        response.statusInfo = StatusInfo();
        response.statusInfo->reasonCode = "InternalError";
        ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
    }
}

void DERControl::send_notify_start_stop(const CiString<36>& control_id, bool started, const ocpp::DateTime& timestamp,
                                        const std::optional<std::vector<CiString<36>>>& superseded_ids) {
    NotifyDERStartStopRequest req;
    req.controlId = control_id;
    req.started = started;
    req.timestamp = timestamp;
    req.supersededIds = superseded_ids;

    ocpp::Call<NotifyDERStartStopRequest> call(req);
    this->context.message_dispatcher.dispatch_call(call, false);
}

std::vector<SetDERControlRequest> DERControl::compute_active_directives() const {
    // No in-memory cache; recover the active set from the persisted CONTROL_JSON rows.
    auto controls =
        this->context.database_handler.get_der_controls_matching_criteria(std::nullopt, std::nullopt, std::nullopt);

    auto now = ocpp::DateTime();
    std::vector<SetDERControlRequest> active;
    active.reserve(controls.size());
    for (const auto& control_json_str : controls) {
        try {
            auto control = json::parse(control_json_str);
            if (control.at("isSuperseded").get<bool>()) {
                continue;
            }

            // A scheduled control is effective only between startTime and startTime+duration; a default
            // control (no startTime) is always effective.
            if (control.contains("startTime")) {
                auto start_time = ocpp::DateTime(control.at("startTime").get<std::string>());
                if (start_time > now) {
                    continue;
                }
                if (control.contains("duration")) {
                    auto duration_seconds = control.at("duration").get<float>();
                    if (std::isfinite(duration_seconds) && duration_seconds >= 0.0F) {
                        auto expiry_tp = start_time.to_time_point() +
                                         std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::duration<double>(static_cast<double>(duration_seconds)));
                        if (ocpp::DateTime(expiry_tp) <= now) {
                            continue;
                        }
                    }
                }
            }

            active.push_back(control.at("request").get<SetDERControlRequest>());
        } catch (const std::exception& e) {
            // Dropping a row under wholesale-replace means the provider stops enforcing a directive the
            // DB still holds active — possible DER state desync, so log at error. controlId is best-effort.
            std::string control_id = "<unknown>";
            try {
                control_id = json::parse(control_json_str).at("request").at("controlId").get<std::string>();
            } catch (const std::exception&) {
            }
            EVLOG_error << "Dropping malformed DER control row (controlId=" << control_id
                        << ") from the active directive set: " << e.what()
                        << " — enforcement of this directive stops, possible DER state desync";
        }
    }
    return active;
}

void DERControl::emit_active_directives() const {
    if (!this->active_directives_callback.has_value()) {
        return;
    }

    std::vector<SetDERControlRequest> active;
    try {
        active = this->compute_active_directives();
    } catch (const everest::db::QueryExecutionException& e) {
        EVLOG_error << "DER active-directives DB read failed: " << e.what()
                    << " — active directive set could not be recomputed and is now stale";
        return;
    }

    try {
        this->active_directives_callback.value()(active);
    } catch (const std::exception& e) {
        EVLOG_error << "DER active-directives callback threw: " << e.what();
    } catch (...) {
        EVLOG_error << "DER active-directives callback threw a non-std exception";
    }
}

void DERControl::republish_active_directives() const {
    this->emit_active_directives();
}

void DERControl::notify_der_alarm(const NotifyDERAlarmRequest& request) {
    ocpp::Call<NotifyDERAlarmRequest> call(request);
    this->context.message_dispatcher.dispatch_call(call, false);
}

void DERControl::check_scheduled_controls() {
    // DB writes inside the transaction; notifications queued and dispatched post-commit
    // so rollback discards them and CSMS never sees a notify for rolled-back state.
    // Reboot replay: FR.20/22 use timestamp = dispatch-time, so first post-boot run fires elapsed schedules.
    std::vector<NotifyDERStartStopRequest> pending_notifications;

    bool active_set_changed = false;

    try {
        auto transaction = this->context.database_handler.begin_transaction();

        auto now = ocpp::DateTime();

        // R04.FR.07: activate deferred supersedes whose new-control startTime arrived.
        // FR.21 requires only started=true on the new control; no paired started=false on existing
        // (that is reserved for FR.22 genuine expiry).
        auto activations = this->context.database_handler.get_der_control_pending_supersede_activations(now);
        for (const auto& activation : activations) {
            this->context.database_handler.update_der_control_superseded(activation.existing_id, true);
            this->context.database_handler.clear_der_control_pending_supersede(activation.new_id);
            this->context.database_handler.append_der_control_displaced_id(activation.new_id, activation.existing_id);
            this->context.database_handler.mark_der_control_started_notified(activation.new_id);

            NotifyDERStartStopRequest start_req;
            start_req.controlId = CiString<36>(activation.new_id);
            start_req.started = true;
            start_req.timestamp = now;
            start_req.supersededIds = std::optional<std::vector<CiString<36>>>{
                std::vector<CiString<36>>{CiString<36>(activation.existing_id)}};
            pending_notifications.push_back(std::move(start_req));
            active_set_changed = true;
        }

        // R04.FR.20/21: first-observation start-notify for non-default scheduled controls.
        auto to_start = this->context.database_handler.get_der_controls_needing_start_notify(now);
        for (const auto& control_id : to_start) {
            this->context.database_handler.mark_der_control_started_notified(control_id);
            NotifyDERStartStopRequest start_req;
            start_req.controlId = CiString<36>(control_id);
            start_req.started = true;
            start_req.timestamp = now;
            pending_notifications.push_back(std::move(start_req));
            active_set_changed = true;
        }

        auto controls = this->context.database_handler.get_der_controls_matching_criteria(std::optional<bool>(false),
                                                                                          std::nullopt, std::nullopt);

        for (const auto& control_json_str : controls) {
            try {
                auto control = json::parse(control_json_str);
                auto control_id = control.at("request").at("controlId").get<std::string>();
                bool is_superseded = control.at("isSuperseded").get<bool>();

                if (is_superseded) {
                    continue;
                }

                if (!control.contains("startTime") || !control.contains("duration")) {
                    continue;
                }

                auto start_str = control.at("startTime").get<std::string>();
                auto duration_seconds = control.at("duration").get<float>();
                if (!std::isfinite(duration_seconds) || duration_seconds < 0.0F) {
                    continue;
                }
                auto start_time = ocpp::DateTime(start_str);

                // bounded float→seconds, no UB on overflow
                auto start_tp = start_time.to_time_point();
                auto expiry_tp = start_tp + std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::duration<double>(static_cast<double>(duration_seconds)));
                auto expiry = ocpp::DateTime(expiry_tp);

                // R04.FR.10/.22
                if (expiry <= now) {
                    NotifyDERStartStopRequest stop_req;
                    stop_req.controlId = CiString<36>(control_id);
                    stop_req.started = false;
                    stop_req.timestamp = now;
                    // R04 §1.48.1 — supersededIds echo allowed on started=false (0..24 optional).
                    if (control.contains("displacedIds") && control.at("displacedIds").is_array() &&
                        !control.at("displacedIds").empty()) {
                        std::vector<CiString<36>> ids;
                        ids.reserve(control.at("displacedIds").size());
                        for (const auto& item : control.at("displacedIds")) {
                            if (item.is_string()) {
                                ids.emplace_back(item.get<std::string>());
                            }
                        }
                        if (!ids.empty()) {
                            stop_req.supersededIds = std::move(ids);
                        }
                    }
                    pending_notifications.push_back(std::move(stop_req));

                    this->context.database_handler.delete_der_control(control_id);
                    active_set_changed = true;
                    // DEBUG: routine, contains CSMS string
                    EVLOG_debug << "DER control " << control_id << " expired, deleted from database";
                }
            } catch (const std::exception& e) {
                // Catch std::exception (not json::exception): unparseable startTime throws TimePointParseException.
                EVLOG_warning << "Skipping malformed DER control row during scheduled check: " << e.what();
            }
        }

        transaction->commit();
    } catch (const std::exception& e) {
        EVLOG_error << "DER scheduled-control check aborted: " << e.what();
        return;
    }

    // TODO: persist notify_outbox if transient dispatch failure becomes a reliability concern.
    for (const auto& notification : pending_notifications) {
        try {
            ocpp::Call<NotifyDERStartStopRequest> call(notification);
            this->context.message_dispatcher.dispatch_call(call, false);
        } catch (const std::exception& e) {
            EVLOG_error << "DER scheduled-control notify dispatch failed: " << e.what();
        }
    }

    if (active_set_changed) {
        this->emit_active_directives();
    }
}

namespace {

constexpr size_t MAX_CONTROLS_PER_REPORT_MESSAGE = 10;
static_assert(MAX_CONTROLS_PER_REPORT_MESSAGE <= 24,
              "OCPP 2.1 caps each typed ReportDERControl field at 0..24 entries. "
              "Raising MAX_CONTROLS_PER_REPORT_MESSAGE above 24 requires a "
              "per-field-cap implementation in build_report.");

ReportDERControlRequest build_report(int32_t request_id, const std::vector<std::string>& control_jsons, bool tbc) {
    ReportDERControlRequest report;
    report.requestId = request_id;

    std::vector<DERCurveGet> curves;
    std::vector<EnterServiceGet> enter_services;
    std::vector<FixedPFGet> fixed_pf_absorbs;
    std::vector<FixedPFGet> fixed_pf_injects;
    std::vector<FixedVarGet> fixed_vars;
    std::vector<FreqDroopGet> freq_droops;
    std::vector<GradientGet> gradients;
    std::vector<LimitMaxDischargeGet> limit_max_discharges;

    for (const auto& control_json_str : control_jsons) {
        try {
            auto stored = json::parse(control_json_str);
            const auto request = stored.at("request").get<SetDERControlRequest>();
            const bool is_superseded = stored.at("isSuperseded").get<bool>();
            const auto control_id = request.controlId.get();
            const auto control_type = request.controlType;
            const bool is_default = request.isDefault;

            // Route each control into the appropriate *Get vector based on type
            switch (control_type) {
            case DERControlEnum::FixedPFAbsorb: {
                FixedPFGet get;
                get.fixedPF = request.fixedPFAbsorb.value();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                fixed_pf_absorbs.push_back(get);
                break;
            }
            case DERControlEnum::FixedPFInject: {
                FixedPFGet get;
                get.fixedPF = request.fixedPFInject.value();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                fixed_pf_injects.push_back(get);
                break;
            }
            case DERControlEnum::FixedVar: {
                FixedVarGet get;
                get.fixedVar = request.fixedVar.value();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                fixed_vars.push_back(get);
                break;
            }
            case DERControlEnum::FreqDroop: {
                FreqDroopGet get;
                get.freqDroop = request.freqDroop.value();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                freq_droops.push_back(get);
                break;
            }
            case DERControlEnum::EnterService: {
                EnterServiceGet get;
                get.enterService = request.enterService.value();
                get.id = control_id;
                enter_services.push_back(get);
                break;
            }
            case DERControlEnum::Gradients: {
                GradientGet get;
                get.gradient = request.gradient.value();
                get.id = control_id;
                gradients.push_back(get);
                break;
            }
            case DERControlEnum::LimitMaxDischarge: {
                LimitMaxDischargeGet get;
                get.limitMaxDischarge = request.limitMaxDischarge.value();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                limit_max_discharges.push_back(get);
                break;
            }
            // All curve-based types
            case DERControlEnum::FreqWatt:
            case DERControlEnum::HFMustTrip:
            case DERControlEnum::HFMayTrip:
            case DERControlEnum::HVMustTrip:
            case DERControlEnum::HVMomCess:
            case DERControlEnum::HVMayTrip:
            case DERControlEnum::LFMustTrip:
            case DERControlEnum::LVMustTrip:
            case DERControlEnum::LVMomCess:
            case DERControlEnum::LVMayTrip:
            case DERControlEnum::PowerMonitoringMustTrip:
            case DERControlEnum::VoltVar:
            case DERControlEnum::VoltWatt:
            case DERControlEnum::WattPF:
            case DERControlEnum::WattVar: {
                DERCurveGet get;
                get.curve = request.curve.value();
                get.id = control_id;
                get.curveType = control_type;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                curves.push_back(get);
                break;
            }
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Failed to parse stored DER control for report: " << e.what();
        }
    }

    if (!curves.empty()) {
        report.curve = curves;
    }
    if (!enter_services.empty()) {
        report.enterService = enter_services;
    }
    if (!fixed_pf_absorbs.empty()) {
        report.fixedPFAbsorb = fixed_pf_absorbs;
    }
    if (!fixed_pf_injects.empty()) {
        report.fixedPFInject = fixed_pf_injects;
    }
    if (!fixed_vars.empty()) {
        report.fixedVar = fixed_vars;
    }
    if (!freq_droops.empty()) {
        report.freqDroop = freq_droops;
    }
    if (!gradients.empty()) {
        report.gradient = gradients;
    }
    if (!limit_max_discharges.empty()) {
        report.limitMaxDischarge = limit_max_discharges;
    }

    // R04.FR.32: set tbc=true for all but the last message in a multi-message report
    if (tbc) {
        report.tbc = true;
    }

    return report;
}

} // anonymous namespace

void DERControl::send_report(int32_t request_id, const std::vector<std::string>& control_jsons) {
    // R04.FR.32: Split into multiple messages if we have more than MAX controls
    const size_t total = control_jsons.size();
    if (total == 0) {
        return;
    }

    size_t index = 0;
    while (index < total) {
        size_t chunk_end = std::min(index + MAX_CONTROLS_PER_REPORT_MESSAGE, total);
        bool is_last = (chunk_end == total);

        std::vector<std::string> chunk(control_jsons.begin() + static_cast<long>(index),
                                       control_jsons.begin() + static_cast<long>(chunk_end));

        // R04.FR.31: requestId set in every message; R04.FR.32: tbc=true except for last
        auto report = build_report(request_id, chunk, !is_last);

        ocpp::Call<ReportDERControlRequest> report_call(report);
        this->context.message_dispatcher.dispatch_call(report_call, false);

        index = chunk_end;
    }
}

} // namespace ocpp::v21
