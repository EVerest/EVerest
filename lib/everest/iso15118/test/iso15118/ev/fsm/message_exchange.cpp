// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The generation-independent half of MessageExchange, against a stub codec. The per-generation
// suites (d20, d2, din) exercise it with their real messages; what is pinned here is the contract
// every generation relies on: one pending request at a time, the payload type the codec chose
// travelling with the request, and an encode failure surfacing as nullopt rather than a partial frame.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <iso15118/ev/message_exchange.hpp>

using iso15118::io::StreamOutputView;
using iso15118::io::v2gtp::PayloadType;

namespace {

enum class StubType {
    None,
    Small,
    Large,
    Oversized
};

struct SmallRequest {
    uint8_t fill{0x11};
};
struct LargeRequest {
    uint8_t fill{0x22};
};
// Serializes more bytes than any output buffer the exchange owns.
struct OversizedRequest {};

struct StubVariant {
    StubType type{StubType::None};
    StubType get_type() const {
        return type;
    }
};

struct StubCodec {
    using Variant = StubVariant;
    using Type = StubType;

    static size_t serialize(const SmallRequest& msg, const StreamOutputView& view) {
        view.payload[0] = msg.fill;
        return 1;
    }
    static size_t serialize(const LargeRequest& msg, const StreamOutputView& view) {
        view.payload[0] = msg.fill;
        view.payload[1] = msg.fill;
        return 2;
    }
    static size_t serialize(const OversizedRequest&, const StreamOutputView&) {
        throw std::runtime_error("does not fit");
    }

    template <typename Msg> static constexpr Type type_of();
    template <typename Msg> static constexpr PayloadType payload_type_of();
};

template <> constexpr StubType StubCodec::type_of<SmallRequest>() {
    return StubType::Small;
}
template <> constexpr StubType StubCodec::type_of<LargeRequest>() {
    return StubType::Large;
}
template <> constexpr StubType StubCodec::type_of<OversizedRequest>() {
    return StubType::Oversized;
}
// Two payload types, so a hard-coded one would be visible.
template <> constexpr PayloadType StubCodec::payload_type_of<SmallRequest>() {
    return PayloadType::SAP;
}
template <> constexpr PayloadType StubCodec::payload_type_of<LargeRequest>() {
    return PayloadType::Part20DC;
}
template <> constexpr PayloadType StubCodec::payload_type_of<OversizedRequest>() {
    return PayloadType::SAP;
}

using Exchange = iso15118::ev::MessageExchange<StubCodec>;

} // namespace

SCENARIO("The EV message exchange holds one request at a time") {
    Exchange exchange{};

    GIVEN("no request") {
        THEN("nothing is pending and the type is None") {
            REQUIRE(exchange.has_request() == false);
            REQUIRE(exchange.pending_request_type() == StubType::None);
        }
        THEN("taking one yields nothing") {
            REQUIRE(exchange.take_request().has_value() == false);
        }
    }

    GIVEN("a pending request") {
        exchange.set_request(SmallRequest{});

        THEN("it is reported pending, with its type") {
            REQUIRE(exchange.has_request() == true);
            REQUIRE(exchange.pending_request_type() == StubType::Small);
        }
        THEN("a second request throws rather than reaching the wire") {
            REQUIRE_THROWS_AS(exchange.set_request(LargeRequest{}), std::logic_error);
        }
        WHEN("it is taken") {
            const auto taken = exchange.take_request();
            THEN("the bytes are the codec's and the slot is free again") {
                REQUIRE(taken.has_value());
                REQUIRE(taken->first == std::vector<uint8_t>{0x11});
                REQUIRE(exchange.has_request() == false);
            }
        }
    }
}

SCENARIO("The payload type travels with the request, from the codec") {
    Exchange exchange{};

    GIVEN("a request the codec maps to SAP") {
        exchange.set_request(SmallRequest{});
        THEN("SAP comes back with it") {
            REQUIRE(exchange.take_request()->second == PayloadType::SAP);
        }
    }

    GIVEN("a request the codec maps to a different payload type") {
        exchange.set_request(LargeRequest{});
        THEN("that type comes back with it") {
            REQUIRE(exchange.take_request()->second == PayloadType::Part20DC);
        }
    }

    GIVEN("pre-encoded bytes the EV signed itself") {
        exchange.set_raw_request({0x01, 0x02, 0x03}, StubType::Large, PayloadType::Part20DC);
        THEN("they are transmitted verbatim under the payload type given") {
            const auto taken = exchange.take_request();
            REQUIRE(taken.has_value());
            REQUIRE(taken->first == std::vector<uint8_t>{0x01, 0x02, 0x03});
            REQUIRE(taken->second == PayloadType::Part20DC);
        }
    }
}

SCENARIO("An encode failure clears the slot instead of emitting a partial frame") {
    Exchange exchange{};
    exchange.set_request(OversizedRequest{});

    THEN("taking it yields nothing and leaves nothing pending") {
        REQUIRE(exchange.take_request().has_value() == false);
        REQUIRE(exchange.has_request() == false);
    }
}

SCENARIO("The EV message exchange holds one response at a time") {
    Exchange exchange{};

    GIVEN("no response") {
        THEN("the peeked type is None and pulling throws") {
            REQUIRE(exchange.peek_response_type() == StubType::None);
            REQUIRE_THROWS_AS(exchange.pull_response(), std::runtime_error);
        }
    }

    GIVEN("a response set") {
        exchange.set_response(std::make_unique<StubVariant>(StubVariant{StubType::Large}));

        THEN("its type can be peeked without consuming it") {
            REQUIRE(exchange.peek_response_type() == StubType::Large);
            REQUIRE(exchange.peek_response_type() == StubType::Large);
        }
        THEN("a second response throws rather than dropping the first") {
            REQUIRE_THROWS_AS(exchange.set_response(std::make_unique<StubVariant>()), std::runtime_error);
        }
        WHEN("it is pulled") {
            const auto pulled = exchange.pull_response();
            THEN("the slot is free and the pulled variant is the one set") {
                REQUIRE(pulled->get_type() == StubType::Large);
                REQUIRE(exchange.peek_response_type() == StubType::None);
            }
        }
    }
}
