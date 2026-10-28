// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "SerializationTestHelpers.hpp"
#include "everest_api_types/generic/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include "everest_api_types/utilities/constants.hpp"
#include "everest_api_types/utilities/request_reply.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <string_view>

using namespace everest::lib::API::V1_0::types::generic;

using namespace everest::lib::API::V1_0::types;

// Warning: these test helpers are generated manually because the serialization of RequestReply works differently
// than other serializations because the to_json function may throw an error
template <>
everest::lib::API::V1_0::types::generic::RequestReply
generate<everest::lib::API::V1_0::types::generic::RequestReply>(bool set_optional_fields, int seed) {
    (void)seed; // Not used in this generator, but silence the compiler warning;
    RequestReply generated_object;
    generated_object.replyTo = "mdvChgNGIuAZPRzFEkDOLnsMUcJYyl";
    generated_object.payload = "{\"data\": { \"inner\" : 23, \"inner_str\":\"string\"}}";
    if (set_optional_fields) {
    }
    return generated_object;
}
template <>
void verify<everest::lib::API::V1_0::types::generic::RequestReply>(
    everest::lib::API::V1_0::types::generic::RequestReply original,
    everest::lib::API::V1_0::types::generic::RequestReply result) {
    auto orig_pl = nlohmann::json::parse(original.payload).dump();
    auto res_pl = nlohmann::json::parse(result.payload).dump();

    EXPECT_EQ(original.replyTo, result.replyTo);
    EXPECT_EQ(orig_pl, res_pl);
}

namespace RequestReplyTestHelper {
RequestReply generate(int payload) {
    RequestReply generated_object;
    generated_object.replyTo = "mdvChgNGIuAZPRzFEkDOLnsMUcJYyl";
    generated_object.payload = std::to_string(payload);
    return generated_object;
}
RequestReply generate(std::string payload) {
    RequestReply generated_object;
    generated_object.replyTo = "mdvChgNGIuAZPRzFEkDOLnsMUcJYyl";
    generated_object.payload = payload;
    return generated_object;
}
void test(RequestReply original) {
    auto result = codec_test(original);
    verify(original, result);
}
void test(std::string payload) {
    test(generate(payload));
}
void test(int payload) {
    test(generate(payload));
}
void verify(RequestReply original_object, RequestReply result_object) {
    EXPECT_EQ(original_object.replyTo, result_object.replyTo);
    EXPECT_EQ(original_object.payload, result_object.payload);
}
}; // namespace RequestReplyTestHelper

// Tests
TEST(generic, RequestReply_obj_payload_set) {
    gen_test<everest::lib::API::V1_0::types::generic::RequestReply>();
}
TEST(generic, RequestReply_string_payload_set) {
    EXPECT_THROW({ RequestReplyTestHelper::test("RHwxIpQVSTKyngUNAaOBuEsqCZDbze"); }, std::invalid_argument);
}

TEST(generic, RequestReply_int_payload_set) {
    RequestReplyTestHelper::test(1);
}

TEST(generic, RequestReply_back_and_forth_1) {
    RequestReply rr;
    rr.replyTo = "/this/is/my/reply/address";
    rr.payload = R"( {"data": { "number" : 23, "string": "string", "obj": {"more": 11 }}} )";

    auto ser = serialize(rr);
    auto des = deserialize<RequestReply>(ser);

    EXPECT_EQ(rr.replyTo, des.replyTo);
    EXPECT_EQ(nlohmann::json::parse(rr.payload).dump(), nlohmann::json::parse(des.payload).dump());
}

TEST(generic, RequestReply_back_and_forth_2) {
    RequestReply rr;
    rr.replyTo = "/this/is/my/reply/address";
    rr.payload = "\"helloworld\"";

    auto ser = serialize(rr);
    auto des = deserialize<RequestReply>(ser);
    auto ser2 = serialize(des);
    auto des2 = deserialize<RequestReply>(ser2);

    EXPECT_EQ(rr.replyTo, des.replyTo);
    EXPECT_EQ(rr.payload, des.payload);
    EXPECT_EQ(rr.replyTo, des2.replyTo);
    EXPECT_EQ(rr.payload, des2.payload);
}

TEST(generic, deserialize_accepts_string_view_subrange) {
    std::string payload = "xxtrueyy";
    std::string_view json_data(payload.data() + 2, 4);

    EXPECT_TRUE(deserialize<bool>(json_data));

    bool result = false;
    EXPECT_TRUE(everest::lib::API::deserialize(json_data, result));
    EXPECT_TRUE(result);
}

namespace {
std::string request_with_nested_payload(std::size_t depth) {
    return R"({"headers":{"replyTo":"reply/topic"},"payload":)" + std::string(depth, '[') + std::string(depth, ']') +
           "}";
}
} // namespace

TEST(RequestReplyDecoding, rejects_payload_nested_deeper_than_limit) {
    using everest::lib::API::max_json_nesting_depth;
    RequestReply msg;

    // The envelope adds one level around the payload.
    EXPECT_TRUE(everest::lib::API::deserialize(request_with_nested_payload(max_json_nesting_depth - 1), msg));
    EXPECT_EQ(msg.replyTo, "reply/topic");
    EXPECT_FALSE(everest::lib::API::deserialize(request_with_nested_payload(max_json_nesting_depth), msg));
    // Deep enough to overflow the stack when serializing the decoded payload.
    EXPECT_FALSE(everest::lib::API::deserialize(request_with_nested_payload(100000), msg));
}

TEST(RequestReplyDecoding, missing_reply_to_is_no_reply) {
    RequestReply msg;

    EXPECT_TRUE(everest::lib::API::deserialize(R"({"payload":{}})", msg));
    EXPECT_FALSE(msg.replyTo.has_value());
    EXPECT_TRUE(everest::lib::API::deserialize(R"({"headers":{},"payload":{}})", msg));
    EXPECT_FALSE(msg.replyTo.has_value());
}

TEST(RequestReplyDecoding, rejects_reply_to_that_is_not_a_publishable_topic) {
    RequestReply msg;

    for (const auto* request :
         {R"({"headers":{"replyTo":""}})", R"({"headers":{"replyTo":"reply/+/topic"}})",
          R"({"headers":{"replyTo":"reply/#"}})", R"({"headers":{"replyTo":42}})", R"({"headers":"reply/topic"})",
          R"({"headers":null})", R"({"headers":{"replyTo":"$SYS/broker/load"}})"}) {
        EXPECT_FALSE(everest::lib::API::deserialize(request, msg)) << request;
    }
}

TEST(RequestReplyDecoding, rejects_request_that_is_not_an_object) {
    RequestReply msg;

    for (const auto* request : {"true", "42", "[]", "null", R"("text")"}) {
        EXPECT_FALSE(everest::lib::API::deserialize(request, msg)) << request;
    }
}

TEST(RequestReplyDecoding, rejects_reply_to_on_a_topic_everest_subscribes_to) {
    RequestReply msg;

    for (const auto* reply_to :
         {"everest_api/1/session_storage_consumer/storage/m2e/clear_sessions",
          "everest_api/1/configuration/m2e/mark_active_slot", "everest_api/discover",
          "everest_api/query-modules/evse_manager_consumer", "everest_api/1/evse_board_support/bsp/m2e/reply/0815"}) {
        auto request = nlohmann::json{{"headers", {{"replyTo", reply_to}}}}.dump();
        EXPECT_FALSE(everest::lib::API::deserialize(request, msg)) << reply_to;
    }

    for (const auto* reply_to : {"everest_api/1/evse_manager_consumer/evse/e2m/get_evse/0815", "client/reply/m2e",
                                 "everest_api_other/m2e/reply"}) {
        auto request = nlohmann::json{{"headers", {{"replyTo", reply_to}}}}.dump();
        EXPECT_TRUE(everest::lib::API::deserialize(request, msg)) << reply_to;
        EXPECT_EQ(msg.replyTo, reply_to);
    }
}

TEST(RequestReplyEncoding, omits_headers_without_reply_to) {
    RequestReply rr;
    rr.payload = "{}";

    auto j = nlohmann::json::parse(serialize(rr));

    EXPECT_FALSE(j.contains("headers"));
    EXPECT_FALSE(deserialize<RequestReply>(serialize(rr)).replyTo.has_value());
}

TEST(DeserializeRequest, reply_to_is_optional_but_must_be_publishable_when_present) {
    using everest::lib::API::deserialize_request;
    std::optional<std::string> reply_to;
    int payload = 0;

    EXPECT_TRUE(deserialize_request(R"({"headers":{"replyTo":"reply/topic"},"payload":7})", reply_to, payload));
    EXPECT_EQ(reply_to, "reply/topic");
    EXPECT_EQ(payload, 7);

    EXPECT_TRUE(deserialize_request(R"({"payload":7})", reply_to, payload));
    EXPECT_FALSE(reply_to.has_value());

    EXPECT_TRUE(deserialize_request(R"({})", reply_to));
    EXPECT_FALSE(reply_to.has_value());

    EXPECT_FALSE(deserialize_request("true", reply_to));
    EXPECT_FALSE(deserialize_request(R"({"headers":{"replyTo":"everest_api/discover"}})", reply_to));

    EXPECT_FALSE(deserialize_request(R"({"headers":{"replyTo":""},"payload":7})", reply_to, payload));
    EXPECT_FALSE(deserialize_request(R"({"headers":{"replyTo":"reply/#"}})", reply_to));
    EXPECT_FALSE(deserialize_request(R"({"headers":{"replyTo":42}})", reply_to));
    EXPECT_FALSE(deserialize_request(R"({"headers":[]})", reply_to));
}
