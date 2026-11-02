// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "helper.hpp"
#include "powermeterImpl.hpp"

#include <cstdlib>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <string_view>
#include <tuple>

namespace {

constexpr std::size_t kFetchCallAlignmentBytes = 8;
constexpr std::size_t kWriteCallAlignmentBytes = 32;
constexpr std::size_t kBytesPerRegister = 2;

struct alignas(kFetchCallAlignmentBytes) FetchCall {
    std::int32_t address;
    std::uint16_t register_count;
};

struct alignas(kWriteCallAlignmentBytes) WriteCall {
    std::int32_t address;
    std::vector<std::uint16_t> data;
};

class FakeModbusTransport : public transport::AbstractModbusTransport {
public:
    void push_fetch_response(std::int32_t address, std::uint16_t register_count, transport::DataVector response) {
        scripted_fetch_[Key{address, register_count}].push_back(std::move(response));
    }

    const std::vector<FetchCall>& fetch_calls() const {
        return fetch_calls_;
    }

    const std::vector<WriteCall>& write_calls() const {
        return write_calls_;
    }

    transport::DataVector fetch(std::int32_t address, std::uint16_t register_count) override {
        fetch_calls_.push_back(FetchCall{address, register_count});

        const Key key{address, register_count};
        auto iter = scripted_fetch_.find(key);
        if (iter == scripted_fetch_.end() || iter->second.empty()) {
            throw std::runtime_error("FakeModbusTransport: no scripted fetch response for address/count");
        }
        transport::DataVector out = std::move(iter->second.front());
        iter->second.pop_front();
        return out;
    }

    void write_multiple_registers(std::int32_t address, const std::vector<std::uint16_t>& data) override {
        write_calls_.push_back(WriteCall{address, data});
    }

private:
    using Key = std::tuple<std::int32_t, std::uint16_t>;
    std::map<Key, std::deque<transport::DataVector>> scripted_fetch_;
    std::vector<FetchCall> fetch_calls_;
    std::vector<WriteCall> write_calls_;
};

transport::DataVector u16_be(std::uint16_t value) {
    constexpr std::uint32_t kByteBits = 8U;
    constexpr std::uint32_t kByteMask = 0xFFU;
    const auto high = static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) >> kByteBits) & kByteMask);
    const auto low = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) & kByteMask);
    return transport::DataVector{high, low};
}

transport::DataVector bytes(std::string_view str) {
    return transport::DataVector{str.begin(), str.end()};
}

transport::DataVector zero_bytes_for_words(std::uint16_t words) {
    return transport::DataVector(words * kBytesPerRegister, 0U);
}

module::main::Conf make_test_conf() {
    constexpr int kDefaultIntervalMs = 1000;
    module::main::Conf conf{};
    conf.powermeter_device_id = 1;
    conf.communication_retry_count = 0;
    conf.communication_retry_delay_ms = 0;
    conf.initial_connection_retry_count = 0;
    conf.initial_connection_retry_delay_ms = 0;
    conf.timezone_offset_minutes = 0;
    conf.live_measurement_interval_ms = kDefaultIntervalMs;
    conf.device_state_read_interval_ms = kDefaultIntervalMs;
    conf.communication_error_pause_delay_s = 0;
    conf.use_system_timezone = false;
    return conf;
}

// Sets the process time zone for the lifetime of the object, without depending on tzdata being installed.
class ScopedTimeZone {
public:
    explicit ScopedTimeZone(const char* posix_tz) {
        const char* previous = std::getenv("TZ");
        if (previous != nullptr) {
            previous_.emplace(previous);
        }
        set(posix_tz);
    }
    ScopedTimeZone(const ScopedTimeZone&) = delete;
    ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;
    ScopedTimeZone(ScopedTimeZone&&) = delete;
    ScopedTimeZone& operator=(ScopedTimeZone&&) = delete;
    ~ScopedTimeZone() {
        if (previous_.has_value()) {
            setenv("TZ", previous_->c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

    static void set(const char* posix_tz) {
        setenv("TZ", posix_tz, 1);
        tzset();
    }

private:
    std::optional<std::string> previous_;
};

std::vector<WriteCall> timezone_writes(const FakeModbusTransport& transport) {
    std::vector<WriteCall> out;
    for (const auto& call : transport.write_calls()) {
        if (call.address == em580::registers::MODBUS_TIMEZONE_OFFSET_ADDRESS) {
            out.push_back(call);
        }
    }
    return out;
}

} // namespace

TEST(EM580PowermeterImpl, StartTransactionHappyPathCountsWrites) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_NOT_READY));
    transport->push_fetch_response(em580::registers::MODBUS_SIGNED_MAP_ADDRESS,
                                   em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256,
                                   zero_bytes_for_words(em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_signed_map_word_count(
        impl, em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256);

    types::powermeter::TransactionReq req{};
    req.evse_id = "DE*TEST*EVSE01";
    req.transaction_id = "12345678-1234-5678-1234-567812345678";
    req.identification_status = types::powermeter::OCMFUserIdentificationStatus::ASSIGNED;
    req.identification_flags = {};
    req.identification_type = types::powermeter::OCMFIdentificationType::ISO14443;
    req.identification_level = types::powermeter::OCMFIdentificationLevel::NONE;
    req.identification_data.emplace("ABC");
    req.tariff_text.emplace("TARIFF");

    const auto resp = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);

    // Happy-path expectation: 8 writes from write_transaction_registers + 2
    // additional writes (session modality + 'B')
    EXPECT_EQ(transport_ptr->write_calls().size(), 10U);
}

TEST(EM580PowermeterImpl, StopTransactionPendingClosedTransactionMismatchReturnsError) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // read_ocmf_file(): size then file bytes
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_SIZE_ADDRESS, 1, u16_be(1));

    const std::string other_uuid = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
    const std::string ocmf_file = R"(OCMF|{"TT":"x<=>)" + other_uuid + R"("}|
{"SA":"ECDSA-brainpoolP384r1-SHA256","SD":"00"})";
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_FILE_ADDRESS, 1, bytes(ocmf_file));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_pending_closed_transaction(impl, true);

    std::string requested_uuid = "12345678-1234-5678-1234-567812345678";
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, requested_uuid);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(*resp.error, "Transaction id mismatch");
    EXPECT_EQ(transport_ptr->write_calls().size(), 0U);
}

TEST(EM580PowermeterImpl, StopTransactionEmptyIdWithoutPendingClosedTransactionCleansUpAndReturnsOk) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // clear_transaction_states() fetches OCMF state once; use NOT_READY to avoid
    // extra cleanup behavior.
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_NOT_READY));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "12345678-1234-5678-1234-567812345678");
    module::main::powermeterImpl::TestAccess::set_pending_closed_transaction(impl, false);

    std::string empty_id;
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, empty_id);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);

    // Expect one write ('E' end command) when no pending closed transaction
    // exists.
    EXPECT_EQ(transport_ptr->write_calls().size(), 1U);
}

TEST(EM580PowermeterImpl, StartTransactionReadyStateCleansUpAndStartsNewTransaction) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // handle_start_transaction() first checks OCMF state.
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_READY));
    // clear_transaction_states(): reads state again, sees READY, reads file
    // (size+file) and confirms NOT_READY.
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_READY));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_SIZE_ADDRESS, 1, u16_be(1));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_FILE_ADDRESS, 1,
                                   bytes("OCMF|{\"TT\":\"x<=>12345678-1234-5678-1234-567812345678\"}|{}"));
    transport->push_fetch_response(em580::registers::MODBUS_SIGNED_MAP_ADDRESS,
                                   em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256,
                                   zero_bytes_for_words(em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_signed_map_word_count(
        impl, em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256);

    types::powermeter::TransactionReq req{};
    req.evse_id = "DE*TEST*EVSE01";
    req.transaction_id = "12345678-1234-5678-1234-567812345678";
    req.identification_status = types::powermeter::OCMFUserIdentificationStatus::ASSIGNED;
    req.identification_flags = {};
    req.identification_type = types::powermeter::OCMFIdentificationType::ISO14443;
    req.identification_level = types::powermeter::OCMFIdentificationLevel::NONE;
    req.identification_data.emplace("ABC");
    req.tariff_text.emplace("TARIFF");

    const auto resp = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);

    // Cleanup confirm write + 10 transaction start writes.
    EXPECT_EQ(transport_ptr->write_calls().size(), 11U);
    EXPECT_EQ(transport_ptr->write_calls()[0].address, em580::registers::MODBUS_OCMF_STATE_ADDRESS);
}

TEST(EM580PowermeterImpl, StopTransactionUnknownIdReturnsErrorAndNoWrites) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_pending_closed_transaction(impl, false);
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "11111111-1111-1111-1111-111111111111");

    std::string requested_uuid = "22222222-2222-2222-2222-222222222222";
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, requested_uuid);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(*resp.error, "No open transaction or unknown transaction id");
    EXPECT_TRUE(transport_ptr->write_calls().empty());
}

TEST(EM580PowermeterImpl, MonitorTransactionStateReadyWhileActiveSetsPendingClosed) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    conf.monitor_transaction_state = true;
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "12345678-1234-5678-1234-567812345678");
    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));

    module::main::powermeterImpl::TestAccess::monitor_transaction_ocmf_state(impl,
                                                                             em580::registers::MODBUS_OCMF_STATE_READY);

    EXPECT_TRUE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));
}

TEST(EM580PowermeterImpl, MonitorTransactionStateReadyWhileInactiveIsIgnored) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    conf.monitor_transaction_state = true;
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    module::main::powermeterImpl::TestAccess::monitor_transaction_ocmf_state(impl,
                                                                             em580::registers::MODBUS_OCMF_STATE_READY);

    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));
}

TEST(EM580PowermeterImpl, MonitorTransactionStateDisabledWhileActiveIsIgnored) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    conf.monitor_transaction_state = false;
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "12345678-1234-5678-1234-567812345678");
    module::main::powermeterImpl::TestAccess::monitor_transaction_ocmf_state(impl,
                                                                             em580::registers::MODBUS_OCMF_STATE_READY);

    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));
}

TEST(EM580TimezoneUtils, LocalTimeZoneFollowsDaylightSavingTime) {
    const ScopedTimeZone time_zone("CET-1CEST,M3.5.0,M10.5.0/3");
    constexpr std::time_t kWinter = 1768478400; // 2026-01-15T12:00:00Z
    constexpr std::time_t kSummer = 1784116800; // 2026-07-15T12:00:00Z
    const auto winter = timezone_utils::local_time_zone(kWinter);
    const auto summer = timezone_utils::local_time_zone(kSummer);
    ASSERT_TRUE(winter.has_value());
    ASSERT_TRUE(summer.has_value());
    EXPECT_EQ(winter->utc_offset_minutes, 60);
    EXPECT_EQ(winter->name, "CET");
    EXPECT_EQ(summer->utc_offset_minutes, 120);
    EXPECT_EQ(summer->name, "CEST");
}

namespace {
struct SystemTimezoneFixture {
    explicit SystemTimezoneFixture(const char* posix_tz) : time_zone(posix_tz), conf(make_test_conf()) {
        conf.use_system_timezone = true;
        impl = std::make_unique<module::main::powermeterImpl>(nullptr, dummy_mod, conf);
        auto fake = std::make_unique<FakeModbusTransport>();
        transport = fake.get();
        module::main::powermeterImpl::TestAccess::set_modbus_transport(*impl, std::move(fake));
    }
    std::vector<std::uint16_t> written_offsets() const {
        std::vector<std::uint16_t> out;
        for (const auto& call : timezone_writes(*transport)) {
            out.push_back(call.data.at(0));
        }
        return out;
    }

    ScopedTimeZone time_zone;
    Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    module::main::Conf conf;
    std::unique_ptr<module::main::powermeterImpl> impl;
    FakeModbusTransport* transport{nullptr};
};
} // namespace

TEST(EM580PowermeterImpl, SystemTimezoneIsWrittenOnlyWhenTheOffsetChanges) {
    SystemTimezoneFixture f("XXX-2");

    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    EXPECT_EQ(f.written_offsets(), std::vector<std::uint16_t>{120});

    ScopedTimeZone::set("XXX-1");
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    EXPECT_EQ(f.written_offsets(), (std::vector<std::uint16_t>{120, 60}));
}

TEST(EM580PowermeterImpl, NegativeHalfHourOffsetIsWrittenAsInt16) {
    SystemTimezoneFixture f("XXX+3:30");

    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    EXPECT_EQ(f.written_offsets(), std::vector<std::uint16_t>{static_cast<std::uint16_t>(std::int16_t{-210})});
}

// A time zone change of the running host (timedatectl set-timezone) must be picked up without a restart.
TEST(EM580PowermeterImpl, HostTimeZoneChangeIsPickedUpWithoutRestart) {
    SystemTimezoneFixture f("XXX-2");
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);

    setenv("TZ", "XXX-1", 1); // without tzset(), as when the host changes its zone
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    EXPECT_EQ(f.written_offsets(), (std::vector<std::uint16_t>{120, 60}));
}

// Every OCMF reading carries the offset it was taken with, so a change is written during a transaction too.
TEST(EM580PowermeterImpl, SystemTimezoneChangeIsWrittenDuringATransaction) {
    SystemTimezoneFixture f("XXX-2");
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    module::main::powermeterImpl::TestAccess::set_transaction_id(*f.impl, "12345678-1234-5678-1234-567812345678");

    ScopedTimeZone::set("XXX-1");
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);
    EXPECT_EQ(f.written_offsets(), (std::vector<std::uint16_t>{120, 60}));
}

// The device may have lost its settings, e.g. after a power cycle, so a configuration writes the offset again.
TEST(EM580PowermeterImpl, ConfigureWritesTheOffsetEvenIfUnchanged) {
    SystemTimezoneFixture f("XXX-2");
    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(*f.impl);

    module::main::powermeterImpl::TestAccess::write_timezone_on_configure(*f.impl);
    EXPECT_EQ(f.written_offsets(), (std::vector<std::uint16_t>{120, 120}));
}

TEST(EM580PowermeterImpl, FixedTimezoneOffsetIsNotUpdated) {
    const ScopedTimeZone time_zone("XXX-2");
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));

    module::main::powermeterImpl::TestAccess::update_timezone_if_changed(impl);
    EXPECT_TRUE(timezone_writes(*transport_ptr).empty());
}
