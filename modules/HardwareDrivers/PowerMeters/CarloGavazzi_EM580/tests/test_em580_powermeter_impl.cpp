// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "helper.hpp"
#include "powermeterImpl.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
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
        std::lock_guard<std::mutex> lock(mutex_);
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
        WriteCall call{address, data};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            write_calls_.push_back(call);
        }
        if (on_write_) {
            on_write_(call);
        }
    }

    void on_write(std::function<void(const WriteCall&)> callback) {
        on_write_ = std::move(callback);
    }

private:
    using Key = std::tuple<std::int32_t, std::uint16_t>;
    std::map<Key, std::deque<transport::DataVector>> scripted_fetch_;
    std::vector<FetchCall> fetch_calls_;
    std::vector<WriteCall> write_calls_;
    std::function<void(const WriteCall&)> on_write_;
    std::mutex mutex_;
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
    conf.startup_transaction_state_timeout_s = 30;
    return conf;
}

types::powermeter::TransactionReq make_transaction_req() {
    types::powermeter::TransactionReq req{};
    req.evse_id = "DE*TEST*EVSE01";
    req.transaction_id = "12345678-1234-5678-1234-567812345678";
    req.identification_status = types::powermeter::OCMFUserIdentificationStatus::ASSIGNED;
    req.identification_flags = {};
    req.identification_type = types::powermeter::OCMFIdentificationType::ISO14443;
    req.identification_level = types::powermeter::OCMFIdentificationLevel::NONE;
    req.identification_data.emplace("ABC");
    req.tariff_text.emplace("TARIFF");
    return req;
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
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
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
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
    module::main::powermeterImpl::TestAccess::set_pending_closed_transaction(impl, true);

    std::string requested_uuid = "12345678-1234-5678-1234-567812345678";
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, requested_uuid);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(*resp.error, "Transaction id mismatch");
    EXPECT_EQ(transport_ptr->write_calls().size(), 0U);
}

TEST(EM580PowermeterImpl, StopTransactionEmptyIdWithIdleDeviceWritesNothing) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_NOT_READY));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "12345678-1234-5678-1234-567812345678");
    module::main::powermeterImpl::TestAccess::set_pending_closed_transaction(impl, false);

    std::string empty_id;
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, empty_id);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);
    EXPECT_TRUE(transport_ptr->write_calls().empty());
}

// At start up EvseManager may clean up before the device is configured, so the local transaction flags are not
// restored yet. A transaction left RUNNING on the device must be ended anyway.
TEST(EM580PowermeterImpl, StopTransactionEmptyIdEndsTransactionLeftRunningOnDevice) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_READY));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_SIZE_ADDRESS, 1, u16_be(1));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_FILE_ADDRESS, 1, bytes("OCMF|{}|{}"));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);

    std::string empty_id;
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, empty_id);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);

    // 'E' end command, then the confirmation of the OCMF file.
    ASSERT_EQ(transport_ptr->write_calls().size(), 2U);
    EXPECT_EQ(transport_ptr->write_calls()[0].address, em580::registers::MODBUS_OCMF_COMMAND_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls()[0].data,
              std::vector<std::uint16_t>{em580::registers::MODBUS_OCMF_COMMAND_END});
    EXPECT_EQ(transport_ptr->write_calls()[1].address, em580::registers::MODBUS_OCMF_STATE_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls()[1].data,
              std::vector<std::uint16_t>{em580::registers::MODBUS_OCMF_STATE_NOT_READY});
}

TEST(EM580PowermeterImpl, StartTransactionRunningStateEndsOpenTransactionAndStartsNewTransaction) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // handle_start_transaction() checks the state, clear_transaction_states() reads it again.
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    // wait_for_ready() after the 'E' command.
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_READY));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_SIZE_ADDRESS, 1, u16_be(1));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_FILE_ADDRESS, 1, bytes("OCMF|{}|{}"));
    transport->push_fetch_response(em580::registers::MODBUS_SIGNED_MAP_ADDRESS,
                                   em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256,
                                   zero_bytes_for_words(em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
    module::main::powermeterImpl::TestAccess::set_signed_map_word_count(
        impl, em580::registers::MODBUS_SIGNED_MAP_WORD_COUNT_256);

    auto req = make_transaction_req();
    const auto resp = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);

    // 'E' end command + cleanup confirm write + 10 transaction start writes.
    ASSERT_EQ(transport_ptr->write_calls().size(), 12U);
    EXPECT_EQ(transport_ptr->write_calls()[0].address, em580::registers::MODBUS_OCMF_COMMAND_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls()[0].data,
              std::vector<std::uint16_t>{em580::registers::MODBUS_OCMF_COMMAND_END});
    EXPECT_EQ(transport_ptr->write_calls()[1].address, em580::registers::MODBUS_OCMF_STATE_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls().back().address, em580::registers::MODBUS_OCMF_COMMAND_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls().back().data,
              std::vector<std::uint16_t>{em580::registers::MODBUS_OCMF_COMMAND_START});
}

TEST(EM580PowermeterImpl, StartTransactionFailsWhenOpenTransactionDoesNotEnd) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    // wait_for_ready() polls 11 times before it gives up.
    constexpr int kStateReads = 2 + 11;
    auto transport = std::make_unique<FakeModbusTransport>();
    for (int i = 0; i < kStateReads; ++i) {
        transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                       u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    }

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);

    auto req = make_transaction_req();
    const auto resp = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);

    // Only the 'E' end command, no transaction registers and no 'B'.
    ASSERT_EQ(transport_ptr->write_calls().size(), 1U);
    EXPECT_EQ(transport_ptr->write_calls()[0].address, em580::registers::MODBUS_OCMF_COMMAND_ADDRESS);
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
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
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
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
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

// EvseManager stops the transaction of the previous session at start up, possibly before the device is configured.
// The request must wait for the transaction state recovered from the device, or the OCMF file would be lost.
TEST(EM580PowermeterImpl, StopTransactionWaitsUntilTheDeviceIsConfigured) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    const std::string ocmf_file = "OCMF|{\"TT\":\"x<=>12345678-1234-5678-1234-567812345678\"}|{}";
    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_READY));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_SIZE_ADDRESS, 1, u16_be(1));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_FILE_ADDRESS, 1, bytes(ocmf_file));

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));

    // Stands in for configure_device() finding the RUNNING transaction and its id in the tariff text.
    std::thread configure([&impl] {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "12345678-1234-5678-1234-567812345678");
        module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
    });

    std::string transaction_id = "12345678-1234-5678-1234-567812345678";
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    configure.join();

    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);
    ASSERT_TRUE(resp.signed_meter_value.has_value());
    EXPECT_EQ(resp.signed_meter_value->signed_meter_data, ocmf_file);
    ASSERT_EQ(transport_ptr->write_calls().size(), 2U);
    EXPECT_EQ(transport_ptr->write_calls()[0].address, em580::registers::MODBUS_OCMF_COMMAND_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls()[0].data,
              std::vector<std::uint16_t>{em580::registers::MODBUS_OCMF_COMMAND_END});
}

TEST(EM580PowermeterImpl, StartupWaitTimesOutOnceOnly) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_startup_wait_timeout(impl, std::chrono::milliseconds{20});

    std::string transaction_id = "12345678-1234-5678-1234-567812345678";
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_TRUE(transport_ptr->write_calls().empty());

    // The device is still not configured, but a later request, e.g. during a communication outage, does not wait.
    module::main::powermeterImpl::TestAccess::set_startup_wait_timeout(impl, std::chrono::seconds{10});
    const auto start = std::chrono::steady_clock::now();
    const auto second = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(second.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds{1});
}

TEST(EM580PowermeterImpl, ShutdownReleasesARequestWaitingForTheDevice) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::make_unique<FakeModbusTransport>());
    module::main::powermeterImpl::TestAccess::set_startup_wait_timeout(impl, std::chrono::seconds{10});

    std::thread stopper([&impl] {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        module::main::powermeterImpl::TestAccess::shutdown(impl);
    });
    const auto start = std::chrono::steady_clock::now();
    std::string transaction_id = "12345678-1234-5678-1234-567812345678";
    module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    stopper.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds{5});
}

// While the transaction left RUNNING is ended, the live thread may read the READY state that follows. It must not be
// reported as a transaction closed unexpectedly, which EvseManager treats as a fatal error.
TEST(EM580PowermeterImpl, TransactionLeftRunningStaysActiveWhenItDoesNotEnd) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    // wait_for_ready() polls 11 times before it gives up.
    constexpr int kStateReads = 2 + 11;
    auto transport = std::make_unique<FakeModbusTransport>();
    for (int i = 0; i < kStateReads; ++i) {
        transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                       u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    }

    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "12345678-1234-5678-1234-567812345678");

    auto req = make_transaction_req();
    const auto resp = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_TRUE(module::main::powermeterImpl::TestAccess::transaction_active(impl));
}

TEST(EM580PowermeterImpl, StartupWaitDisabledWithZeroTimeout) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    conf.startup_transaction_state_timeout_s = 0;
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::make_unique<FakeModbusTransport>());

    const auto start = std::chrono::steady_clock::now();
    std::string transaction_id = "12345678-1234-5678-1234-567812345678";
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds{1});
}

// Recovering the transaction state on configuration must not interleave with ending a transaction: it would read the
// READY between 'E' and the confirmation and restore a closed transaction that the cleanup has already discarded.
TEST(EM580PowermeterImpl, TransactionStateRecoveryWaitsForAStopInProgress) {
    static Everest::PtrContainer<module::CarloGavazzi_EM580> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // Cleanup: RUNNING, then READY after 'E'. Recovery afterwards: NOT_READY.
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_READY));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                   u16_be(em580::registers::MODBUS_OCMF_STATE_NOT_READY));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_SIZE_ADDRESS, 1, u16_be(1));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_STATE_FILE_ADDRESS, 1, bytes("OCMF|{}|{}"));
    transport->push_fetch_response(em580::registers::MODBUS_OCMF_TARIFF_TEXT_ADDRESS,
                                   em580::registers::MODBUS_OCMF_TARIFF_TEXT_WORD_COUNT,
                                   zero_bytes_for_words(em580::registers::MODBUS_OCMF_TARIFF_TEXT_WORD_COUNT));
    std::thread recovery;
    transport->on_write([&impl, &recovery](const WriteCall& call) {
        if (call.address == em580::registers::MODBUS_OCMF_COMMAND_ADDRESS && !recovery.joinable()) {
            recovery =
                std::thread([&impl] { module::main::powermeterImpl::TestAccess::read_transaction_state_and_id(impl); });
            // Gives the recovery the chance to run in between, which the serialization must prevent.
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    });

    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::end_startup_wait(impl);

    std::string empty_id;
    const auto resp = module::main::powermeterImpl::TestAccess::stop_transaction(impl, empty_id);
    ASSERT_TRUE(recovery.joinable());
    recovery.join();

    EXPECT_EQ(resp.status, types::powermeter::TransactionRequestStatus::OK);
    // 'E', then the confirmation of the OCMF file once READY was read by the cleanup, not by the recovery.
    ASSERT_EQ(transport_ptr->write_calls().size(), 2U);
    EXPECT_EQ(transport_ptr->write_calls()[1].address, em580::registers::MODBUS_OCMF_STATE_ADDRESS);
    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));
    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::transaction_active(impl));
}
