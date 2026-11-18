// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "helpers.hpp"

#include <catch2/catch_all.hpp>
#include <utils/error.hpp>

fs::path get_bin_dir() {
    return fs::canonical("/proc/self/exe").parent_path();
}

std::string get_unique_db_name() {
    return "error_database_sqlite_" + Everest::error::UUID().to_string() + ".db";
}

namespace {
std::vector<Everest::error::ErrorPtr> get_base_test_errors() {
    return {// index 0
            std::make_shared<Everest::error::Error>(
                "test_type_a", "test_sub_type_a", "test_message_a", "test_description_a",
                ImplementationIdentifier("test_origin_module_a", "test_origin_implementation_a"), "everest-test",
                Everest::error::Severity::Low, date::utc_clock::now(), Everest::error::UUID(),
                Everest::error::State::ClearedByModule),
            // index 1
            std::make_shared<Everest::error::Error>(
                "test_type_b", "test_sub_type_b", "test_message_b", "test_description_b",
                ImplementationIdentifier("test_origin_module_b", "test_origin_implementation_b"), "everest-test",
                Everest::error::Severity::Low, date::utc_clock::now() + std::chrono::hours(1), Everest::error::UUID(),
                Everest::error::State::ClearedByModule),
            // index 2
            std::make_shared<Everest::error::Error>(
                "test_type_c", "test_sub_type_c", "test_message_c", "test_description_c",
                ImplementationIdentifier("test_origin_module_a", "test_origin_implementation_a"), "everest-test",
                Everest::error::Severity::Low, date::utc_clock::now() + std::chrono::hours(2), Everest::error::UUID(),
                Everest::error::State::ClearedByModule),
            // index 3
            std::make_shared<Everest::error::Error>(
                "test_type_c", "test_sub_type_c", "test_message_c", "test_description_c",
                ImplementationIdentifier("test_origin_module_c", "test_origin_implementation_c"), "everest-test",
                Everest::error::Severity::Low, date::utc_clock::now() + std::chrono::hours(3), Everest::error::UUID(),
                Everest::error::State::Active),
            // index 4
            std::make_shared<Everest::error::Error>(
                "test_type_c", "test_sub_type_a", "test_message_c", "test_description_c",
                ImplementationIdentifier("test_origin_module_a", "test_origin_implementation_a"), "everest-test",
                Everest::error::Severity::Medium, date::utc_clock::now() + std::chrono::hours(4),
                Everest::error::UUID(), Everest::error::State::Active),
            // index 5
            std::make_shared<Everest::error::Error>(
                "test_type_c", "test_sub_type_a", "test_message_c", "test_description_c",
                ImplementationIdentifier("test_origin_module_c", "test_origin_implementation_c"), "everest-test",
                Everest::error::Severity::Medium, date::utc_clock::now() + std::chrono::hours(5),
                Everest::error::UUID(), Everest::error::State::Active),
            // index 6
            std::make_shared<Everest::error::Error>(
                "test_type_a", "test_sub_type_a", "test_message_a", "test_description_a",
                ImplementationIdentifier("test_origin_module_a", "test_origin_implementation_a"), "everest-test",
                Everest::error::Severity::Medium, date::utc_clock::now() + std::chrono::hours(6),
                Everest::error::UUID(), Everest::error::State::ClearedByReboot),
            // index 7
            std::make_shared<Everest::error::Error>(
                "test_type_a", "test_sub_type_a", "test_message_a", "test_description_a",
                ImplementationIdentifier("test_origin_module_c", "test_origin_implementation_c"), "everest-test",
                Everest::error::Severity::Medium, date::utc_clock::now() + std::chrono::hours(7),
                Everest::error::UUID(), Everest::error::State::ClearedByReboot),
            // index 8
            std::make_shared<Everest::error::Error>(
                "test_type_a", "test_sub_type_a", "test_message_a", "test_description_a",
                ImplementationIdentifier("test_origin_module_a", "test_origin_implementation_a"), "everest-test",
                Everest::error::Severity::High, date::utc_clock::now() + std::chrono::hours(8), Everest::error::UUID(),
                Everest::error::State::ClearedByReboot),
            // index 9
            std::make_shared<Everest::error::Error>(
                "test_type_c", "test_sub_type_c", "test_message_c", "test_description_c",
                ImplementationIdentifier("test_origin_module_c", "test_origin_implementation_c"), "everest-test",
                Everest::error::Severity::High, date::utc_clock::now() + std::chrono::hours(9), Everest::error::UUID(),
                Everest::error::State::ClearedByReboot),
            // index 10
            std::make_shared<Everest::error::Error>(
                "test_type_c", "test_sub_type_c", "test_message_c", "test_description_c",
                ImplementationIdentifier("test_origin_module_a", "test_origin_implementation_a"), "everest-test",
                Everest::error::Severity::High, date::utc_clock::now() + std::chrono::hours(10), Everest::error::UUID(),
                Everest::error::State::ClearedByReboot),
            // index 11
            std::make_shared<Everest::error::Error>(
                "test_type_b", "test_sub_type_b", "test_message_b", "test_description_b",
                ImplementationIdentifier("test_origin_module_c", "test_origin_implementation_c"), "everest-test",
                Everest::error::Severity::High, date::utc_clock::now() + std::chrono::hours(11), Everest::error::UUID(),
                Everest::error::State::ClearedByReboot)};
}

} // namespace

std::vector<Everest::error::ErrorPtr> get_test_errors() {
    auto errors = get_base_test_errors();
    errors.at(0)->trigger_value = Everest::error::TriggerValue{"85.3 degC"};
    errors.at(3)->trigger_value = Everest::error::TriggerValue{"85,3 °C – Grenzwert überschritten"};
    errors.at(4)->trigger_value = Everest::error::TriggerValue{""};
    errors.at(0)->origin.mapping = Mapping(1, 2);
    errors.at(3)->origin.mapping = Mapping(2);
    errors.at(4)->origin.mapping = Mapping(1, 1);
    return errors;
}

void check_expected_errors_in_list(const std::vector<Everest::error::ErrorPtr>& expected_errors,
                                   const std::list<Everest::error::ErrorPtr>& errors) {
    REQUIRE(expected_errors.size() == errors.size());
    for (Everest::error::ErrorPtr exp_err : expected_errors) {
        auto result = std::find_if(errors.begin(), errors.end(), [&exp_err](const Everest::error::ErrorPtr& err) {
            return exp_err->uuid == err->uuid;
        });
        REQUIRE(result != errors.end());
        REQUIRE((*result)->type == exp_err->type);
        REQUIRE((*result)->message == exp_err->message);
        REQUIRE((*result)->description == exp_err->description);
        REQUIRE((*result)->origin == exp_err->origin);
        REQUIRE((*result)->severity == exp_err->severity);
        REQUIRE(Everest::Date::to_rfc3339((*result)->timestamp) == Everest::Date::to_rfc3339(exp_err->timestamp));
        REQUIRE((*result)->state == exp_err->state);
        REQUIRE((*result)->sub_type == exp_err->sub_type);
        REQUIRE((*result)->vendor_id == exp_err->vendor_id);
        REQUIRE((*result)->origin.mapping.has_value() == exp_err->origin.mapping.has_value());
        if (exp_err->origin.mapping.has_value()) {
            REQUIRE((*result)->origin.mapping.value().evse == exp_err->origin.mapping.value().evse);
            REQUIRE((*result)->origin.mapping.value().connector == exp_err->origin.mapping.value().connector);
        }
        REQUIRE((*result)->trigger_value.has_value() == exp_err->trigger_value.has_value());
        if (exp_err->trigger_value.has_value()) {
            REQUIRE((*result)->trigger_value.value().text_repr == exp_err->trigger_value.value().text_repr);
        }
    }
}

TestDatabase::TestDatabase(const fs::path& db_path_, const bool reset_) :
    db_path(db_path_), db(std::make_unique<module::ErrorDatabaseSqlite>(db_path_, reset_)) {
}

TestDatabase::~TestDatabase() {
    db.reset();
    for (const auto* suffix : {"", "-wal", "-shm"}) {
        fs::remove(db_path.string() + suffix);
    }
}

void TestDatabase::add_error(Everest::error::ErrorPtr error) {
    db->add_error(error);
}

std::list<Everest::error::ErrorPtr>
TestDatabase::get_errors(const std::list<Everest::error::ErrorFilter>& filters) const {
    return db->get_errors(filters);
}

std::list<Everest::error::ErrorPtr> TestDatabase::edit_errors(const std::list<Everest::error::ErrorFilter>& filters,
                                                              Everest::error::ErrorDatabase::EditErrorFunc edit_func) {
    return db->edit_errors(filters, edit_func);
}

std::list<Everest::error::ErrorPtr> TestDatabase::remove_errors(const std::list<Everest::error::ErrorFilter>& filters) {
    return db->remove_errors(filters);
}
