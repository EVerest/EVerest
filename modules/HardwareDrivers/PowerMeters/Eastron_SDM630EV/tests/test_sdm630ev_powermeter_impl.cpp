// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "helper.hpp"
#include "powermeterImpl.hpp"
#include "registers.hpp"

namespace {

namespace regs = sdm630ev::registers;

struct FetchCall {
    std::int32_t address;
    std::uint16_t register_count;
    transport::RegisterType type;
};

struct WriteCall {
    std::int32_t address;
    std::vector<std::uint16_t> data;
};

class FakeModbusTransport : public transport::AbstractModbusTransport {
public:
    void push_fetch_response(std::int32_t address, std::uint16_t register_count, transport::RegisterType type,
                             transport::DataVector response) {
        scripted_fetch_[Key{address, register_count, static_cast<int>(type)}].push_back(std::move(response));
    }

    const std::vector<FetchCall>& fetch_calls() const {
        return fetch_calls_;
    }

    const std::vector<WriteCall>& write_calls() const {
        return write_calls_;
    }

    transport::DataVector fetch(std::int32_t address, std::uint16_t register_count,
                                transport::RegisterType type) override {
        fetch_calls_.push_back(FetchCall{address, register_count, type});

        const Key key{address, register_count, static_cast<int>(type)};
        auto iter = scripted_fetch_.find(key);
        if (iter == scripted_fetch_.end() || iter->second.empty()) {
            throw std::runtime_error("FakeModbusTransport: no scripted fetch response for address " +
                                     std::to_string(address) + " count " + std::to_string(register_count));
        }
        transport::DataVector out = iter->second.front();
        if (iter->second.size() > 1) {
            iter->second.pop_front();
        }
        return out;
    }

    void write_multiple_registers(std::int32_t address, const std::vector<std::uint16_t>& data) override {
        write_calls_.push_back(WriteCall{address, data});
    }

private:
    using Key = std::tuple<std::int32_t, std::uint16_t, int>;
    // The last scripted response for a key is sticky and repeats.
    std::map<Key, std::deque<transport::DataVector>> scripted_fetch_;
    std::vector<FetchCall> fetch_calls_;
    std::vector<WriteCall> write_calls_;
};

transport::DataVector u16_be(std::uint16_t value) {
    return {static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value & 0xFF)};
}

transport::DataVector bytes(const std::string& str) {
    transport::DataVector out{str.begin(), str.end()};
    if (out.size() % 2 != 0) {
        out.push_back(0);
    }
    return out;
}

module::main::Conf make_test_conf() {
    module::main::Conf conf{};
    conf.powermeter_device_id = 1;
    conf.communication_error_pause_delay_s = 0;
    conf.live_measurement_interval_ms = 1000;
    conf.timezone_offset_minutes = 0;
    conf.ocmf_charge_point_identification_type = "EVSEID";
    conf.ocmf_charge_point_identification = "";
    conf.ocmf_signature_timeout_ms = 2000;
    conf.public_key_format = "der";
    conf.meter_id = "";
    return conf;
}

types::powermeter::TransactionReq make_test_request() {
    types::powermeter::TransactionReq req{};
    req.evse_id = "DE*PNX*E12345*1";
    req.transaction_id = "12345678-1234-5678-1234-567812345678";
    req.identification_status = types::powermeter::OCMFUserIdentificationStatus::ASSIGNED;
    req.identification_flags = {types::powermeter::OCMFIdentificationFlags::RFID_PLAIN};
    req.identification_type = types::powermeter::OCMFIdentificationType::ISO14443;
    req.identification_data.emplace("A1B2C3D4");
    return req;
}

void script_ocmf_document_reads(FakeModbusTransport& transport, const std::string& payload,
                                const transport::DataVector& signature) {
    transport.push_fetch_response(regs::OCMF_JSON_LENGTH_ADDRESS, 1, transport::RegisterType::Holding,
                                  u16_be(static_cast<std::uint16_t>(payload.size())));
    transport.push_fetch_response(regs::OCMF_JSON_DATA_ADDRESS, static_cast<std::uint16_t>((payload.size() + 1) / 2),
                                  transport::RegisterType::Holding, bytes(payload));
    transport.push_fetch_response(regs::SIGNATURE_LENGTH_ADDRESS, 1, transport::RegisterType::Holding,
                                  u16_be(static_cast<std::uint16_t>(signature.size())));
    transport.push_fetch_response(regs::SIGNATURE_DATA_ADDRESS, static_cast<std::uint16_t>((signature.size() + 1) / 2),
                                  transport::RegisterType::Holding, signature);
}

} // namespace

TEST(Sdm630EvPowermeterImpl, StartTransactionWritesDatasetInOrder) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // Idle before start, in progress after the begin command.
    transport->push_fetch_response(regs::CHARGING_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::CHARGING_STATUS_IDLE));
    transport->push_fetch_response(regs::CHARGING_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::CHARGING_STATUS_IN_PROGRESS));
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));

    auto req = make_test_request();
    const auto response = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(response.status, types::powermeter::TransactionRequestStatus::OK);
    EXPECT_FALSE(response.signed_meter_value.has_value());

    // time + IS + IF + IT + ID + CT + CI + begin command
    const auto& writes = transport_ptr->write_calls();
    ASSERT_EQ(writes.size(), 8U);
    EXPECT_EQ(writes[0].address, regs::TIME_BCD_ADDRESS);
    EXPECT_EQ(writes[1].address, regs::OCMF_IS_ADDRESS);
    EXPECT_EQ(writes[1].data, std::vector<std::uint16_t>{0x0000}); // ASSIGNED is 0 on this device
    EXPECT_EQ(writes[2].address, regs::OCMF_IF_ADDRESS);
    EXPECT_EQ(writes[2].data, (std::vector<std::uint16_t>{0x0000, 0x0002})); // RFID_PLAIN = bit 1
    EXPECT_EQ(writes[3].address, regs::OCMF_IT_ADDRESS);
    EXPECT_EQ(writes[3].data, std::vector<std::uint16_t>{0x0003}); // ISO14443
    EXPECT_EQ(writes[4].address, regs::OCMF_ID_ADDRESS);
    EXPECT_EQ(writes[4].data.size(), regs::OCMF_TEXT_FIELD_WORD_COUNT);
    EXPECT_EQ(writes[5].address, regs::OCMF_CT_ADDRESS);
    EXPECT_EQ(writes[5].data, std::vector<std::uint16_t>{0x0001}); // EVSEID
    EXPECT_EQ(writes[6].address, regs::OCMF_CI_ADDRESS);
    EXPECT_EQ(writes[6].data.size(), regs::OCMF_TEXT_FIELD_WORD_COUNT);
    EXPECT_EQ(writes[6].data[0], ('D' << 8) | 'E'); // falls back to evse_id
    EXPECT_EQ(writes[7].address, regs::CHARGE_CONTROL_ADDRESS);
    EXPECT_EQ(writes[7].data, std::vector<std::uint16_t>{regs::CHARGE_CONTROL_BEGIN});
}

TEST(Sdm630EvPowermeterImpl, StartTransactionCleansUpOpenSession) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // In progress at start triggers cleanup, in progress again after begin.
    transport->push_fetch_response(regs::CHARGING_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::CHARGING_STATUS_IN_PROGRESS));
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::SIGNATURE_STATUS_OK));
    script_ocmf_document_reads(*transport, R"(OCMF|{"FV":"1.0"}|{"SD":"AA"})", {0xAA, 0xBB});
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));

    auto req = make_test_request();
    const auto response = module::main::powermeterImpl::TestAccess::start_transaction(impl, req);
    EXPECT_EQ(response.status, types::powermeter::TransactionRequestStatus::OK);

    // Cleanup ends the stale session before the new dataset is written.
    const auto& writes = transport_ptr->write_calls();
    ASSERT_GE(writes.size(), 9U);
    EXPECT_EQ(writes[0].address, regs::CHARGE_CONTROL_ADDRESS);
    EXPECT_EQ(writes[0].data, std::vector<std::uint16_t>{regs::CHARGE_CONTROL_END});
}

TEST(Sdm630EvPowermeterImpl, StopTransactionReturnsAssembledOcmf) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    // Signature walks in-progress -> in-progress -> OK.
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::SIGNATURE_STATUS_IN_PROGRESS));
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::SIGNATURE_STATUS_IN_PROGRESS));
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::SIGNATURE_STATUS_OK));
    // Device output without signature section exercises the append branch.
    script_ocmf_document_reads(*transport, R"(OCMF|{"FV":"1.0"})", {0xDE, 0xAD, 0xBE, 0xEF});
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-1");
    module::main::powermeterImpl::TestAccess::set_public_key_hex(impl, "3059AA");

    std::string transaction_id = "tx-1";
    const auto response = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    ASSERT_EQ(response.status, types::powermeter::TransactionRequestStatus::OK);
    ASSERT_TRUE(response.signed_meter_value.has_value());
    EXPECT_EQ(response.signed_meter_value->encoding_method, "OCMF");
    EXPECT_EQ(response.signed_meter_value->signed_meter_data,
              R"(OCMF|{"FV":"1.0"}|{"SA":"ECDSA-secp256r1-SHA256","SD":"DEADBEEF"})");
    EXPECT_EQ(response.signed_meter_value->public_key.value_or(""), "3059AA");

    ASSERT_EQ(transport_ptr->write_calls().size(), 1U);
    EXPECT_EQ(transport_ptr->write_calls()[0].address, regs::CHARGE_CONTROL_ADDRESS);
    EXPECT_EQ(transport_ptr->write_calls()[0].data, std::vector<std::uint16_t>{regs::CHARGE_CONTROL_END});
}

TEST(Sdm630EvPowermeterImpl, StopTransactionReportsSignatureError) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding, u16_be(0x05));
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-1");

    std::string transaction_id = "tx-1";
    const auto response = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(response.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_NE(response.error.value_or("").find("invalid measurement"), std::string::npos);
}

TEST(Sdm630EvPowermeterImpl, StopTransactionTimesOutOnStuckSignature) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    conf.ocmf_signature_timeout_ms = 0;
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::SIGNATURE_STATUS_IN_PROGRESS));
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-1");

    std::string transaction_id = "tx-1";
    const auto response = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(response.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_NE(response.error.value_or("").find("Timeout"), std::string::npos);
}

TEST(Sdm630EvPowermeterImpl, StopTransactionRejectsUnknownIdWithoutWrites) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-1");

    std::string transaction_id = "tx-other";
    const auto response = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(response.status, types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR);
    EXPECT_TRUE(transport_ptr->write_calls().empty());
}

TEST(Sdm630EvPowermeterImpl, StopTransactionWithEmptyIdCleansUp) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(regs::CHARGING_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::CHARGING_STATUS_IDLE));
    auto* transport_ptr = transport.get();
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-1");

    std::string transaction_id;
    const auto response = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    EXPECT_EQ(response.status, types::powermeter::TransactionRequestStatus::OK);
    EXPECT_FALSE(response.signed_meter_value.has_value());
    // Device already idle: no end command needed.
    EXPECT_TRUE(transport_ptr->write_calls().empty());
}

TEST(Sdm630EvPowermeterImpl, StopTransactionRecoversInterruptedSession) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);

    auto transport = std::make_unique<FakeModbusTransport>();
    transport->push_fetch_response(regs::SIGNATURE_STATUS_ADDRESS, 1, transport::RegisterType::Holding,
                                   u16_be(regs::SIGNATURE_STATUS_OK));
    script_ocmf_document_reads(*transport, R"(OCMF|{"FV":"1.0"}|{"SD":"AA"})", {0xAA});
    module::main::powermeterImpl::TestAccess::set_modbus_transport(impl, std::move(transport));
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-before-powerloss");
    module::main::powermeterImpl::TestAccess::set_pending_closed_transaction(impl, true);

    // After power loss EVerest may ask with a different transaction id; the
    // device cannot verify it, the pending document is returned regardless.
    std::string transaction_id = "tx-after-powerloss";
    const auto response = module::main::powermeterImpl::TestAccess::stop_transaction(impl, transaction_id);
    ASSERT_EQ(response.status, types::powermeter::TransactionRequestStatus::OK);
    ASSERT_TRUE(response.signed_meter_value.has_value());
    EXPECT_EQ(response.signed_meter_value->signed_meter_data, R"(OCMF|{"FV":"1.0"}|{"SD":"AA"})");
    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));
}

TEST(Sdm630EvPowermeterImpl, MonitorChargingStatusFlagsInterruption) {
    static Everest::PtrContainer<module::Eastron_SDM630EV> dummy_mod;
    auto conf = make_test_conf();
    module::main::powermeterImpl impl(nullptr, dummy_mod, conf);
    module::main::powermeterImpl::TestAccess::set_transaction_id(impl, "tx-1");

    module::main::powermeterImpl::TestAccess::monitor_charging_status(impl, regs::CHARGING_STATUS_IN_PROGRESS);
    EXPECT_FALSE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));

    module::main::powermeterImpl::TestAccess::monitor_charging_status(impl, regs::CHARGING_STATUS_POWER_LOSS);
    EXPECT_TRUE(module::main::powermeterImpl::TestAccess::pending_closed_transaction(impl));
}
