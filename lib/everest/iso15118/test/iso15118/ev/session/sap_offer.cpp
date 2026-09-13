// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <iso15118/ev/sap_offer.hpp>

using namespace iso15118;
using namespace iso15118::ev;

namespace {

using ServiceCategory = message_20::datatypes::ServiceCategory;

std::vector<std::string> namespaces(const std::vector<OfferedProtocol>& offer) {
    std::vector<std::string> list;
    for (const auto& entry : offer) {
        list.push_back(entry.entry.protocol_namespace);
    }
    return list;
}

SapOfferInput all_generations(ServiceCategory service) {
    SapOfferInput input;
    input.supported_protocols = {ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121};
    input.energy_service = service;
    return input;
}

} // namespace

SCENARIO("ISO15118-20 EV build_sap_offer covers the offered generations") {
    GIVEN("a DC service offering -20, -2 and DIN") {
        const auto offer = build_sap_offer(all_generations(ServiceCategory::DC));

        THEN("all three are offered in priority order with the DC -20 namespace") {
            REQUIRE(namespaces(offer) ==
                    std::vector<std::string>{ISO20_DC_PROTOCOL_NAMESPACE, ISO2_NAMESPACE, DIN70121_NAMESPACE});
            REQUIRE(offer[0].protocol == ProtocolId::ISO15118_20);
            REQUIRE(offer[1].protocol == ProtocolId::ISO15118_2);
            REQUIRE(offer[2].protocol == ProtocolId::DIN70121);
        }

        THEN("schema ids are numbered from 1 and equal the priority") {
            for (size_t i = 0; i < offer.size(); ++i) {
                REQUIRE(offer[i].entry.schema_id == static_cast<uint8_t>(i + 1));
                REQUIRE(offer[i].entry.priority == offer[i].entry.schema_id);
            }
        }

        THEN("the version numbers match the generations") {
            REQUIRE(offer[0].entry.version_number_major == 1);
            REQUIRE(offer[0].entry.version_number_minor == 0);
            REQUIRE(offer[1].entry.version_number_major == 2);
            REQUIRE(offer[2].entry.version_number_major == 2);
        }
    }

    GIVEN("an AC service offering -20, -2 and DIN") {
        const auto offer = build_sap_offer(all_generations(ServiceCategory::AC));

        THEN("the AC -20 namespace is used and DIN is dropped, renumbering the rest") {
            REQUIRE(namespaces(offer) == std::vector<std::string>{ISO20_AC_PROTOCOL_NAMESPACE, ISO2_NAMESPACE});
            REQUIRE(offer[1].entry.schema_id == 2);
        }
    }

    GIVEN("an AC_BPT service offering -20 only") {
        SapOfferInput input;
        input.energy_service = ServiceCategory::AC_BPT;
        const auto offer = build_sap_offer(input);

        THEN("the AC -20 namespace is used") {
            REQUIRE(namespaces(offer) == std::vector<std::string>{ISO20_AC_PROTOCOL_NAMESPACE});
        }
    }

    GIVEN("a DC service under TLS offering -20, -2 and DIN") {
        auto input = all_generations(ServiceCategory::DC);
        input.tls = true;
        const auto offer = build_sap_offer(input);

        THEN("DIN is dropped [V2G-DC-868]") {
            REQUIRE(namespaces(offer) == std::vector<std::string>{ISO20_DC_PROTOCOL_NAMESPACE, ISO2_NAMESPACE});
        }
    }

    GIVEN("no supported protocols at all") {
        SapOfferInput input;
        input.supported_protocols = {};
        const auto offer = build_sap_offer(input);

        THEN("the offer is empty") {
            REQUIRE(offer.empty());
        }
    }
}

SCENARIO("ISO15118-20 EV build_sap_offer appends a custom protocol") {
    GIVEN("a DC -20 offer with an unknown custom namespace") {
        SapOfferInput input;
        input.custom_protocol = "urn:example:custom:MsgDef";
        const auto offer = build_sap_offer(input);

        THEN("it is appended last with the next schema id and treated as -20") {
            REQUIRE(namespaces(offer) ==
                    std::vector<std::string>{ISO20_DC_PROTOCOL_NAMESPACE, "urn:example:custom:MsgDef"});
            REQUIRE(offer[1].entry.schema_id == 2);
            REQUIRE(offer[1].protocol == ProtocolId::ISO15118_20);
        }
    }

    GIVEN("a DC -20 offer with a custom namespace that is a known generation") {
        SapOfferInput input;
        input.custom_protocol = ISO2_NAMESPACE;
        const auto offer = build_sap_offer(input);

        THEN("the entry maps to that generation") {
            REQUIRE(offer.size() == 2);
            REQUIRE(offer[1].protocol == ProtocolId::ISO15118_2);
        }
    }

    GIVEN("a custom namespace that duplicates an already offered one") {
        SapOfferInput input;
        input.custom_protocol = ISO20_DC_PROTOCOL_NAMESPACE;
        const auto offer = build_sap_offer(input);

        THEN("it is not offered twice") {
            REQUIRE(offer.size() == 1);
        }
    }
}

SCENARIO("ISO15118-20 EV build_sap_offer constrains a resumed session to one protocol") {
    GIVEN("a DC offer of -20, -2 and DIN resuming an ISO 15118-2 session") {
        auto input = all_generations(ServiceCategory::DC);
        input.resume_protocol = ProtocolId::ISO15118_2;
        input.custom_protocol = "urn:example:custom:MsgDef";
        const auto offer = build_sap_offer(input);

        THEN("only that protocol is offered, with schema id 1, and no custom entry is appended") {
            REQUIRE(offer.size() == 1);
            REQUIRE(offer[0].entry.protocol_namespace == ISO2_NAMESPACE);
            REQUIRE(offer[0].entry.schema_id == 1);
            REQUIRE(offer[0].protocol == ProtocolId::ISO15118_2);
        }
    }

    GIVEN("a resume protocol that is not in the supported list") {
        SapOfferInput input;
        input.supported_protocols = {ProtocolId::ISO15118_20};
        input.resume_protocol = ProtocolId::DIN70121;
        const auto offer = build_sap_offer(input);

        THEN("nothing can be offered") {
            REQUIRE(offer.empty());
        }
    }
}

SCENARIO("ISO15118-20 EV offer_from_app_protocols adopts an explicit list") {
    GIVEN("a list mixing known and unknown namespaces") {
        const std::vector<message_20::SupportedAppProtocol> list{{ISO20_DC_PROTOCOL_NAMESPACE, 1, 0, 3, 3},
                                                                 {"urn:example:unknown:MsgDef", 1, 0, 4, 4},
                                                                 {DIN70121_NAMESPACE, 2, 0, 5, 5}};
        const auto offer = offer_from_app_protocols(list);

        THEN("the unknown namespace is dropped and the given schema ids are kept verbatim") {
            REQUIRE(offer.size() == 2);
            REQUIRE(offer[0].entry.schema_id == 3);
            REQUIRE(offer[0].protocol == ProtocolId::ISO15118_20);
            REQUIRE(offer[1].entry.schema_id == 5);
            REQUIRE(offer[1].protocol == ProtocolId::DIN70121);
        }
    }

    GIVEN("a list of only unknown namespaces") {
        const auto offer = offer_from_app_protocols({{"urn:example:unknown:MsgDef", 1, 0, 1, 1}});

        THEN("the offer is empty") {
            REQUIRE(offer.empty());
        }
    }
}
