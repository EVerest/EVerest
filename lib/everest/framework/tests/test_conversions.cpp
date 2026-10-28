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

    GIVEN("Valid timestamp") {
        THEN("It should parse") {
            const auto now_utc = date::utc_clock::now();
            const auto now_str = Everest::Date::to_rfc3339(now_utc);
            CHECK(Everest::Date::from_rfc3339(now_str) == Everest::Date::try_from_rfc3339(now_str));
        }
    }
}

SCENARIO("Strict RFC 3339 parsing", "[date]") {
    using namespace std::chrono;
    using date::utc_clock;
    const auto utc = [](date::sys_days day, nanoseconds time_of_day) { return utc_clock::from_sys(day + time_of_day); };
    const auto jan_2 = date::sys_days{date::year{2026} / 1 / 2};

    GIVEN("Valid RFC 3339 date-times") {
        THEN("They parse to the expected time point") {
            CHECK(Everest::Date::try_from_rfc3339("2026-01-02T03:04:05.678Z") == utc(jan_2, 11045678ms));
            CHECK(Everest::Date::try_from_rfc3339("2026-01-02T03:04:05Z") == utc(jan_2, 11045s));
            CHECK(Everest::Date::try_from_rfc3339("2026-01-02t03:04:05z") == utc(jan_2, 11045s));
            CHECK(Everest::Date::try_from_rfc3339("2026-01-02T03:04:05.123456789Z") ==
                  utc(jan_2, 11045s + 123456789ns));
            CHECK(Everest::Date::try_from_rfc3339("2026-01-02T03:04:05.1234567891Z") ==
                  utc(jan_2, 11045s + 123456789ns));
            CHECK(Everest::Date::try_from_rfc3339("2026-01-02T05:04:05+02:00") == utc(jan_2, 11045s));
            CHECK(Everest::Date::try_from_rfc3339("2026-01-01T23:34:05.5-03:30") == utc(jan_2, 11045500ms));
            CHECK(Everest::Date::try_from_rfc3339("2024-02-29T00:00:00Z").has_value());
        }
        THEN("A leap second maps onto the inserted second") {
            const auto leap = Everest::Date::try_from_rfc3339("2016-12-31T23:59:60Z");
            REQUIRE(leap.has_value());
            CHECK(*leap == utc(date::sys_days{date::year{2017} / 1 / 1}, 0s) - 1s);
            CHECK(*leap == utc(date::sys_days{date::year{2016} / 12 / 31}, 86399s) + 1s);
        }
        THEN("from_rfc3339 agrees with try_from_rfc3339") {
            CHECK(Everest::Date::from_rfc3339("2026-01-02T05:04:05.678+02:00") == utc(jan_2, 11045678ms));
        }
    }

    GIVEN("Invalid RFC 3339 date-times") {
        const auto input = GENERATE(
            as<std::string>{}, "", "2026", "2026-01-02", "2026-01-02T03:04:05", "2026-01-02T03:04:05.678",
            "2026-99-02T03:04:05.678Z", "2026-00-02T03:04:05.678Z", "2026-02-30T03:04:05.678Z",
            "2025-02-29T03:04:05.678Z", "2026-01-32T03:04:05.678Z", "2026-01-02T24:04:05.678Z",
            "2026-01-02T03:60:05.678Z", "2026-01-02T03:04:61.678Z", "-026-01-02T03:04:05.678Z",
            "2026-+1-02T03:04:05.678Z", "2026/01/02T03:04:05.678Z", "2026-01-02 03:04:05.678Z",
            "2026-01-02T03-04-05.678Z", "2026-01-02T03:04:05.Z", "2026-01-02T03:04:05.678ZZ",
            "2026-01-02T03:04:05.678Z garbage", "2026-01-02T03:04:05+0200", "2026-01-02T03:04:05+24:00",
            "2026-01-02T03:04:05+02:60", "2026-01-02T03:04:05+02", "9999-99-99T99:99:99.999Z", "2026-01-02T23:59:60Z");
        THEN("try_from_rfc3339 rejects " << input) {
            CHECK_FALSE(Everest::Date::try_from_rfc3339(input).has_value());
        }
    }

    GIVEN("Date-times that only the lenient fallback accepts") {
        THEN("from_rfc3339 ignores the UTC offset and anything after the seconds") {
            CHECK(Everest::Date::from_rfc3339("2026-01-02T03:04:05") == utc(jan_2, 11045s));
            CHECK(Everest::Date::from_rfc3339("2026-01-02T03:04:05.678") == utc(jan_2, 11045678ms));
            CHECK(Everest::Date::from_rfc3339("2026-01-02T03:04:05.678+0200") == utc(jan_2, 11045678ms));
            CHECK(Everest::Date::from_rfc3339("2026-01-02T03:04:05+24:00") == utc(jan_2, 11045s));
            CHECK(Everest::Date::from_rfc3339("2026-01-02T03:04:05.Z garbage") == utc(jan_2, 11045s));
        }
    }

    GIVEN("The first and last nanosecond of utc_clock") {
        // the limits end in ...192Z and ...807Z, so changing the last digit moves them by one nanosecond
        const auto first = date::format("%FT%TZ", utc_clock::time_point::min());
        const auto last = date::format("%FT%TZ", utc_clock::time_point::max());
        auto before_first = first;
        before_first[before_first.size() - 2] -= 1;
        auto after_last = last;
        after_last[after_last.size() - 2] += 1;

        THEN("They parse to the limits") {
            CHECK(Everest::Date::try_from_rfc3339(first) == utc_clock::time_point::min());
            CHECK(Everest::Date::try_from_rfc3339(last) == utc_clock::time_point::max());
            // without the offset, only the lenient fallback accepts them
            CHECK(Everest::Date::from_rfc3339(first.substr(0, first.size() - 1)) == utc_clock::time_point::min());
            CHECK(Everest::Date::from_rfc3339(last.substr(0, last.size() - 1)) == utc_clock::time_point::max());
        }
        THEN("One nanosecond beyond yields the epoch") {
            CHECK(Everest::Date::from_rfc3339(before_first) == utc_clock::time_point{});
            CHECK(Everest::Date::from_rfc3339(after_last) == utc_clock::time_point{});
        }
    }

    GIVEN("Date-times outside the range of utc_clock") {
        const auto input =
            GENERATE(as<std::string>{}, "0000-01-01T00:00:00Z", "1600-01-01T00:00:00Z", "1677-09-21T00:00:00Z",
                     "2262-04-12T00:00:00Z", "2300-01-01T00:00:00Z", "9999-12-31T23:59:59Z", "2300-01-01T00:00:00");
        THEN("from_rfc3339 yields the epoch for " << input) {
            CHECK(Everest::Date::from_rfc3339(input) == utc_clock::time_point{});
        }
    }

    GIVEN("Fields wider than two digits") {
        const auto input =
            GENERATE(as<std::string>{}, "2026-01-02T03:04:09999999995", "2262-04-01T23:59:5999999.9999-23:59",
                     "2026-01-02T03:04:05999Z", "2026-01-02T003:04:05Z", "02026-01-02T03:04:05Z");
        THEN("from_rfc3339 yields the epoch for " << input) {
            CHECK(Everest::Date::from_rfc3339(input) == utc_clock::time_point{});
        }
    }

    GIVEN("Out-of-range fields in a full-length date-time") {
        THEN("from_rfc3339 yields the epoch instead of a normalized date") {
            const utc_clock::time_point epoch{};
            CHECK(Everest::Date::from_rfc3339("2026-99-02T03:04:05.678Z") == epoch);
            CHECK(Everest::Date::from_rfc3339("2026-01-02T77:04:05.678Z") == epoch);
            CHECK(Everest::Date::from_rfc3339("2026-02-30T03:04:05.678Z") == epoch);
            CHECK(Everest::Date::from_rfc3339("2026-01-02T23:59:60") == epoch);
            CHECK(Everest::Date::from_rfc3339("garbage") == epoch);
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
