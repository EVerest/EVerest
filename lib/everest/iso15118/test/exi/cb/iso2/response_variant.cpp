// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/response_variant.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/welding_detection.hpp>

#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

namespace {

const SessionId SESSION_ID = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

template <typename Message> message_2::ResponseVariant roundtrip(const Message& message) {
    const auto serialized = serialize_helper(message);
    return message_2::ResponseVariant{io::StreamInputView{serialized.data(), serialized.size()}};
}

template <typename Response> const Response& expect(const message_2::ResponseVariant& variant) {
    REQUIRE(variant.get_type() == message_2::TypeTrait<Response>::type);
    const auto* msg = variant.get_if<Response>();
    REQUIRE(msg != nullptr);
    REQUIRE(msg->header.session_id == SESSION_ID);
    return *msg;
}

template <typename Response> Response response(ResponseCode code = ResponseCode::OK) {
    Response res{};
    res.header.session_id = SESSION_ID;
    res.response_code = code;
    return res;
}

} // namespace

SCENARIO("Decode ISO-2 responses through the ResponseVariant") {

    GIVEN("A session_setup_res") {
        auto res = response<message_2::SessionSetupResponse>(ResponseCode::OK_NewSessionEstablished);
        res.evse_id = "DE*PNX*E12345*1";

        THEN("It decodes as a SessionSetupRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<message_2::SessionSetupResponse>(variant);
            REQUIRE(msg.response_code == ResponseCode::OK_NewSessionEstablished);
            REQUIRE(msg.evse_id == "DE*PNX*E12345*1");
        }
    }

    GIVEN("A service_discovery_res") {
        auto res = response<message_2::ServiceDiscoveryResponse>();
        res.payment_option_list = {PaymentOption::ExternalPayment};
        res.charge_service.service_id = 1;
        res.charge_service.service_category = ServiceCategory::EVCharging;
        res.charge_service.supported_energy_transfer_mode = {EnergyTransferMode::DC_extended};

        THEN("It decodes as a ServiceDiscoveryRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<message_2::ServiceDiscoveryResponse>(variant);
            REQUIRE(msg.payment_option_list.size() == 1);
            REQUIRE(msg.charge_service.service_id == 1);
        }
    }

    GIVEN("A service_detail_res") {
        auto res = response<message_2::ServiceDetailResponse>();
        res.service_id = 42;

        THEN("It decodes as a ServiceDetailRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<message_2::ServiceDetailResponse>(variant).service_id == 42);
        }
    }

    GIVEN("A payment_service_selection_res") {
        const auto res = response<message_2::PaymentServiceSelectionResponse>(ResponseCode::FAILED);

        THEN("It decodes as a PaymentServiceSelectionRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<message_2::PaymentServiceSelectionResponse>(variant).response_code == ResponseCode::FAILED);
        }
    }

    GIVEN("A payment_details_res") {
        auto res = response<message_2::PaymentDetailsResponse>();
        res.gen_challenge.fill(0x5A);
        res.evse_timestamp = 1739635913;

        THEN("It decodes as a PaymentDetailsRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<message_2::PaymentDetailsResponse>(variant);
            REQUIRE(msg.gen_challenge == res.gen_challenge);
            REQUIRE(msg.evse_timestamp == 1739635913);
        }
    }

    GIVEN("An authorization_res") {
        auto res = response<message_2::AuthorizationResponse>();
        res.evse_processing = EVSEProcessing::Ongoing;

        THEN("It decodes as an AuthorizationRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<message_2::AuthorizationResponse>(variant).evse_processing == EVSEProcessing::Ongoing);
        }
    }

    GIVEN("A charge_parameter_discovery_res") {
        auto res = response<message_2::ChargeParameterDiscoveryResponse>();
        res.evse_processing = EVSEProcessing::Finished;
        auto& ac = res.ac_evse_charge_parameter.emplace();
        ac.ac_evse_status = {0, EVSENotification::None, false};
        ac.evse_nominal_voltage = to_physical_value(230, Unit::V);
        ac.evse_max_current = to_physical_value(32, Unit::A);

        THEN("It decodes as a ChargeParameterDiscoveryRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<message_2::ChargeParameterDiscoveryResponse>(variant);
            REQUIRE(msg.ac_evse_charge_parameter.has_value());
            REQUIRE(from_physical_value(msg.ac_evse_charge_parameter->evse_max_current) == 32);
        }
    }

    GIVEN("A power_delivery_res") {
        auto res = response<message_2::PowerDeliveryResponse>();
        res.ac_evse_status = AC_EVSEStatus{0, EVSENotification::StopCharging, false};

        THEN("It decodes as a PowerDeliveryRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<message_2::PowerDeliveryResponse>(variant);
            REQUIRE(msg.ac_evse_status.has_value());
            REQUIRE(msg.ac_evse_status->notification == EVSENotification::StopCharging);
        }
    }

    GIVEN("A charging_status_res") {
        auto res = response<message_2::ChargingStatusResponse>();
        res.evse_id = "DE*PNX*E12345*1";
        res.sa_schedule_tuple_id = 1;
        res.ac_evse_status = {0, EVSENotification::None, false};

        THEN("It decodes as a ChargingStatusRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<message_2::ChargingStatusResponse>(variant).evse_id == "DE*PNX*E12345*1");
        }
    }

    GIVEN("A cable_check_res") {
        auto res = response<message_2::CableCheckResponse>();
        res.dc_evse_status = {0, EVSENotification::None, IsolationLevel::Valid, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_processing = EVSEProcessing::Ongoing;

        THEN("It decodes as a CableCheckRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<message_2::CableCheckResponse>(variant).evse_processing == EVSEProcessing::Ongoing);
        }
    }

    GIVEN("A pre_charge_res") {
        auto res = response<message_2::PreChargeResponse>();
        res.dc_evse_status = {0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(399, Unit::V);

        THEN("It decodes as a PreChargeRes") {
            const auto variant = roundtrip(res);
            REQUIRE(from_physical_value(expect<message_2::PreChargeResponse>(variant).evse_present_voltage) == 399);
        }
    }

    GIVEN("A current_demand_res") {
        auto res = response<message_2::CurrentDemandResponse>();
        res.dc_evse_status = {0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(400, Unit::V);
        res.evse_present_current = to_physical_value(79, Unit::A);
        res.evse_id = "DE*PNX*E12345*1";
        res.sa_schedule_tuple_id = 1;

        THEN("It decodes as a CurrentDemandRes") {
            const auto variant = roundtrip(res);
            REQUIRE(from_physical_value(expect<message_2::CurrentDemandResponse>(variant).evse_present_current) == 79);
        }
    }

    GIVEN("A welding_detection_res") {
        auto res = response<message_2::WeldingDetectionResponse>();
        res.dc_evse_status = {0, EVSENotification::None, std::nullopt, DC_EVSEStatusCode::EVSE_Ready};
        res.evse_present_voltage = to_physical_value(42, Unit::V);

        THEN("It decodes as a WeldingDetectionRes") {
            const auto variant = roundtrip(res);
            REQUIRE(from_physical_value(expect<message_2::WeldingDetectionResponse>(variant).evse_present_voltage) ==
                    42);
        }
    }

    GIVEN("A session_stop_res") {
        const auto res = response<message_2::SessionStopResponse>();

        THEN("It decodes as a SessionStopRes") {
            const auto variant = roundtrip(res);
            REQUIRE(expect<message_2::SessionStopResponse>(variant).response_code == ResponseCode::OK);
        }
    }

    GIVEN("A metering_receipt_res") {
        auto res = response<message_2::MeteringReceiptResponse>(ResponseCode::FAILED_MeteringSignatureNotValid);
        res.ac_evse_status = AC_EVSEStatus{0, EVSENotification::None, false};

        THEN("It decodes as a MeteringReceiptRes") {
            const auto variant = roundtrip(res);
            const auto& msg = expect<message_2::MeteringReceiptResponse>(variant);
            REQUIRE(msg.response_code == ResponseCode::FAILED_MeteringSignatureNotValid);
            REQUIRE(msg.ac_evse_status.has_value());
        }
    }

    GIVEN("A certificate_installation_res") {
        auto res = response<message_2::CertificateInstallationResponse>();
        res.sa_provisioning_chain.certificate = {0x30, 0x82, 0x01, 0x02};
        res.contract_chain.certificate = {0x30, 0x82, 0x03, 0x04};
        res.encrypted_private_key = {0xAA, 0xBB, 0xCC};
        res.dh_public_key = {0x04, 0x01, 0x02};
        res.emaid = "DEPNX123456789";

        const auto serialized = serialize_helper(res);
        const message_2::ResponseVariant variant{io::StreamInputView{serialized.data(), serialized.size()}};

        THEN("It decodes in full, and keeps the raw EXI the CPS signature is checked over") {
            const auto& msg = expect<message_2::CertificateInstallationResponse>(variant);
            REQUIRE(msg.contract_chain.certificate == res.contract_chain.certificate);
            REQUIRE(msg.emaid == "DEPNX123456789");
            REQUIRE(variant.get_exi_payload() == serialized);
        }
    }
}

SCENARIO("The ISO-2 ResponseVariant refuses what is not a response") {

    GIVEN("A session_setup_req") {
        message_2::SessionSetupRequest req;
        req.header.session_id = SESSION_ID;
        req.evcc_id = {0x00, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};

        THEN("No message is decoded and the error says why") {
            const auto variant = roundtrip(req);
            REQUIRE(variant.get_type() == message_2::Type::None);
            REQUIRE(variant.get_if<message_2::SessionSetupRequest>() == nullptr);
            REQUIRE_FALSE(variant.get_error().empty());
        }
    }

    GIVEN("Bytes that are not EXI") {
        const std::vector<uint8_t> garbage{0xFF, 0xFF, 0xFF, 0xFF};

        THEN("No message is decoded and the error says why") {
            const message_2::ResponseVariant variant{io::StreamInputView{garbage.data(), garbage.size()}};
            REQUIRE(variant.get_type() == message_2::Type::None);
            REQUIRE_FALSE(variant.get_error().empty());
        }
    }
}

// The library's own encoder always writes RelativeTimeInterval, so a round trip cannot reach the
// other branch of the PMaxScheduleEntry choice. This builds that branch with the raw cbv2g
// encoder, which is the only way a real SECC's bytes could ever produce it.
SCENARIO("ISO-2 charge_parameter_discovery_res with a TimeInterval schedule entry") {

    GIVEN("a schedule whose only entry is on the TimeInterval branch") {
        iso2_exiDocument doc{};
        init_iso2_exiDocument(&doc);
        init_iso2_MessageHeaderType(&doc.V2G_Message.Header);
        init_iso2_BodyType(&doc.V2G_Message.Body);
        std::copy(SESSION_ID.begin(), SESSION_ID.end(), doc.V2G_Message.Header.SessionID.bytes);
        doc.V2G_Message.Header.SessionID.bytesLen = static_cast<uint16_t>(SESSION_ID.size());

        auto& res = doc.V2G_Message.Body.ChargeParameterDiscoveryRes;
        init_iso2_ChargeParameterDiscoveryResType(&res);
        doc.V2G_Message.Body.ChargeParameterDiscoveryRes_isUsed = 1u;
        res.ResponseCode = iso2_responseCodeType_OK;
        res.EVSEProcessing = iso2_EVSEProcessingType_Finished;

        auto& ac = res.AC_EVSEChargeParameter;
        init_iso2_AC_EVSEChargeParameterType(&ac);
        ac.EVSENominalVoltage = {0, iso2_unitSymbolType_V, 230};
        ac.EVSEMaxCurrent = {0, iso2_unitSymbolType_A, 32};
        res.AC_EVSEChargeParameter_isUsed = 1u;

        auto& tuple = res.SAScheduleList.SAScheduleTuple.array[0];
        init_iso2_SAScheduleTupleType(&tuple);
        tuple.SAScheduleTupleID = 1;

        auto& entry = tuple.PMaxSchedule.PMaxScheduleEntry.array[0];
        init_iso2_PMaxScheduleEntryType(&entry);
        // TimeInterval carries no content: iso2_IntervalType is the abstract, empty type.
        entry.TimeInterval_isUsed = 1u;
        entry.PMax = {0, iso2_unitSymbolType_W, 42};
        tuple.PMaxSchedule.PMaxScheduleEntry.arrayLen = 1;

        res.SAScheduleList.SAScheduleTuple.arrayLen = 1;
        res.SAScheduleList_isUsed = 1u;

        uint8_t buffer[1024];
        auto out = get_exi_output_stream(io::StreamOutputView{buffer, sizeof(buffer)});
        REQUIRE(encode_iso2_exiDocument(&out, &doc) == 0);
        const auto length = exi_bitstream_get_length(&out);

        THEN("the entry is dropped rather than decoded from uninitialised memory") {
            const message_2::ResponseVariant variant{io::StreamInputView{buffer, length}};

            const auto& msg = expect<message_2::ChargeParameterDiscoveryResponse>(variant);
            REQUIRE(msg.sa_schedule_list.has_value());
            REQUIRE(msg.sa_schedule_list->size() == 1);
            REQUIRE(msg.sa_schedule_list->at(0).pmax_schedule.empty());
        }
    }
}
