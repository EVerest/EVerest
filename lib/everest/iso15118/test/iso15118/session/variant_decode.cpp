// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Encoder.h>

#include <iso15118/message/variant.hpp>

using namespace iso15118;

namespace {

// A well-formed request of a type the SECC does not implement.
std::vector<uint8_t> encoded_vehicle_check_in_req() {
    iso20_exiDocument doc;
    init_iso20_exiDocument(&doc);
    doc.VehicleCheckInReq_isUsed = 1;
    init_iso20_VehicleCheckInReqType(&doc.VehicleCheckInReq);
    doc.VehicleCheckInReq.Header.SessionID.bytesLen = iso20_sessionIDType_BYTES_SIZE;
    doc.VehicleCheckInReq.Header.TimeStamp = 1691411798;
    doc.VehicleCheckInReq.EVCheckInStatus = iso20_evCheckInStatusType_CheckIn;
    doc.VehicleCheckInReq.ParkingMethod = iso20_parkingMethodType_Manual;

    std::vector<uint8_t> buffer(256);
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, buffer.data(), buffer.size(), 0, nullptr);
    REQUIRE(encode_iso20_exiDocument(&stream, &doc) == 0);
    buffer.resize(exi_bitstream_get_length(&stream));
    return buffer;
}

} // namespace

SCENARIO("A -20 Variant tells an undecodable frame from a decoded but unhandled message") {
    GIVEN("A decoded VehicleCheckInReq") {
        const auto payload = encoded_vehicle_check_in_req();
        const message_20::Variant variant{io::v2gtp::PayloadType::Part20Main,
                                          io::StreamInputView{payload.data(), payload.size()}};

        THEN("it carries no message, names the reason and is not undecodable: the state answers it") {
            REQUIRE(variant.get_type() == message_20::Type::None);
            REQUIRE_FALSE(variant.get_error().empty());
            REQUIRE_FALSE(variant.is_undecodable());
        }
    }

    GIVEN("A payload the EXI decoder rejects") {
        const std::vector<uint8_t> payload{0xff, 0xff, 0xff, 0xff};
        const message_20::Variant variant{io::v2gtp::PayloadType::Part20Main,
                                          io::StreamInputView{payload.data(), payload.size()}};

        THEN("it is undecodable [V2G20-800]") {
            REQUIRE(variant.get_type() == message_20::Type::None);
            REQUIRE(variant.is_undecodable());
        }
    }

    GIVEN("An empty payload") {
        const message_20::Variant variant{io::v2gtp::PayloadType::Part20Main, io::StreamInputView{nullptr, 0}};

        THEN("it is undecodable [V2G20-800]") {
            REQUIRE(variant.is_undecodable());
        }
    }
}
