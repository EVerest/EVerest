// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 - 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/cb_exi.hpp>

#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN charge parameter discovery messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize charge_parameter_discovery_req (DC)") {
        ChargeParameterDiscoveryRequest req;
        req.header.session_id = session_id;
        req.ev_requested_energy_transfer_type = datatypes::EnergyTransferMode::DC_extended;

        auto& dc = req.dc_ev_charge_parameter.emplace();
        dc.dc_ev_status.ev_ready = true;
        dc.dc_ev_status.ev_error_code = datatypes::DcEvErrorCode::NO_ERROR;
        dc.dc_ev_status.ev_ress_soc = 50;
        dc.ev_maximum_current_limit = 200.0;
        dc.ev_maximum_power_limit = 60000.0;
        dc.ev_maximum_voltage_limit = 500.0;
        dc.full_soc = 100;
        dc.bulk_soc = 80;

        THEN("It round-trips through the Variant") {
            const auto variant = roundtrip(req);

            REQUIRE(variant.get_type() == Type::ChargeParameterDiscoveryReq);
            const auto& msg = variant.get<ChargeParameterDiscoveryRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.ev_requested_energy_transfer_type == datatypes::EnergyTransferMode::DC_extended);
            REQUIRE(msg.dc_ev_charge_parameter.has_value());
            const auto& d = msg.dc_ev_charge_parameter.value();
            REQUIRE(d.dc_ev_status.ev_ready == true);
            REQUIRE(d.dc_ev_status.ev_ress_soc == 50);
            REQUIRE(d.ev_maximum_current_limit == 200.0);
            REQUIRE(d.ev_maximum_power_limit.value() == 60000.0);
            REQUIRE(d.ev_maximum_voltage_limit == 500.0);
            REQUIRE(d.full_soc.value() == 100);
            REQUIRE(d.bulk_soc.value() == 80);
        }
    }

    GIVEN("Serialize and deserialize charge_parameter_discovery_res (DC)") {
        ChargeParameterDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.evse_processing = datatypes::EvseProcessing::Ongoing;

        auto& dc = res.dc_evse_charge_parameter.emplace();
        dc.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        dc.dc_evse_status.notification_max_delay = 0;
        dc.dc_evse_status.evse_notification = datatypes::EvseNotification::None;
        dc.evse_maximum_current_limit = 300.0;
        dc.evse_maximum_power_limit = 60000.0;
        dc.evse_maximum_voltage_limit = 900.0;
        dc.evse_minimum_current_limit = 0.0;
        dc.evse_minimum_voltage_limit = 200.0;
        dc.evse_peak_current_ripple = 2.0;

        THEN("It round-trips through the Variant") {
            const auto variant = roundtrip(res);

            REQUIRE(variant.get_type() == Type::ChargeParameterDiscoveryRes);
            const auto& msg = variant.get<ChargeParameterDiscoveryResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.evse_processing == datatypes::EvseProcessing::Ongoing);
            REQUIRE(msg.dc_evse_charge_parameter.has_value());
            const auto& d = msg.dc_evse_charge_parameter.value();
            REQUIRE(d.dc_evse_status.evse_status_code == datatypes::DcEvseStatusCode::EVSE_Ready);
            REQUIRE(d.evse_maximum_current_limit == 300.0);
            REQUIRE(d.evse_maximum_power_limit.value() == 60000.0);
            REQUIRE(d.evse_maximum_voltage_limit == 900.0);
            REQUIRE(d.evse_minimum_voltage_limit == 200.0);
            REQUIRE(d.evse_peak_current_ripple == 2.0);
        }
    }
}

// The library's own encoder always writes RelativeTimeInterval, so a round trip cannot reach the
// other branch of the PMaxScheduleEntry choice. This builds that branch with the raw cbv2g
// encoder, which is the only way a real SECC's bytes could ever produce it.
SCENARIO("DIN charge_parameter_discovery_res with a TimeInterval schedule entry") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("a schedule whose only entry is on the TimeInterval branch") {
        din_exiDocument doc{};
        init_din_exiDocument(&doc);
        init_din_BodyType(&doc.V2G_Message.Body);
        std::copy(session_id.begin(), session_id.end(), doc.V2G_Message.Header.SessionID.bytes);
        doc.V2G_Message.Header.SessionID.bytesLen = static_cast<uint16_t>(session_id.size());

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
            const io::StreamInputView view{buffer, length};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ChargeParameterDiscoveryRes);
            const auto& msg = variant.get<ChargeParameterDiscoveryResponse>();
            REQUIRE(msg.sa_schedule_list.has_value());
            REQUIRE(msg.sa_schedule_list->size() == 1);
            REQUIRE(msg.sa_schedule_list->at(0).pmax_schedule.empty());
        }
    }
}
