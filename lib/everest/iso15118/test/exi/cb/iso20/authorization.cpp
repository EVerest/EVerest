#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <cstring>
#include <stdexcept>

#include <iso15118/message/authorization.hpp>
#include <iso15118/message/variant.hpp>

#include <cbv2g/iso_20/iso20_CommonMessages_Datatypes.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Encoder.h>

#include "helper.hpp"

using namespace iso15118;

SCENARIO("Se/Deserialize authorization messages") {

    GIVEN("Deserialize authorization_req eim") {

        uint8_t doc_raw[] = {0x80, 0x00, 0x04, 0x79, 0x0c, 0x8a, 0xdc, 0xee, 0xee,
                             0x09, 0x68, 0x8d, 0x6c, 0xac, 0x3a, 0x60, 0x62, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::AuthorizationReq);

            const auto& msg = variant.get<message_20::AuthorizationRequest>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1});
            REQUIRE(header.timestamp == 1691411798);

            REQUIRE(msg.selected_authorization_service == message_20::datatypes::Authorization::EIM);
            REQUIRE(std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(msg.authorization_mode));
        }
    }

    GIVEN("Serialize authorization_res") {

        message_20::AuthorizationResponse res;

        res.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        res.response_code = message_20::datatypes::ResponseCode::OK;
        res.evse_processing = message_20::datatypes::Processing::Finished;

        std::vector<uint8_t> expected = {0x80, 0x04, 0x04, 0x79, 0x0c, 0x8a, 0xdc, 0xee, 0xee, 0x09,
                                         0x68, 0x8d, 0x6c, 0xac, 0x3a, 0x60, 0x62, 0x00, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(res) == expected);
        }
    }

    GIVEN("Deserialize authorization_res eim") {

        uint8_t doc_raw[] = {0x80, 0x04, 0x04, 0x79, 0x0c, 0x8a, 0xdc, 0xee, 0xee, 0x09,
                             0x68, 0x8d, 0x6c, 0xac, 0x3a, 0x60, 0x62, 0x00, 0x00};

        const io::StreamInputView stream_view{doc_raw, sizeof(doc_raw)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_20::Type::AuthorizationRes);

            const auto& msg = variant.get<message_20::AuthorizationResponse>();
            const auto& header = msg.header;

            REQUIRE(header.session_id == std::array<uint8_t, 8>{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1});
            REQUIRE(header.timestamp == 1691411798);

            REQUIRE(msg.evse_processing == message_20::datatypes::Processing::Finished);
            REQUIRE(msg.response_code == message_20::datatypes::ResponseCode::OK);
        }
    }

    GIVEN("Serialize authorization_req eim") {

        message_20::AuthorizationRequest req;

        req.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        req.selected_authorization_service = message_20::datatypes::Authorization::EIM;
        req.authorization_mode = message_20::datatypes::EIM_ASReqAuthorizationMode{};

        std::vector<uint8_t> expected = {0x80, 0x00, 0x04, 0x79, 0x0c, 0x8a, 0xdc, 0xee, 0xee,
                                         0x09, 0x68, 0x8d, 0x6c, 0xac, 0x3a, 0x60, 0x62, 0x00};

        THEN("It should be serialized successfully") {
            REQUIRE(serialize_helper(req) == expected);
        }
    }

    GIVEN("Convert authorization_req into the encoder struct") {

        message_20::AuthorizationRequest req;

        req.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        // Deliberately EIM in both cases, so the authorization mode variant is the only thing
        // that can steer which mode the encoder struct carries.
        req.selected_authorization_service = message_20::datatypes::Authorization::EIM;

        THEN("An eim mode marks eim used and pnc unused") {
            req.authorization_mode = message_20::datatypes::EIM_ASReqAuthorizationMode{};

            iso20_AuthorizationReqType out{};
            message_20::convert(req, out);

            CHECK(out.EIM_AReqAuthorizationMode_isUsed);
            CHECK_FALSE(out.PnC_AReqAuthorizationMode_isUsed);
        }

        THEN("A pnc mode marks pnc used and eim unused") {
            auto& pnc = req.authorization_mode.emplace<message_20::datatypes::PnC_ASReqAuthorizationMode>();
            pnc.id = "id1";
            pnc.gen_challenge.fill(0x5a);
            pnc.contract_certificate_chain.certificate = {0x30, 0x82, 0x01, 0x02};

            iso20_AuthorizationReqType out{};
            message_20::convert(req, out);

            CHECK_FALSE(out.EIM_AReqAuthorizationMode_isUsed);
            CHECK(out.PnC_AReqAuthorizationMode_isUsed);
            CHECK(std::string(out.PnC_AReqAuthorizationMode.Id.characters,
                              out.PnC_AReqAuthorizationMode.Id.charactersLen) == "id1");
            CHECK(out.PnC_AReqAuthorizationMode.GenChallenge.bytesLen == 16);
            CHECK(out.PnC_AReqAuthorizationMode.ContractCertificateChain.Certificate.bytesLen == 4);
        }
    }

    GIVEN("Round trip of a pnc authorization_req") {

        message_20::AuthorizationRequest req;

        req.header = message_20::Header{{0xF2, 0x19, 0x15, 0xB9, 0xDD, 0xDC, 0x12, 0xD1}, 1691411798};
        req.selected_authorization_service = message_20::datatypes::Authorization::PnC;
        auto& pnc = req.authorization_mode.emplace<message_20::datatypes::PnC_ASReqAuthorizationMode>();
        pnc.id = "id1";
        for (std::size_t i = 0; i < pnc.gen_challenge.size(); ++i) {
            pnc.gen_challenge[i] = static_cast<uint8_t>(i);
        }
        pnc.contract_certificate_chain.certificate = {0x30, 0x82, 0x01, 0x02, 0x03};
        pnc.contract_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>{0x30, 0x11});
        pnc.contract_certificate_chain.sub_certificates.emplace_back(std::vector<uint8_t>{0x30, 0x22, 0x23});

        const auto encoded = serialize_helper(req);

        const io::StreamInputView stream_view{encoded.data(), encoded.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("Every pnc field and the raw exi survive") {
            REQUIRE(variant.get_type() == message_20::Type::AuthorizationReq);
            REQUIRE(variant.get_exi_payload() == encoded);

            const auto& msg = variant.get<message_20::AuthorizationRequest>();
            REQUIRE(msg.selected_authorization_service == message_20::datatypes::Authorization::PnC);
            REQUIRE(std::holds_alternative<message_20::datatypes::PnC_ASReqAuthorizationMode>(msg.authorization_mode));
            const auto& decoded = std::get<message_20::datatypes::PnC_ASReqAuthorizationMode>(msg.authorization_mode);
            REQUIRE(decoded.id == "id1");
            REQUIRE(decoded.gen_challenge == pnc.gen_challenge);
            REQUIRE(decoded.contract_certificate_chain.certificate == pnc.contract_certificate_chain.certificate);
            REQUIRE(decoded.contract_certificate_chain.sub_certificates.size() == 2);
            REQUIRE(decoded.contract_certificate_chain.sub_certificates[0] == std::vector<uint8_t>{0x30, 0x11});
            REQUIRE(decoded.contract_certificate_chain.sub_certificates[1] == std::vector<uint8_t>{0x30, 0x22, 0x23});
            REQUIRE_FALSE(msg.header.signature.has_value());
        }
    }

    GIVEN("A pnc authorization_req carrying a header signature") {

        iso20_exiDocument doc{};
        init_iso20_exiDocument(&doc);
        doc.AuthorizationReq_isUsed = 1;
        auto& req = doc.AuthorizationReq;
        init_iso20_AuthorizationReqType(&req);
        req.Header.SessionID.bytesLen = 8;
        req.Header.TimeStamp = 1691411798;
        req.SelectedAuthorizationService = iso20_authorizationType_PnC;
        req.PnC_AReqAuthorizationMode_isUsed = 1;
        init_iso20_PnC_AReqAuthorizationModeType(&req.PnC_AReqAuthorizationMode);
        std::memcpy(req.PnC_AReqAuthorizationMode.Id.characters, "id1", 3);
        req.PnC_AReqAuthorizationMode.Id.charactersLen = 3;
        req.PnC_AReqAuthorizationMode.GenChallenge.bytesLen = 16;
        req.PnC_AReqAuthorizationMode.ContractCertificateChain.Certificate.bytesLen = 2;
        req.PnC_AReqAuthorizationMode.ContractCertificateChain.SubCertificates.Certificate.arrayLen = 1;
        req.PnC_AReqAuthorizationMode.ContractCertificateChain.SubCertificates.Certificate.array[0].bytesLen = 2;

        req.Header.Signature_isUsed = 1;
        auto& sig = req.Header.Signature;
        init_iso20_SignatureType(&sig);
        const auto set_string = [](auto& field, const char* value) {
            const auto len = std::strlen(value);
            std::memcpy(field.characters, value, len);
            field.charactersLen = static_cast<uint16_t>(len);
        };
        set_string(sig.SignedInfo.CanonicalizationMethod.Algorithm, "http://www.w3.org/TR/canonical-exi/");
        set_string(sig.SignedInfo.SignatureMethod.Algorithm, "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha512");
        sig.SignedInfo.Reference.arrayLen = 1;
        auto& ref = sig.SignedInfo.Reference.array[0];
        init_iso20_ReferenceType(&ref);
        ref.URI_isUsed = 1;
        set_string(ref.URI, "#id1");
        ref.Transforms_isUsed = 1;
        set_string(ref.Transforms.Transform.Algorithm, "http://www.w3.org/TR/canonical-exi/");
        set_string(ref.DigestMethod.Algorithm, "http://www.w3.org/2001/04/xmlenc#sha512");
        ref.DigestValue.bytesLen = 64;
        for (uint16_t i = 0; i < 64; ++i) {
            ref.DigestValue.bytes[i] = static_cast<uint8_t>(i);
        }
        sig.SignatureValue.CONTENT.bytesLen = 132;
        for (uint16_t i = 0; i < 132; ++i) {
            sig.SignatureValue.CONTENT.bytes[i] = static_cast<uint8_t>(0xff - i);
        }

        uint8_t buffer[2048];
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, buffer, sizeof(buffer), 0, nullptr);
        REQUIRE(encode_iso20_exiDocument(&stream, &doc) == 0);
        const io::StreamInputView stream_view{buffer, exi_bitstream_get_length(&stream)};

        message_20::Variant variant(io::v2gtp::PayloadType::Part20Main, stream_view);

        THEN("The header signature is decoded") {
            REQUIRE(variant.get_type() == message_20::Type::AuthorizationReq);
            const auto& msg = variant.get<message_20::AuthorizationRequest>();
            REQUIRE(msg.header.signature.has_value());
            const auto& signature = msg.header.signature.value();
            REQUIRE(signature.signed_info.canonicalization_method == "http://www.w3.org/TR/canonical-exi/");
            REQUIRE(signature.signed_info.signature_method == "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha512");
            REQUIRE(signature.signed_info.references.size() == 1);
            const auto& reference = signature.signed_info.references[0];
            REQUIRE(reference.uri == "#id1");
            REQUIRE(reference.transform_algorithm == "http://www.w3.org/TR/canonical-exi/");
            REQUIRE(reference.digest_method == "http://www.w3.org/2001/04/xmlenc#sha512");
            REQUIRE(reference.digest_value.size() == 64);
            REQUIRE(reference.digest_value[63] == 63);
            REQUIRE(signature.signature.value.size() == 132);
            REQUIRE(signature.signature.value[0] == 0xff);
        }
    }
}
