// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <memory>
#include <optional>
#include <string>

#include <utils/error.hpp>
#include <utils/error/error_database_map.hpp>
#include <utils/error/error_manager_impl.hpp>

namespace {

Everest::error::Error make_error(const std::string& text_repr) {
    Everest::error::Error error{"evse_board_support/MREC3HighTemperature",
                                "SubType",
                                "a message",
                                "a description",
                                ImplementationIdentifier("bsp_1", "main"),
                                "com.example",
                                Everest::error::Severity::High,
                                Everest::Date::from_rfc3339("2026-10-13T10:00:00.000Z"),
                                Everest::error::UUID("the-uuid"),
                                Everest::error::State::Active};
    error.trigger_value = Everest::error::TriggerValue{text_repr};
    return error;
}

std::optional<std::string> raised_text_repr(const std::string& text_repr) {
    std::optional<std::string> published;
    Everest::error::ErrorManagerImpl manager(
        nullptr, std::make_shared<Everest::error::ErrorDatabaseMap>(), {},
        [&published](const Everest::error::Error& error) { published = error.trigger_value.value().text_repr; },
        [](const Everest::error::Error&) {}, false);
    manager.raise_error(make_error(text_repr));
    return published;
}

} // namespace

SCENARIO("Raising an error limits the length of its trigger value") {
    constexpr auto max_length = Everest::error::TriggerValue::text_repr_max_length;

    GIVEN("A text representation at the limit") {
        THEN("It is published unchanged") {
            CHECK(raised_text_repr(std::string(max_length, 'a')) == std::string(max_length, 'a'));
        }
    }

    GIVEN("A text representation over the limit") {
        THEN("It is truncated to the limit") {
            CHECK(raised_text_repr(std::string(max_length + 100, 'a')) == std::string(max_length, 'a'));
        }
    }

    GIVEN("Multi-byte characters at the limit") {
        THEN("Characters are counted, not bytes, and none is split") {
            const auto text = std::string(max_length - 1, 'a') + "äö";
            CHECK(raised_text_repr(text) == std::string(max_length - 1, 'a') + "ä");
        }
    }
}
