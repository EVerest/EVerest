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

    /// \brief Run a transaction started with START_ID_TAG and stopped with \p id_tag_end , and wait until the
    /// StopTransaction.conf was handled
    void run_transaction(const std::optional<CiString<20>>& id_tag_end) {
        auto charge_point = make_charge_point();
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        ASSERT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));

        charge_point->on_session_started(CONNECTOR, SESSION_ID, ocpp::SessionStartedReason::EVConnected, std::nullopt);
        charge_point->on_transaction_started(CONNECTOR, SESSION_ID, START_ID_TAG.get(), /*meter_start=*/0.0,
                                             /*reservation_id=*/std::nullopt, ocpp::DateTime(),
                                             /*signed_meter_value=*/std::nullopt);
        ASSERT_TRUE(wait_for_action_count("StartTransaction", 1, WAIT_TIMEOUT));
        ASSERT_TRUE(wait_for_all_answered());

        charge_point->on_transaction_stopped(CONNECTOR, SESSION_ID, Reason::Local, ocpp::DateTime(),
                                             /*energy_wh_import=*/1000.0F, id_tag_end,
                                             /*signed_meter_value=*/std::nullopt,
                                             /*start_signed_meter_value=*/std::nullopt);
        ASSERT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
        ASSERT_TRUE(wait_for_all_answered());
        charge_point->stop();
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
    run_transaction(STOP_ID_TAG);

    const auto stop_entry = cached(STOP_ID_TAG);
    ASSERT_TRUE(stop_entry.has_value());
    EXPECT_EQ(stop_entry->status, AuthorizationStatus::Blocked);

    const auto start_entry = cached(START_ID_TAG);
    ASSERT_TRUE(start_entry.has_value());
    EXPECT_EQ(start_entry->status, AuthorizationStatus::Accepted);
}

// Without an idTag in the StopTransaction.req, there is no idTag the idTagInfo could be cached for
TEST_F(ChargePointStopTransactionAuthCacheTest, StopIdTagInfoWithoutStopIdTagIsNotCached) {
    run_transaction(std::nullopt);

    const auto start_entry = cached(START_ID_TAG);
    ASSERT_TRUE(start_entry.has_value());
    EXPECT_EQ(start_entry->status, AuthorizationStatus::Accepted);
}

} // namespace v16
} // namespace ocpp
