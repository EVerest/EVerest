// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <everest/ocpp_module_common/conversions.hpp>

#include <generated/types/grid_support.hpp>
#include <ocpp/v2/ocpp_enums.hpp>
#include <ocpp/v21/messages/NotifyDERAlarm.hpp>
#include <ocpp/v21/messages/SetDERControl.hpp>

namespace {

namespace gs = types::grid_support;

TEST(GridSupportConversions, VoltVarRequestPopulatesOnlyCurve) {
    ocpp::v21::SetDERControlRequest request;
    request.isDefault = true;
    request.controlId = "ctrl-1";
    request.controlType = ocpp::v2::DERControlEnum::VoltVar;

    ocpp::v2::DERCurve curve;
    curve.priority = 7;
    curve.yUnit = ocpp::v2::DERUnitEnum::PctMaxVar;
    curve.curveData.push_back(ocpp::v2::DERCurvePoints{1.0f, 2.0f, std::nullopt});
    request.curve = curve;

    const auto directive =
        ocpp_module_common::conversions::to_grid_support_directive(request, "OCPP", "2026-06-16T00:00:00Z");

    EXPECT_EQ(directive.directive_type, gs::DirectiveType::VoltVar);
    EXPECT_EQ(directive.id, "ctrl-1");
    EXPECT_TRUE(directive.is_default);
    EXPECT_EQ(directive.priority, 7);
    EXPECT_EQ(directive.source, "OCPP");
    EXPECT_EQ(directive.received_at, "2026-06-16T00:00:00Z");

    ASSERT_TRUE(directive.curve.has_value());
    EXPECT_EQ(directive.curve->y_unit, gs::DERUnit::PctMaxVar);
    ASSERT_EQ(directive.curve->curve_data.size(), 1u);
    EXPECT_FLOAT_EQ(directive.curve->curve_data.at(0).x, 1.0f);
    EXPECT_FLOAT_EQ(directive.curve->curve_data.at(0).y, 2.0f);

    // exactly the curve payload is populated
    EXPECT_FALSE(directive.freq_droop.has_value());
    EXPECT_FALSE(directive.enter_service.has_value());
    EXPECT_FALSE(directive.fixed_pf.has_value());
    EXPECT_FALSE(directive.fixed_var.has_value());
    EXPECT_FALSE(directive.gradient.has_value());
    EXPECT_FALSE(directive.limit_max_discharge.has_value());
}

// Builds a minimal VoltVar curve request carrying the given fractional-seconds duration.
gs::Directive directive_with_curve_duration(float duration_s) {
    ocpp::v21::SetDERControlRequest request;
    request.controlId = "ctrl-dur";
    request.controlType = ocpp::v2::DERControlEnum::VoltVar;

    ocpp::v2::DERCurve curve;
    curve.priority = 1;
    curve.yUnit = ocpp::v2::DERUnitEnum::PctMaxVar;
    curve.curveData.push_back(ocpp::v2::DERCurvePoints{1.0f, 2.0f, std::nullopt});
    curve.duration = duration_s;
    request.curve = curve;

    return ocpp_module_common::conversions::to_grid_support_directive(request, "OCPP", "2026-06-16T00:00:00Z");
}

// A NaN duration never expires in libocpp, so duration_s is left unset (unbounded).
TEST(GridSupportConversions, NanDurationLeavesDurationUnset) {
    const auto directive = directive_with_curve_duration(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FALSE(directive.duration_s.has_value());
}

// A negative duration never expires in libocpp, so duration_s is left unset (unbounded).
TEST(GridSupportConversions, NegativeDurationLeavesDurationUnset) {
    const auto directive = directive_with_curve_duration(-5.0f);
    EXPECT_FALSE(directive.duration_s.has_value());
}

// An out-of-int-range duration is clamped to the max int rather than triggering UB.
TEST(GridSupportConversions, OverflowingDurationClampsToIntMax) {
    const auto directive = directive_with_curve_duration(1e12f);
    ASSERT_TRUE(directive.duration_s.has_value());
    EXPECT_EQ(directive.duration_s.value(), std::numeric_limits<int>::max());
}

// A fractional duration rounds to the nearest second (llround), not truncation.
TEST(GridSupportConversions, FractionalDurationRoundsToNearestSecond) {
    const auto directive = directive_with_curve_duration(90.7f);
    ASSERT_TRUE(directive.duration_s.has_value());
    EXPECT_EQ(directive.duration_s.value(), 91);
}

TEST(GridSupportConversions, FreqDroopRequestPopulatesOnlyFreqDroop) {
    ocpp::v21::SetDERControlRequest request;
    request.isDefault = false;
    request.controlId = "ctrl-2";
    request.controlType = ocpp::v2::DERControlEnum::FreqDroop;

    ocpp::v2::FreqDroop freq_droop;
    freq_droop.priority = 3;
    freq_droop.overFreq = 50.2f;
    freq_droop.underFreq = 49.8f;
    freq_droop.overDroop = 0.04f;
    freq_droop.underDroop = 0.05f;
    freq_droop.responseTime = 1.5f;
    request.freqDroop = freq_droop;

    const auto directive =
        ocpp_module_common::conversions::to_grid_support_directive(request, "OCPP", "2026-06-16T00:00:00Z");

    EXPECT_EQ(directive.directive_type, gs::DirectiveType::FreqDroop);
    EXPECT_FALSE(directive.is_default);
    EXPECT_EQ(directive.priority, 3);

    ASSERT_TRUE(directive.freq_droop.has_value());
    EXPECT_FLOAT_EQ(directive.freq_droop->over_freq, 50.2f);
    EXPECT_FLOAT_EQ(directive.freq_droop->under_freq, 49.8f);
    EXPECT_FLOAT_EQ(directive.freq_droop->over_droop, 0.04f);
    EXPECT_FLOAT_EQ(directive.freq_droop->under_droop, 0.05f);
    EXPECT_FLOAT_EQ(directive.freq_droop->response_time, 1.5f);

    // only freq_droop populated
    EXPECT_FALSE(directive.curve.has_value());
    EXPECT_FALSE(directive.enter_service.has_value());
    EXPECT_FALSE(directive.fixed_pf.has_value());
    EXPECT_FALSE(directive.fixed_var.has_value());
    EXPECT_FALSE(directive.gradient.has_value());
    EXPECT_FALSE(directive.limit_max_discharge.has_value());
}

TEST(GridSupportConversions, GridAlarmMapsToNotifyDerAlarm) {
    gs::GridAlarm alarm;
    alarm.fault = gs::GridEventFault::OverVoltage;
    alarm.alarm_ended = true;
    alarm.directive_type = gs::DirectiveType::VoltVar;
    alarm.timestamp = "2026-06-16T00:00:00Z";
    alarm.extra_info = "details";

    const auto notify = ocpp_module_common::conversions::to_ocpp_notify_der_alarm(alarm);

    EXPECT_EQ(notify.controlType, ocpp::v2::DERControlEnum::VoltVar);
    ASSERT_TRUE(notify.gridEventFault.has_value());
    EXPECT_EQ(notify.gridEventFault.value(), ocpp::v2::GridEventFaultEnum::OverVoltage);
    ASSERT_TRUE(notify.alarmEnded.has_value());
    EXPECT_TRUE(notify.alarmEnded.value());
    ASSERT_TRUE(notify.extraInfo.has_value());
    EXPECT_EQ(notify.extraInfo.value().get(), "details");
}

TEST(GridSupportConversions, GridAlarmWithoutDirectiveTypeThrows) {
    gs::GridAlarm alarm;
    alarm.fault = gs::GridEventFault::OverVoltage;
    alarm.alarm_ended = false;
    alarm.timestamp = "2026-06-16T00:00:00Z";
    // directive_type intentionally unset

    EXPECT_THROW(ocpp_module_common::conversions::to_ocpp_notify_der_alarm(alarm), std::invalid_argument);
}

TEST(GridSupportConversions, FixedPfInjectRequestPopulatesFixedPf) {
    ocpp::v21::SetDERControlRequest request;
    request.isDefault = false;
    request.controlId = "ctrl-pf";
    request.controlType = ocpp::v2::DERControlEnum::FixedPFInject;

    ocpp::v2::FixedPF fixed_pf;
    fixed_pf.priority = 4;
    fixed_pf.displacement = 0.95f;
    fixed_pf.excitation = false;
    request.fixedPFInject = fixed_pf;

    const auto directive =
        ocpp_module_common::conversions::to_grid_support_directive(request, "OCPP", "2026-06-16T00:00:00Z");

    EXPECT_EQ(directive.directive_type, gs::DirectiveType::FixedPFInject);
    EXPECT_EQ(directive.priority, 4);
    ASSERT_TRUE(directive.fixed_pf.has_value());
    EXPECT_FLOAT_EQ(directive.fixed_pf->displacement, 0.95f);
    EXPECT_FALSE(directive.fixed_pf->excitation);

    EXPECT_FALSE(directive.curve.has_value());
    EXPECT_FALSE(directive.fixed_var.has_value());
}

TEST(GridSupportConversions, LimitMaxDischargeRoundTripsNestedCurve) {
    ocpp::v21::SetDERControlRequest request;
    request.isDefault = false;
    request.controlId = "ctrl-lmd";
    request.controlType = ocpp::v2::DERControlEnum::LimitMaxDischarge;

    ocpp::v2::DERCurve must_trip;
    must_trip.priority = 9;
    must_trip.yUnit = ocpp::v2::DERUnitEnum::PctMaxW;
    must_trip.curveData.push_back(ocpp::v2::DERCurvePoints{10.0f, 20.0f, std::nullopt});

    ocpp::v2::LimitMaxDischarge limit;
    limit.priority = 6;
    limit.pctMaxDischargePower = 50.0f;
    limit.powerMonitoringMustTrip = must_trip;
    request.limitMaxDischarge = limit;

    const auto directive =
        ocpp_module_common::conversions::to_grid_support_directive(request, "OCPP", "2026-06-16T00:00:00Z");

    EXPECT_EQ(directive.directive_type, gs::DirectiveType::LimitMaxDischarge);
    EXPECT_EQ(directive.priority, 6);
    ASSERT_TRUE(directive.limit_max_discharge.has_value());
    EXPECT_FLOAT_EQ(directive.limit_max_discharge->pct_max_discharge_power.value(), 50.0f);

    ASSERT_TRUE(directive.limit_max_discharge->power_monitoring_must_trip.has_value());
    const auto& nested = directive.limit_max_discharge->power_monitoring_must_trip.value();
    // The nested must-trip curve inherits the directive's priority; a distinct curve priority is not represented.
    EXPECT_EQ(nested.y_unit, gs::DERUnit::PctMaxW);
    ASSERT_EQ(nested.curve_data.size(), 1u);
    EXPECT_FLOAT_EQ(nested.curve_data.at(0).x, 10.0f);
    EXPECT_FLOAT_EQ(nested.curve_data.at(0).y, 20.0f);
}

ocpp::v21::SetDERControlRequest make_control(const std::string& control_id, ocpp::v2::DERControlEnum control_type) {
    ocpp::v21::SetDERControlRequest request;
    request.controlId = control_id;
    request.controlType = control_type;
    return request;
}

TEST(GridSupportConversions, TranslateActiveControlsSkipsUnmappedAndContinues) {
    const std::vector<ocpp::v21::SetDERControlRequest> controls{
        make_control("good-1", ocpp::v2::DERControlEnum::VoltVar),
        make_control("bad", ocpp::v2::DERControlEnum::FreqDroop),
        make_control("good-2", ocpp::v2::DERControlEnum::VoltWatt),
    };

    // Throws for the offending control, mirroring to_grid_support_directive's std::out_of_range.
    const auto translate = [](const ocpp::v21::SetDERControlRequest& control) -> gs::Directive {
        if (control.controlId.get() == "bad") {
            throw std::out_of_range("unmapped DERControlEnum");
        }
        gs::Directive directive;
        directive.id = control.controlId.get();
        directive.directive_type = ocpp_module_common::conversions::to_grid_support_directive_type(control.controlType);
        return directive;
    };

    const auto directives = ocpp_module_common::conversions::translate_active_controls(controls, translate);

    ASSERT_EQ(directives.size(), 2u);
    EXPECT_EQ(directives.at(0).id, "good-1");
    EXPECT_EQ(directives.at(0).directive_type, gs::DirectiveType::VoltVar);
    EXPECT_EQ(directives.at(1).id, "good-2");
    EXPECT_EQ(directives.at(1).directive_type, gs::DirectiveType::VoltWatt);
}

} // namespace
