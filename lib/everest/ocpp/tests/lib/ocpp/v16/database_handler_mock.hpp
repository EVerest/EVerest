// SPDX-License-Identifier: Apache-2.0

#ifndef OCPP_DATABASE_HANDLE_MOCK_H
#define OCPP_DATABASE_HANDLE_MOCK_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v16/database_handler.hpp>

namespace ocpp {

class DatabaseHandlerMock : public v16::DatabaseHandler {
public:
    DatabaseHandlerMock(std::unique_ptr<everest::db::sqlite::ConnectionInterface> database,
                        const fs::path& init_script_path) :
        DatabaseHandler(std::move(database), init_script_path, 2) {};
    MOCK_METHOD(void, insert_or_update_charging_profile, (const int, const v16::ChargingProfile&), (override));
    MOCK_METHOD(void, delete_charging_profile, (const int profile_id), (override));
    MOCK_METHOD(std::vector<int32_t>, get_charging_profile_ids_by_connector_id, (const int connector_id), (override));
};

} // namespace ocpp

#endif // DATABASE_HANDLE_MOCK_H