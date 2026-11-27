// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_reset.cpp
/// \brief Tests that a Reset.req keeps ongoing transactions when the internal key ResumeTransactionsOnReset is set.

#include "charge_point_test_base.hpp"

#include <ocpp/v16/messages/Reset.hpp>

#include <optional>
#include <vector>

namespace ocpp {
namespace v16 {

namespace {
constexpr std::int32_t CONNECTOR = 1;
const std::string SESSION_ID = "session-1";
constexpr std::int32_t TRANSACTION_ID = 42;
} // namespace

class ChargePointResetTest : public ChargePointTestBase {
protected:
    nlohmann::json response_payload_for_locked(const std::string& action) override {
        if (action == "StartTransaction") {
            return nlohmann::json{{"idTagInfo", {{"status", "Accepted"}}}, {"transactionId", TRANSACTION_ID}};
        }
        return ChargePointTestBase::response_payload_for_locked(action);
    }

    void set_internal_key(const std::string& key, const nlohmann::json& value) {
        const auto user_config_path = this->tmp_dir / "user_config.json";
        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        auto config = nlohmann::json::parse(ifs);
        config["Internal"][key] = value;
        this->configuration =
            std::make_unique<ChargePointConfiguration>(config.dump(), CONFIG_DIR_V16, user_config_path);
    }

    std::unique_ptr<ChargePointImpl> start_with_transaction() {
        auto charge_point = make_charge_point();
        charge_point->register_stop_transaction_callback([this](std::int32_t, Reason reason) {
            const std::lock_guard<std::mutex> lock(this->mtx);
            this->stop_reasons.push_back(reason);
            return true;
        });
        charge_point->register_is_reset_allowed_callback([](const ResetType&) { return true; });
        charge_point->register_reset_callback([this](const ResetType& type) {
            {
                const std::lock_guard<std::mutex> lock(this->mtx);
                this->reset_type = type;
            }
            this->cv.notify_all();
        });
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        EXPECT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));

        charge_point->on_session_started(CONNECTOR, SESSION_ID, ocpp::SessionStartedReason::EVConnected, std::nullopt);
        charge_point->on_transaction_started(CONNECTOR, SESSION_ID, "TOKEN", /*meter_start=*/0.0,
                                             /*reservation_id=*/std::nullopt, ocpp::DateTime(),
                                             /*signed_meter_value=*/std::nullopt);
        EXPECT_TRUE(wait_for_action_count("StartTransaction", 1, WAIT_TIMEOUT));
        EXPECT_TRUE(wait_for_all_answered());
        return charge_point;
    }

    void send_reset(ResetType type) {
        ResetRequest request;
        request.type = type;
        const ocpp::Call<ResetRequest> call(request);
        this->to_charge_point(nlohmann::json(call).dump());
    }

    bool wait_for_reset_callback() {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, WAIT_TIMEOUT, [this]() { return this->reset_type.has_value(); });
    }

    std::vector<Reason> stop_reasons;
    std::optional<ResetType> reset_type;
};

TEST_F(ChargePointResetTest, ResumeTransactionsOnResetKeepsTransactions) {
    set_internal_key("ResumeTransactionsOnReset", true);
    ASSERT_EQ(this->configuration->getResumeTransactionsOnReset(), true);
    auto charge_point = start_with_transaction();

    send_reset(ResetType::Hard);
    ASSERT_TRUE(wait_for_reset_callback());
    EXPECT_EQ(this->reset_type, ResetType::Hard);

    // as OCPPmulti does before resetting the system
    charge_point->stop();

    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_TRUE(this->stop_reasons.empty());
    EXPECT_EQ(this->count_action_locked("StopTransaction"), 0);
}

// A charge point restarted after a reset that did not happen stops its transactions as usual
TEST_F(ChargePointResetTest, RestartAfterResetWithResumptionStopsTransactionsAgain) {
    set_internal_key("ResumeTransactionsOnReset", true);
    auto charge_point = start_with_transaction();
    send_reset(ResetType::Hard);
    ASSERT_TRUE(wait_for_reset_callback());
    charge_point->stop();

    ASSERT_TRUE(charge_point->restart({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                                      BootReasonEnum::PowerUp));
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
    charge_point->on_session_started(CONNECTOR, "session-2", ocpp::SessionStartedReason::EVConnected, std::nullopt);
    charge_point->on_transaction_started(CONNECTOR, "session-2", "TOKEN", /*meter_start=*/0.0,
                                         /*reservation_id=*/std::nullopt, ocpp::DateTime(),
                                         /*signed_meter_value=*/std::nullopt);
    charge_point->stop();

    const std::lock_guard<std::mutex> lock(this->mtx);
    ASSERT_EQ(this->stop_reasons.size(), 1);
    EXPECT_EQ(this->stop_reasons.front(), Reason::Other);
}

TEST_F(ChargePointResetTest, ResetStopsTransactionsByDefault) {
    auto charge_point = start_with_transaction();
    EXPECT_FALSE(this->configuration->getResumeTransactionsOnReset().has_value());

    send_reset(ResetType::Soft);

    {
        const std::lock_guard<std::mutex> lock(this->mtx);
        ASSERT_EQ(this->stop_reasons.size(), 1);
        EXPECT_EQ(this->stop_reasons.front(), Reason::SoftReset);
    }
    // ends the wait of the reset thread for the stopped transaction
    charge_point->on_transaction_stopped(CONNECTOR, SESSION_ID, Reason::SoftReset, ocpp::DateTime(),
                                         /*energy_wh_import=*/0.0F, std::nullopt, std::nullopt, std::nullopt);
    EXPECT_TRUE(wait_for_reset_callback());
    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
