// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/response_variant.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

namespace {

const datatypes::SessionId SESSION_ID = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

template <typename Message> ResponseVariant roundtrip(const Message& message) {
    const auto serialized = serialize_helper(message);
    return ResponseVariant{io::StreamInputView{serialized.data(), serialized.size()}};
}

template <typename Response> const Response& expect(const ResponseVariant& variant) {
    REQUIRE(variant.get_type() == TypeTrait<Response>::type);
    const auto* msg = variant.get_if<Response>();
    REQUIRE(msg != nullptr);
    REQUIRE(msg->header.session_id == SESSION_ID);
    return *msg;
}

template <typename Response> Response response(datatypes::ResponseCode code = datatypes::ResponseCode::OK) {
    Response res{};
    res.header.session_id = SESSION_ID;
    res.response_code = code;
    return res;
}

} // namespace

SCENARIO("Decode DIN responses through the ResponseVariant") {

    GIVEN("A session_setup_res") {
        auto res = response<SessionSetupResponse>(datatypes::ResponseCode::OK_NewSessionEstablished);
        res.evse_id = {0x01, 0x02, 0x03, 0x04};

        THEN("It decodes as a SessionSetupRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<SessionSetupResponse>(variant);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK_NewSessionEstablished);
            REQUIRE(msg.evse_id == res.evse_id);
        }
    }

    GIVEN("A service_discovery_res") {
        auto res = response<ServiceDiscoveryResponse>();
        res.payment_options = {datatypes::PaymentOption::ExternalPayment};
        res.charge_service.service_tag.service_id = 1;
        res.charge_service.service_tag.service_category = datatypes::ServiceCategory::EVCharging;
        res.charge_service.energy_transfer_type = datatypes::SupportedEnergyTransferMode::DC_extended;

        THEN("It decodes as a ServiceDiscoveryRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<ServiceDiscoveryResponse>(variant).charge_service.energy_transfer_type ==
                    datatypes::SupportedEnergyTransferMode::DC_extended);
        }
    }

    GIVEN("A service_payment_selection_res") {
        const auto res = response<ServicePaymentSelectionResponse>(datatypes::ResponseCode::FAILED);

        THEN("It decodes as a ServicePaymentSelectionRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<ServicePaymentSelectionResponse>(variant).response_code == datatypes::ResponseCode::FAILED);
        }
    }

    GIVEN("A contract_authentication_res") {
        auto res = response<ContractAuthenticationResponse>();
        res.evse_processing = datatypes::EvseProcessing::Ongoing;

        THEN("It decodes as a ContractAuthenticationRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<ContractAuthenticationResponse>(variant).evse_processing ==
                    datatypes::EvseProcessing::Ongoing);
        }
    }

    GIVEN("A charge_parameter_discovery_res") {
        auto res = response<ChargeParameterDiscoveryResponse>();
        res.evse_processing = datatypes::EvseProcessing::Finished;
        auto& dc = res.dc_evse_charge_parameter.emplace();
        dc.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        dc.evse_maximum_current_limit = 300.0;
        dc.evse_maximum_voltage_limit = 900.0;
        dc.evse_minimum_current_limit = 0.0;
        dc.evse_minimum_voltage_limit = 200.0;
        dc.evse_peak_current_ripple = 2.0;

        THEN("It decodes as a ChargeParameterDiscoveryRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<ChargeParameterDiscoveryResponse>(variant);
            REQUIRE(msg.dc_evse_charge_parameter.has_value());
            REQUIRE(msg.dc_evse_charge_parameter->evse_maximum_voltage_limit == 900.0);
        }
    }

    GIVEN("A cable_check_res") {
        auto res = response<CableCheckResponse>();
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
        res.evse_processing = datatypes::EvseProcessing::Ongoing;

        THEN("It decodes as a CableCheckRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<CableCheckResponse>(variant).dc_evse_status.evse_status_code ==
                    datatypes::DcEvseStatusCode::EVSE_IsolationMonitoringActive);
        }
    }

    GIVEN("A pre_charge_res") {
        auto res = response<PreChargeResponse>();
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 399.0;

        THEN("It decodes as a PreChargeRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<PreChargeResponse>(variant).evse_present_voltage == 399.0);
        }
    }

    GIVEN("A power_delivery_res") {
        auto res = response<PowerDeliveryResponse>();
        res.dc_evse_status.emplace().evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;

        THEN("It decodes as a PowerDeliveryRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<PowerDeliveryResponse>(variant).dc_evse_status.has_value());
        }
    }

    GIVEN("A current_demand_res") {
        auto res = response<CurrentDemandResponse>();
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 419.0;
        res.evse_present_current = 124.0;

        THEN("It decodes as a CurrentDemandRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<CurrentDemandResponse>(variant).evse_present_current == 124.0);
        }
    }

    GIVEN("A welding_detection_res") {
        auto res = response<WeldingDetectionResponse>();
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 10.0;

        THEN("It decodes as a WeldingDetectionRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<WeldingDetectionResponse>(variant).evse_present_voltage == 10.0);
        }
    }

    GIVEN("A session_stop_res") {
        const auto res = response<SessionStopResponse>();

        THEN("It decodes as a SessionStopRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<SessionStopResponse>(variant).response_code == datatypes::ResponseCode::OK);
        }
    }
}

SCENARIO("The DIN ResponseVariant refuses what is not a response") {

    GIVEN("A session_stop_req") {
        SessionStopRequest req;
        req.header.session_id = SESSION_ID;

        THEN("No message is decoded and the error says why") {
            const auto variant = roundtrip(req);
            REQUIRE(variant.get_type() == Type::None);
            REQUIRE(variant.get_if<SessionStopRequest>() == nullptr);
            REQUIRE_FALSE(variant.get_error().empty());
        }
    }

    GIVEN("Bytes that are not EXI") {
        const std::vector<uint8_t> garbage{0xFF, 0xFF, 0xFF, 0xFF};

        THEN("No message is decoded and the error says why") {
            const ResponseVariant variant{io::StreamInputView{garbage.data(), garbage.size()}};
            REQUIRE(variant.get_type() == Type::None);
            REQUIRE_FALSE(variant.get_error().empty());
        }
    }
}

// The library's own encoder always writes RelativeTimeInterval, so a round trip cannot reach the
// other branch of the PMaxScheduleEntry choice. This builds that branch with the raw cbv2g
// encoder, which is the only way a real SECC's bytes could ever produce it.
SCENARIO("DIN charge_parameter_discovery_res with a TimeInterval schedule entry") {

    GIVEN("a schedule whose only entry is on the TimeInterval branch") {
        din_exiDocument doc{};
        init_din_exiDocument(&doc);
        init_din_BodyType(&doc.V2G_Message.Body);
        std::copy(SESSION_ID.begin(), SESSION_ID.end(), doc.V2G_Message.Header.SessionID.bytes);
        doc.V2G_Message.Header.SessionID.bytesLen = static_cast<uint16_t>(SESSION_ID.size());

        auto& res = doc.V2G_Message.Body.ChargeParameterDiscoveryRes;
        init_din_ChargeParameterDiscoveryResType(&res);
        doc.V2G_Message.Body.ChargeParameterDiscoveryRes_isUsed = 1u;
        res.ResponseCode = din_responseCodeType_OK;
        res.EVSEProcessing = din_EVSEProcessingType_Finished;

        auto& tuple = res.SAScheduleList.SAScheduleTuple.array[0];
        init_din_SAScheduleTupleType(&tuple);
        tuple.SAScheduleTupleID = 1;
        tuple.PMaxSchedule.PMaxScheduleID = 1;

        auto& entry = tuple.PMaxSchedule.PMaxScheduleEntry.array[0];
        init_din_PMaxScheduleEntryType(&entry);
        // TimeInterval carries no content: din_IntervalType is the abstract, empty type.
        entry.TimeInterval_isUsed = 1u;
        entry.PMax = 42;
        tuple.PMaxSchedule.PMaxScheduleEntry.arrayLen = 1;

        res.SAScheduleList.SAScheduleTuple.arrayLen = 1;
        res.SAScheduleList_isUsed = 1u;

        uint8_t buffer[1024];
        auto out = get_exi_output_stream(io::StreamOutputView{buffer, sizeof(buffer)});
        REQUIRE(encode_din_exiDocument(&out, &doc) == 0);
        const auto length = exi_bitstream_get_length(&out);

        THEN("the entry is dropped rather than decoded from uninitialised memory") {
            const ResponseVariant variant{io::StreamInputView{buffer, length}};

            const auto& msg = expect<ChargeParameterDiscoveryResponse>(variant);
            REQUIRE(msg.sa_schedule_list.has_value());
            REQUIRE(msg.sa_schedule_list->size() == 1);
            REQUIRE(msg.sa_schedule_list->at(0).pmax_schedule.empty());
        }
    }
}
