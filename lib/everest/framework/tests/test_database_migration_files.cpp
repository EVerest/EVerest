// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_all.hpp>

#include <everest/database/sqlite/connection.hpp>
#include <everest/database/sqlite/schema_updater.hpp>
#include <tests/helpers.hpp>

using namespace everest::db::sqlite;

namespace {

std::string migrations_dir() {
    return Everest::tests::get_bin_dir().string() + "/migrations";
}

} // namespace

TEST_CASE("Framework migration files - step by step", "[framework_migration]") {
    Connection db("file::memory:?cache=shared");
    REQUIRE(db.open_connection());
    SchemaUpdater updater{&db};
    const auto mdir = migrations_dir();

    for (uint32_t i = 1; i <= 4; i++) {
        REQUIRE(updater.apply_migration_files(mdir, i));
        REQUIRE(db.get_user_version() == i);
    }

    for (uint32_t i = 4; i > 0; i--) {
        REQUIRE(updater.apply_migration_files(mdir, i));
        REQUIRE(db.get_user_version() == i);
    }
}

TEST_CASE("Framework migration files - at once", "[framework_migration]") {
    Connection db("file::memory:?cache=shared");
    REQUIRE(db.open_connection());
    SchemaUpdater updater{&db};
    const auto mdir = migrations_dir();

    REQUIRE(updater.apply_migration_files(mdir, 4));
    REQUIRE(db.get_user_version() == 4);

    REQUIRE(updater.apply_migration_files(mdir, 1));
    REQUIRE(db.get_user_version() == 1);
}
