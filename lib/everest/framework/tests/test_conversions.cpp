// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <utils/conversions.hpp>
#include <utils/date.hpp>
#include <utils/types.hpp>

SCENARIO("Check conversions", "[!throws]") {
    GIVEN("Valid CmdErrors") {
        THEN("It shouldn't throw") {
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::MessageParsingError) ==
                  "MessageParsingError");

            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::SchemaValidationError) ==
                  "SchemaValidationError");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::HandlerException) ==
                  "HandlerException");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::CmdTimeout) == "CmdTimeout");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::Shutdown) == "Shutdown");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::NotReady) == "NotReady");
        }
    }

    GIVEN("Invalid CmdErrors") {
        THEN("It should throw") {
            CHECK_THROWS(Everest::conversions::cmd_error_type_to_string(static_cast<Everest::CmdErrorType>(-1)));
        }
    }

    GIVEN("Valid CmdError strings") {
        THEN("It shouldn't throw") {
            CHECK(Everest::conversions::string_to_cmd_error_type("MessageParsingError") ==
                  Everest::CmdErrorType::MessageParsingError);

            CHECK(Everest::conversions::string_to_cmd_error_type("SchemaValidationError") ==
                  Everest::CmdErrorType::SchemaValidationError);
            CHECK(Everest::conversions::string_to_cmd_error_type("HandlerException") ==
                  Everest::CmdErrorType::HandlerException);
            CHECK(Everest::conversions::string_to_cmd_error_type("CmdTimeout") == Everest::CmdErrorType::CmdTimeout);
            CHECK(Everest::conversions::string_to_cmd_error_type("Shutdown") == Everest::CmdErrorType::Shutdown);
            CHECK(Everest::conversions::string_to_cmd_error_type("NotReady") == Everest::CmdErrorType::NotReady);
        }
    }

    GIVEN("Invalid CmdError strings") {
        THEN("It should throw") {
            CHECK_THROWS(Everest::conversions::string_to_cmd_error_type("ThisIsAnInvalidCmdErrorString"));
        }
    }

    GIVEN("Valid CmdErrorError") {
        THEN("It shouldn't throw") {
            Everest::CmdResultError cmd_result_error = {Everest::CmdErrorType::Shutdown, "message", nullptr};
            json cmd_result_error_json = {{Everest::conversions::ERROR_TYPE, "Shutdown"},
                                          {Everest::conversions::ERROR_MSG, "message"}};
            Everest::CmdResultError cmd_result_error_from_json = cmd_result_error_json;
            CHECK(json(cmd_result_error) == cmd_result_error_json);
            CHECK(json(cmd_result_error_from_json) == cmd_result_error_json);
        }
    }
}

SCENARIO("tzdb preload makes concurrent utc_clock::now() safe", "[!throws]") {
    GIVEN("A single-threaded tzdb preload before any worker thread") {
        Everest::Date::preload_tzdb();

        WHEN("Many threads call utc_clock::now() concurrently") {
            constexpr int thread_count = 16;
            constexpr int iterations = 2000;
            std::atomic<int> parked{0};
            std::atomic<bool> go{false};
            std::atomic<int> failures{0};

            std::vector<std::thread> threads;
            threads.reserve(thread_count);
            for (int i = 0; i < thread_count; ++i) {
                threads.emplace_back([&] {
                    parked.fetch_add(1);
                    while (!go.load()) {
                    }
                    for (int k = 0; k < iterations; ++k) {
                        try {
                            const auto ts = Everest::Date::to_rfc3339(date::utc_clock::now());
                            if (ts.empty()) {
                                failures.fetch_add(1);
                            }
                        } catch (...) {
                            failures.fetch_add(1);
                        }
                    }
                });
            }
            while (parked.load() < thread_count) {
            }
            go.store(true);
            for (auto& t : threads) {
                t.join();
            }

            THEN("No call crashes, throws, or yields an empty timestamp") {
                CHECK(failures.load() == 0);
            }
        }
    }
}
