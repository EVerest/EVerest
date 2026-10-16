// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_offline_authorization.cpp
/// \brief Tests of the offline authorization of id_tags from the AuthorizationCache in ChargePointImpl.
///
/// The test configuration points to a CSMS that is not running, so the charge point stays offline.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

// charge_point_impl.hpp uses ChargePointStates and ErrorInfo without including their declarations
#include <ocpp/v16/charge_point_state_machine.hpp>

#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_impl.hpp>
#include <ocpp/v16/database_handler.hpp>

#include "evse_security_mock.hpp"

namespace fs = std::filesystem;

using ::testing::NiceMock;

namespace ocpp {
namespace v16 {

class ChargePointOfflineAuthorizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();

        this->tmp_dir = fs::temp_directory_path() / ("ocpp_v16_offline_authorization_test_" +
                                                     std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(this->tmp_dir);

        const auto user_config = this->tmp_dir / "user_config.json";
        std::ofstream(user_config) << "{}";
        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        auto config = nlohmann::json::parse(ifs);
        config["Core"]["LocalAuthorizeOffline"] = true;
        config["Core"]["AuthorizationCacheEnabled"] = true;
        config["Core"]["AllowOfflineTxForUnknownId"] = true;
        this->configuration = std::make_unique<ChargePointConfiguration>(config.dump(), CONFIG_DIR_V16, user_config);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    void insert_authorization_cache_entry(const CiString<20>& id_tag, const IdTagInfo& id_tag_info) {
        DatabaseHandler database_handler(std::make_unique<everest::db::sqlite::Connection>(
                                             this->tmp_dir / (this->configuration->getChargePointId() + ".db")),
                                         fs::path(MIGRATION_FILES_LOCATION_V16),
                                         this->configuration->getNumberOfConnectors());
        database_handler.open_connection();
        database_handler.insert_or_update_authorization_cache_entry(id_tag, id_tag_info);
    }

    std::unique_ptr<ChargePointImpl> make_charge_point() {
        return std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, /*security_configuration=*/std::nullopt);
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;
};

// An id_tag the CSMS rejected is known from the authorization cache, so AllowOfflineTxForUnknownId must not accept
// it while offline.
TEST_F(ChargePointOfflineAuthorizationTest, OfflineAuthorizationRejectsCachedNonAcceptedIdTag) {
    const CiString<20> id_tag("BLOCKED_TAG");
    IdTagInfo id_tag_info;
    id_tag_info.status = AuthorizationStatus::Blocked;
    insert_authorization_cache_entry(id_tag, id_tag_info);

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});

    EXPECT_EQ(charge_point->authorize_id_token(id_tag).id_tag_info.status, AuthorizationStatus::Blocked);

    charge_point->stop();
}

// Invalid means the identifier is unknown (7.2), so AllowOfflineTxForUnknownId applies while offline.
TEST_F(ChargePointOfflineAuthorizationTest, OfflineAuthorizationAcceptsCachedInvalidIdTagAsUnknown) {
    const CiString<20> id_tag("INVALID_TAG");
    IdTagInfo id_tag_info;
    id_tag_info.status = AuthorizationStatus::Invalid;
    insert_authorization_cache_entry(id_tag, id_tag_info);

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});

    EXPECT_EQ(charge_point->authorize_id_token(id_tag).id_tag_info.status, AuthorizationStatus::Accepted);

    charge_point->stop();
}

// 3.5.4: identifiers that were valid but are expired due to passage of time must be rejected while offline.
TEST_F(ChargePointOfflineAuthorizationTest, OfflineAuthorizationRejectsCachedIdTagExpiredOverTime) {
    const CiString<20> id_tag("EXPIRED_TAG");
    IdTagInfo id_tag_info;
    id_tag_info.status = AuthorizationStatus::Accepted;
    id_tag_info.expiryDate = DateTime(date::utc_clock::now() - std::chrono::hours(1));
    insert_authorization_cache_entry(id_tag, id_tag_info);

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});

    EXPECT_EQ(charge_point->authorize_id_token(id_tag).id_tag_info.status, AuthorizationStatus::Expired);

    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
