// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// MessageExchange twice over: first the generation-independent contract against a stub codec, then
// the ISO 15118-20 instantiation against real messages. The stub half pins what every generation
// leans on - one pending request at a time, the payload type the codec chose travelling with the
// request, an encode failure surfacing as nullopt rather than a partial frame - without needing a
// protocol to say it in.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/message_exchange.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/variant.hpp>

using namespace iso15118;
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

SCENARIO("ISO15118-20 EV MessageExchange serializes requests") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("A SessionSetupRequest set as the pending request") {
        message_20::SessionSetupRequest request{};
        request.header.session_id = {0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
        request.evccid = "EVEREST_EV";

        msg_exch.set_request(request);

        THEN("has_request is true before taking it") {
            REQUIRE(msg_exch.has_request() == true);
        }

        WHEN("the request is taken") {
            const auto taken = msg_exch.take_request();

            THEN("it yields a non-empty Part20Main payload and clears the pending flag") {
                REQUIRE(taken.has_value());
                REQUIRE(taken->first.empty() == false);
                REQUIRE(taken->second == io::v2gtp::PayloadType::Part20Main);
                REQUIRE(msg_exch.has_request() == false);
            }

            THEN("the framed bytes round-trip-decode back to the SessionSetupRequest") {
                REQUIRE(taken.has_value());
                const auto& bytes = taken->first;
                message_20::Variant decoded{taken->second, io::StreamInputView{bytes.data(), bytes.size()}};
                const auto* decoded_request = decoded.get_if<message_20::SessionSetupRequest>();
                REQUIRE(decoded_request != nullptr);
                REQUIRE(decoded_request->evccid == request.evccid);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV MessageExchange holds a single request and guards the slot") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("A SessionSetupRequest set as the pending request") {
        message_20::SessionSetupRequest setup{};
        setup.header.session_id = {0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02};
        setup.evccid = "EVEREST_EV";

        msg_exch.set_request(setup);

        THEN("a second set_request on the occupied slot throws (depth-<=1 invariant)") {
            message_20::DC_PreChargeRequest pre_charge{};
            pre_charge.processing = message_20::datatypes::Processing::Finished;
            REQUIRE_THROWS_AS(msg_exch.set_request(pre_charge), std::logic_error);
        }

        WHEN("the request is taken, the slot is free for the next one") {
            REQUIRE(msg_exch.take_request().has_value());
            REQUIRE(msg_exch.has_request() == false);

            message_20::DC_PreChargeRequest pre_charge{};
            pre_charge.processing = message_20::datatypes::Processing::Finished;
            pre_charge.present_voltage = {4000, -1};
            pre_charge.target_voltage = {4000, -1};

            msg_exch.set_request(pre_charge);

            THEN("the newly set request round-trip-decodes back to a DC_PreChargeRequest") {
                const auto taken = msg_exch.take_request();
                REQUIRE(taken.has_value());

                message_20::Variant decoded{taken->second,
                                            io::StreamInputView{taken->first.data(), taken->first.size()}};
                const auto* pre_charge_decoded = decoded.get_if<message_20::DC_PreChargeRequest>();
                REQUIRE(pre_charge_decoded != nullptr);
                REQUIRE(pre_charge_decoded->processing == message_20::datatypes::Processing::Finished);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV MessageExchange guards against encode overflow") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("A ScheduleExchangeRequest whose power schedule far exceeds the encode buffer") {
        // The AuthorizationRequest PnC certificate is dropped by the CPP->CB convert()
        // in message/authorization.cpp (a TODO there), so it can never overflow. A
        // ScheduleExchangeRequest power schedule IS fully serialized: 1024
        // EVPowerScheduleEntry elements produce an EXI payload well past the 4096-byte
        // buffer, so encode_iso20_exiDocument errors, serialize throws, and take_request
        // must surface the failure so the caller can stop.
        message_20::ScheduleExchangeRequest request{};
        request.max_supporting_points = 1024;

        message_20::datatypes::Scheduled_SEReqControlMode mode{};
        message_20::datatypes::EVEnergyOffer offer{};
        offer.power_schedule.time_anchor = 0;
        for (std::size_t i = 0; i < 1024; ++i) {
            offer.power_schedule.entries.emplace_back(message_20::datatypes::EVPowerScheduleEntry{60, {1000, 0}});
        }
        mode.energy_offer = offer;
        request.control_mode = mode;

        msg_exch.set_request(request);

        WHEN("the request is taken") {
            const auto taken = msg_exch.take_request();

            THEN("the encode failure surfaces as nullopt and the pending flag is cleared") {
                REQUIRE(taken.has_value() == false);
                REQUIRE(msg_exch.has_request() == false);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV MessageExchange guards its response slot") {

    ev::d20::MessageExchange msg_exch{};

    GIVEN("An empty MessageExchange") {

        THEN("peek_response_type reports None when no response is staged") {
            REQUIRE(msg_exch.peek_response_type() == message_20::Type::None);
        }

        THEN("pull_response on an empty slot throws") {
            REQUIRE_THROWS_AS(msg_exch.pull_response(), std::runtime_error);
        }
    }

    GIVEN("A MessageExchange with a response already staged") {
        message_20::SessionSetupResponse res{};
        res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
        res.evseid = "DE*PNX*E12345";
        msg_exch.set_response(std::make_unique<message_20::Variant>(res));

        THEN("a second set_response throws rather than dropping the unhandled response") {
            auto second = std::make_unique<message_20::Variant>(res);
            REQUIRE_THROWS_AS(msg_exch.set_response(std::move(second)), std::runtime_error);
        }
    }
}
