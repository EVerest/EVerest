// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_stop_transaction_auth_cache.cpp
/// \brief Tests that ChargePointImpl caches the idTagInfo of a StopTransaction.conf.

#include "charge_point_test_base.hpp"

#include <ocpp/v16/database_handler.hpp>

namespace ocpp {
namespace v16 {

namespace {
constexpr std::int32_t CONNECTOR = 1;
const std::string SESSION_ID = "session-1";
const CiString<20> START_ID_TAG("START_TAG");
const CiString<20> STOP_ID_TAG("STOP_TAG");
constexpr std::int32_t TRANSACTION_ID = 42;
} // namespace

class ChargePointStopTransactionAuthCacheTest : public ChargePointTestBase {
protected:
    nlohmann::json response_payload_for_locked(const std::string& action) override {
        if (action == "StartTransaction") {
            return nlohmann::json{{"idTagInfo", {{"status", "Accepted"}}}, {"transactionId", TRANSACTION_ID}};
        }
        if (action == "StopTransaction") {
            return nlohmann::json{{"idTagInfo", {{"status", "Blocked"}}}};
        }
        return ChargePointTestBase::response_payload_for_locked(action);
    }

    void enable_user_pricing() {
        std::ifstream config_file(CONFIG_FILE_LOCATION_V16);
        auto config = nlohmann::json::parse(config_file);
        config["CostAndPrice"]["CustomDisplayCostAndPrice"] = true;
        this->configuration = std::make_unique<ChargePointConfiguration>(config.dump(), CONFIG_DIR_V16,
                                                                         this->tmp_dir / "user_config.json");
    }

    void expect_start_id_tag_has_no_session(ChargePointImpl& charge_point) {
        std::optional<TariffMessage> tariff_message;
        charge_point.register_tariff_message_callback([&tariff_message](const TariffMessage& message) {
            tariff_message = message;
            DataTransferResponse response;
            response.status = DataTransferStatus::Accepted;
            return response;
        });

        const nlohmann::json price = {{"idToken", START_ID_TAG.get()}, {"priceText", "0.30/kWh"}};
        const nlohmann::json request = nlohmann::json::array(
            {2,
             "set-user-price",
             "DataTransfer",
             {{"vendorId", "org.openchargealliance.costmsg"}, {"messageId", "SetUserPrice"}, {"data", price.dump()}}});
        this->to_charge_point(request.dump());
        charge_point.register_tariff_message_callback(nullptr);

        EXPECT_THAT(tariff_message,
                    ::testing::Optional(::testing::AllOf(
                        ::testing::Field(&TariffMessage::identifier_type, ::testing::Optional(IdentifierType::IdToken)),
                        ::testing::Field(&TariffMessage::identifier_id, ::testing::Optional(START_ID_TAG.get())))));
    }

    /// \brief Run a transaction started with START_ID_TAG and stopped with \p id_tag_end , and wait until the
    /// StopTransaction.conf was handled
    void run_transaction(ChargePointImpl& charge_point, const std::optional<CiString<20>>& id_tag_end,
                         std::size_t stop_transaction_calls = 1) {
        charge_point.start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                           BootReasonEnum::PowerUp, {});
        charge_point.on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        ASSERT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));

        charge_point.on_session_started(CONNECTOR, SESSION_ID, ocpp::SessionStartedReason::EVConnected, std::nullopt);
        charge_point.on_transaction_started(CONNECTOR, SESSION_ID, START_ID_TAG.get(), /*meter_start=*/0.0,
                                            /*reservation_id=*/std::nullopt, ocpp::DateTime(),
                                            /*signed_meter_value=*/std::nullopt);
        ASSERT_TRUE(wait_for_action_count("StartTransaction", 1, WAIT_TIMEOUT));
        ASSERT_TRUE(wait_for_all_answered());

        charge_point.on_transaction_stopped(CONNECTOR, SESSION_ID, Reason::Local, ocpp::DateTime(),
                                            /*energy_wh_import=*/1000.0F, id_tag_end,
                                            /*signed_meter_value=*/std::nullopt,
                                            /*start_signed_meter_value=*/std::nullopt);
        ASSERT_TRUE(wait_for_action_count("StopTransaction", stop_transaction_calls, WAIT_TIMEOUT));
        ASSERT_TRUE(wait_for_all_answered());
    }

    std::optional<IdTagInfo> cached(const CiString<20>& id_tag) {
        DatabaseHandler database_handler(std::make_unique<everest::db::sqlite::Connection>(
                                             this->tmp_dir / (this->configuration->getChargePointId() + ".db")),
                                         fs::path(MIGRATION_FILES_LOCATION_V16),
                                         this->configuration->getNumberOfConnectors());
        database_handler.open_connection();
        return database_handler.get_authorization_cache_entry(id_tag);
    }
};

// The idTagInfo of a StopTransaction.conf is about the idTag of the StopTransaction.req
TEST_F(ChargePointStopTransactionAuthCacheTest, StopIdTagInfoIsCachedForTheStopIdTag) {
    auto charge_point = make_charge_point();
    run_transaction(*charge_point, STOP_ID_TAG);
    charge_point->stop();

    const auto stop_entry = cached(STOP_ID_TAG);
    ASSERT_TRUE(stop_entry.has_value());
    EXPECT_EQ(stop_entry->status, AuthorizationStatus::Blocked);

    const auto start_entry = cached(START_ID_TAG);
    ASSERT_TRUE(start_entry.has_value());
    EXPECT_EQ(start_entry->status, AuthorizationStatus::Accepted);
}

TEST_F(ChargePointStopTransactionAuthCacheTest, StopIdTagInfoIsCachedWhenStopCallbackWaitsForConfirmation) {
    enable_user_pricing();
    bool response_processed = false;
    auto charge_point = make_charge_point();
    charge_point->register_transaction_stopped_callback([&](const auto&, auto, const auto&) {
        ASSERT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
        response_processed = wait_for_all_answered();
    });
    ASSERT_NO_FATAL_FAILURE(run_transaction(*charge_point, STOP_ID_TAG));
    ASSERT_TRUE(response_processed);
    expect_start_id_tag_has_no_session(*charge_point);
    charge_point->stop();

    EXPECT_THAT(cached(STOP_ID_TAG),
                ::testing::Optional(::testing::Field(&IdTagInfo::status, AuthorizationStatus::Blocked)));
}

class ChargePointStopTransactionRetryTest : public ChargePointStopTransactionAuthCacheTest {
protected:
    nlohmann::json response_payload_for_locked(const std::string& action) override {
        if (action == "StopTransaction" and not this->stop_transaction_rejected) {
            this->stop_transaction_rejected = true;
            return nullptr;
        }
        return ChargePointStopTransactionAuthCacheTest::response_payload_for_locked(action);
    }

    bool stop_transaction_rejected{false};
};

TEST_F(ChargePointStopTransactionRetryTest, StopIdTagInfoIsCachedAfterStopTransactionRetry) {
    enable_user_pricing();
    this->configuration->setTransactionMessageAttempts(2);
    this->configuration->setTransactionMessageRetryInterval(0);
    auto charge_point = make_charge_point();
    ASSERT_NO_FATAL_FAILURE(run_transaction(*charge_point, STOP_ID_TAG, 2));
    expect_start_id_tag_has_no_session(*charge_point);
    charge_point->stop();

    EXPECT_THAT(cached(STOP_ID_TAG),
                ::testing::Optional(::testing::Field(&IdTagInfo::status, AuthorizationStatus::Blocked)));
}

// Without an idTag in the StopTransaction.req, there is no idTag the idTagInfo could be cached for
TEST_F(ChargePointStopTransactionAuthCacheTest, StopIdTagInfoWithoutStopIdTagIsNotCached) {
    auto charge_point = make_charge_point();
    run_transaction(*charge_point, std::nullopt);
    charge_point->stop();

    const auto start_entry = cached(START_ID_TAG);
    ASSERT_TRUE(start_entry.has_value());
    EXPECT_EQ(start_entry->status, AuthorizationStatus::Accepted);
}

} // namespace v16
} // namespace ocpp
