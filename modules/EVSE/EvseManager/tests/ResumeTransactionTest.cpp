// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <resume_transaction.hpp>
#include <utils/date.hpp>

namespace {
using namespace module;
using namespace std::chrono_literals;

const ResumeSituations all_situations{ResumeSituation::PowerLoss, ResumeSituation::Crash, ResumeSituation::Restart,
                                      ResumeSituation::Reboot, ResumeSituation::Unknown};

TEST(ParseResumeSituations, NeverIsEmpty) {
    EXPECT_EQ(parse_resume_situations("Never"), ResumeSituations{});
}

TEST(ParseResumeSituations, AlwaysIncludesUnknown) {
    EXPECT_EQ(parse_resume_situations("Always"), all_situations);
}

TEST(ParseResumeSituations, SingleValues) {
    EXPECT_EQ(parse_resume_situations("PowerLoss"), ResumeSituations{ResumeSituation::PowerLoss});
    EXPECT_EQ(parse_resume_situations("Crash"), ResumeSituations{ResumeSituation::Crash});
    EXPECT_EQ(parse_resume_situations("Restart"), ResumeSituations{ResumeSituation::Restart});
    EXPECT_EQ(parse_resume_situations("Reboot"), ResumeSituations{ResumeSituation::Reboot});
}

TEST(ParseResumeSituations, ListWithWhitespace) {
    EXPECT_EQ(parse_resume_situations("PowerLoss,Crash"),
              (ResumeSituations{ResumeSituation::PowerLoss, ResumeSituation::Crash}));
    EXPECT_EQ(parse_resume_situations(" PowerLoss , Reboot "),
              (ResumeSituations{ResumeSituation::PowerLoss, ResumeSituation::Reboot}));
    EXPECT_EQ(parse_resume_situations("Crash,Crash"), ResumeSituations{ResumeSituation::Crash});
}

TEST(ParseResumeSituations, InvalidValues) {
    EXPECT_FALSE(parse_resume_situations("").has_value());
    EXPECT_FALSE(parse_resume_situations("Unknown").has_value());
    EXPECT_FALSE(parse_resume_situations("powerloss").has_value());
    EXPECT_FALSE(parse_resume_situations("PowerLoss,Foo").has_value());
    EXPECT_FALSE(parse_resume_situations("Crash,").has_value());
    EXPECT_FALSE(parse_resume_situations("Never,Crash").has_value());
    EXPECT_FALSE(parse_resume_situations("Always,Crash").has_value());
    EXPECT_FALSE(parse_resume_situations("Always,Never").has_value());
}

TEST(ResumeSituationToString, NamesMatchConfigValues) {
    EXPECT_EQ(resume_situation_to_string(ResumeSituation::PowerLoss), "PowerLoss");
    EXPECT_EQ(resume_situation_to_string(ResumeSituation::Crash), "Crash");
    EXPECT_EQ(resume_situation_to_string(ResumeSituation::Restart), "Restart");
    EXPECT_EQ(resume_situation_to_string(ResumeSituation::Reboot), "Reboot");
    EXPECT_EQ(resume_situation_to_string(ResumeSituation::Unknown), "Unknown");
}

StoredTransaction stored_on_boot(const std::optional<std::string>& boot_id, bool clean_shutdown) {
    StoredTransaction stored;
    stored.session_uuid = "session-1";
    stored.boot_id = boot_id;
    stored.clean_shutdown = clean_shutdown;
    return stored;
}

TEST(DetectResumeSituation, FromBootIdAndCleanShutdown) {
    EXPECT_EQ(detect_resume_situation(stored_on_boot("boot-1", false), "boot-2"), ResumeSituation::PowerLoss);
    EXPECT_EQ(detect_resume_situation(stored_on_boot("boot-1", false), "boot-1"), ResumeSituation::Crash);
    EXPECT_EQ(detect_resume_situation(stored_on_boot("boot-1", true), "boot-1"), ResumeSituation::Restart);
    EXPECT_EQ(detect_resume_situation(stored_on_boot("boot-1", true), "boot-2"), ResumeSituation::Reboot);
}

TEST(DetectResumeSituation, MissingBootIdIsUnknown) {
    EXPECT_EQ(detect_resume_situation(stored_on_boot(std::nullopt, false), "boot-2"), ResumeSituation::Unknown);
    EXPECT_EQ(detect_resume_situation(stored_on_boot("boot-1", false), std::nullopt), ResumeSituation::Unknown);
}

TEST(StoredTransactionJson, RoundTrip) {
    StoredTransaction stored = stored_on_boot("boot-1", true);
    types::authorization::ProvidedIdToken token;
    token.id_token.value = "TOKEN";
    token.id_token.type = types::authorization::IdTokenType::ISO14443;
    token.authorization_type = types::authorization::AuthorizationType::RFID;
    stored.id_token = token;
    types::units_signed::SignedMeterValue start;
    start.signed_meter_data = "OCMF|{}";
    start.signing_method = "ECDSA";
    start.encoding_method = "OCMF";
    stored.start_signed_meter_value = start;
    stored.transaction_started_at = "2026-11-24T10:00:00.000Z";
    stored.last_alive = "2026-11-24T10:05:00.000Z";

    const nlohmann::json j = stored;
    const StoredTransaction parsed = j;

    EXPECT_EQ(parsed.session_uuid, "session-1");
    ASSERT_TRUE(parsed.id_token.has_value());
    EXPECT_EQ(parsed.id_token->id_token.value, "TOKEN");
    EXPECT_EQ(parsed.id_token->authorization_type, types::authorization::AuthorizationType::RFID);
    ASSERT_TRUE(parsed.start_signed_meter_value.has_value());
    EXPECT_EQ(parsed.start_signed_meter_value->signed_meter_data, "OCMF|{}");
    EXPECT_EQ(parsed.transaction_started_at, "2026-11-24T10:00:00.000Z");
    EXPECT_EQ(parsed.last_alive, "2026-11-24T10:05:00.000Z");
    EXPECT_EQ(parsed.boot_id, "boot-1");
    EXPECT_TRUE(parsed.clean_shutdown);
    EXPECT_FALSE(parsed.resume_after_reset);
}

TEST(StoredTransactionJson, ResumeAfterResetRoundTrip) {
    StoredTransaction stored = stored_on_boot("boot-1", true);
    stored.resume_after_reset = true;
    stored.energy_transfer_paused = true;
    const nlohmann::json j = stored;
    const StoredTransaction parsed = j;
    EXPECT_TRUE(parsed.resume_after_reset);
    EXPECT_TRUE(parsed.energy_transfer_paused);
}

TEST(StoredTransactionJson, OptionalFieldsMayBeMissing) {
    const StoredTransaction parsed = nlohmann::json{{"session_uuid", "session-1"}};
    EXPECT_EQ(parsed.session_uuid, "session-1");
    EXPECT_FALSE(parsed.id_token.has_value());
    EXPECT_FALSE(parsed.start_signed_meter_value.has_value());
    EXPECT_FALSE(parsed.last_alive.has_value());
    EXPECT_FALSE(parsed.boot_id.has_value());
    EXPECT_FALSE(parsed.clean_shutdown);
    EXPECT_FALSE(parsed.resume_after_reset);
}

struct DecideResumeTest : public testing::Test {
    const std::chrono::time_point<date::utc_clock> now = Everest::Date::from_rfc3339("2026-11-24T10:10:00.000Z");
    StoredTransaction stored = stored_on_boot("boot-1", false);
    ResumeLimits limits{{ResumeSituation::PowerLoss}, 300s};

    void SetUp() override {
        types::authorization::ProvidedIdToken token;
        token.id_token.value = "TOKEN";
        token.id_token.type = types::authorization::IdTokenType::ISO14443;
        token.authorization_type = types::authorization::AuthorizationType::RFID;
        stored.id_token = token;
        stored.last_alive = "2026-11-24T10:06:00.000Z";
    }
};

TEST_F(DecideResumeTest, ResumesAllowedSituationWithinOutage) {
    const auto decision = decide_resume(stored, "boot-2", limits, now);
    EXPECT_TRUE(decision.resume);
}

TEST_F(DecideResumeTest, NeverResumesWhenDisabled) {
    limits.situations = {};
    const auto decision = decide_resume(stored, "boot-2", limits, now);
    EXPECT_FALSE(decision.resume);
    EXPECT_FALSE(decision.reason.empty());
}

TEST_F(DecideResumeTest, InvalidConfigurationIsReportedAsSuch) {
    limits.situations = {};
    limits.valid_configuration = false;
    const auto decision = decide_resume(stored, "boot-2", limits, now);
    EXPECT_FALSE(decision.resume);
    EXPECT_NE(decision.reason.find("invalid"), std::string::npos);
    EXPECT_EQ(decision.reason.find("Never"), std::string::npos);
}

TEST_F(DecideResumeTest, SituationNotConfigured) {
    const auto decision = decide_resume(stored, "boot-1", limits, now);
    EXPECT_FALSE(decision.resume);
    EXPECT_NE(decision.reason.find("Crash"), std::string::npos);
}

TEST_F(DecideResumeTest, LegacyRecordWithoutTokenIsNotResumed) {
    stored.id_token.reset();
    EXPECT_FALSE(decide_resume(stored, "boot-2", limits, now).resume);
}

TEST_F(DecideResumeTest, OutageTooLong) {
    stored.last_alive = "2026-11-24T10:04:59.000Z";
    EXPECT_FALSE(decide_resume(stored, "boot-2", limits, now).resume);
}

TEST_F(DecideResumeTest, OutageExactlyAtLimit) {
    stored.last_alive = "2026-11-24T10:05:00.000Z";
    EXPECT_TRUE(decide_resume(stored, "boot-2", limits, now).resume);
}

// e.g. a board without a real-time clock before NTP has set the time
TEST_F(DecideResumeTest, ClockBeforeLastAliveIsNotPlausible) {
    stored.last_alive = "2026-11-24T10:11:00.000Z";
    const auto decision = decide_resume(stored, "boot-2", limits, now);
    EXPECT_FALSE(decision.resume);
    EXPECT_NE(decision.reason.find("resume_transaction_max_outage_s"), std::string::npos) << decision.reason;
}

TEST_F(DecideResumeTest, MissingLastAliveWithLimit) {
    stored.last_alive.reset();
    EXPECT_FALSE(decide_resume(stored, "boot-2", limits, now).resume);
}

TEST_F(DecideResumeTest, ZeroLimitDisablesOutageCheck) {
    limits.max_outage = 0s;
    stored.last_alive = "2020-01-01T00:00:00.000Z";
    EXPECT_TRUE(decide_resume(stored, "boot-2", limits, now).resume);
    stored.last_alive.reset();
    EXPECT_TRUE(decide_resume(stored, "boot-2", limits, now).resume);
}

TEST_F(DecideResumeTest, UnknownSituationOnlyResumedByAlways) {
    EXPECT_FALSE(decide_resume(stored, std::nullopt, limits, now).resume);
    limits.situations = all_situations;
    EXPECT_TRUE(decide_resume(stored, std::nullopt, limits, now).resume);
}

TEST_F(DecideResumeTest, ResetWithResumptionIgnoresSituationsAndOutage) {
    stored.resume_after_reset = true;
    stored.last_alive = "2020-01-01T00:00:00.000Z";
    limits.situations = {};
    const auto decision = decide_resume(stored, "boot-1", limits, now);
    EXPECT_TRUE(decision.resume);
    EXPECT_NE(decision.reason.find("reset"), std::string::npos);
}

TEST_F(DecideResumeTest, ResetWithResumptionIgnoresInvalidConfiguration) {
    stored.resume_after_reset = true;
    limits.valid_configuration = false;
    EXPECT_TRUE(decide_resume(stored, "boot-1", limits, now).resume);
}

TEST_F(DecideResumeTest, ResetWithResumptionNeedsIdToken) {
    stored.resume_after_reset = true;
    stored.id_token.reset();
    EXPECT_FALSE(decide_resume(stored, "boot-1", limits, now).resume);
}

} // namespace
