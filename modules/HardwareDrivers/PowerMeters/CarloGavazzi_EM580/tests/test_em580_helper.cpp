// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "helper.hpp"

#include <chrono>
#include <deque>
#include <map>
#include <tuple>

namespace {

constexpr std::size_t kFetchCallAlignmentBytes = 8;
constexpr std::size_t kWriteCallAlignmentBytes = 32;

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
    // Script one fetch response for (address, register_count).
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

} // namespace

TEST(EM580Helper, ExtractTransactionIdFromTTHappyPath) {
    const std::string uuid = "12345678-1234-5678-1234-567812345678";
    const std::string ocmf = R"(OCMF|{"TT":"price-2.30-EUR/kWh<=>)" + uuid + R"(","RD":[]}|
{"SA":"ECDSA-brainpoolP384r1-SHA256","SD":"00"})";

    const auto tid = ocmf::extract_transaction_id_from_ocmf_record(ocmf);
    ASSERT_TRUE(tid.has_value());
    EXPECT_EQ(*tid, uuid);
}

TEST(EM580Helper, ExtractTransactionIdFromTTMissingMarker) {
    const std::string ocmf = R"(OCMF|{"TT":"no-marker-here","RD":[]}|
{"SA":"ECDSA-brainpoolP384r1-SHA256","SD":"00"})";

    const auto tid = ocmf::extract_transaction_id_from_ocmf_record(ocmf);
    EXPECT_FALSE(tid.has_value());
}

TEST(EM580Helper, MaxPayloadBytesForWords) {
    EXPECT_EQ(modbus_utils::max_payload_bytes_for_words(0), 0);
    EXPECT_EQ(modbus_utils::max_payload_bytes_for_words(1),
              1); // 2 bytes total, reserve NUL => 1
    EXPECT_EQ(modbus_utils::max_payload_bytes_for_words(126),
              251); // 252 bytes total, reserve NUL => 251
}

TEST(EM580Helper, StringToModbusCharArrayZeroTerminatedAndUsedOnly) {
    const auto words = modbus_utils::string_to_modbus_char_array("AB", 126);
    ASSERT_EQ(words.size(), 2U); // 'A''B''\0' => 3 bytes => 2 words
    EXPECT_EQ(words[0], 0x4142);
    EXPECT_EQ(words[1], 0x0000);
}

TEST(EM580Helper, StringToModbusCharArrayTruncatesToFitWithNul) {
    // 1 word => 2 bytes total => only 1 byte payload + NUL
    const auto words = modbus_utils::string_to_modbus_char_array("HELLO", 1);
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words[0], 0x4800); // 'H' + '\0'
}

TEST(EM580Helper, OcmfConfirmFileReadWritesNotReadyToStateRegister) {
    FakeModbusTransport transport;
    ocmf::confirm_file_read(transport);

    ASSERT_EQ(transport.write_calls().size(), 1U);
    EXPECT_EQ(transport.write_calls()[0].address, em580::registers::MODBUS_OCMF_STATE_ADDRESS);
    ASSERT_EQ(transport.write_calls()[0].data.size(), 1U);
    EXPECT_EQ(transport.write_calls()[0].data[0], em580::registers::MODBUS_OCMF_STATE_NOT_READY);
}

TEST(EM580Helper, OcmfWaitForReadyReachesReady) {
    FakeModbusTransport transport;
    transport.push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                  u16_be(em580::registers::MODBUS_OCMF_STATE_NOT_READY));
    transport.push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                  u16_be(em580::registers::MODBUS_OCMF_STATE_RUNNING));
    transport.push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                  u16_be(em580::registers::MODBUS_OCMF_STATE_READY));

    const bool success = ocmf::wait_for_ready(transport, std::chrono::milliseconds{0}, 10);
    EXPECT_TRUE(success);
    EXPECT_EQ(transport.fetch_calls().size(), 3U);
}

TEST(EM580Helper, OcmfWaitForReadyFailsOnCorrupted) {
    FakeModbusTransport transport;
    transport.push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                  u16_be(em580::registers::MODBUS_OCMF_STATE_CORRUPTED));

    const bool success = ocmf::wait_for_ready(transport, std::chrono::milliseconds{0}, 10);
    EXPECT_FALSE(success);
    EXPECT_EQ(transport.fetch_calls().size(), 1U);
}

TEST(EM580Helper, IsUuid36ValidAndInvalid) {
    EXPECT_TRUE(ocmf::is_uuid36("12345678-1234-5678-1234-567812345678"));
    EXPECT_TRUE(ocmf::is_uuid36("ABCDEFAB-CDEF-ABCD-EFAB-CDEFABCDEFAB"));

    EXPECT_FALSE(ocmf::is_uuid36("12345678-1234-5678-1234-56781234567"));  // too short
    EXPECT_FALSE(ocmf::is_uuid36("123456781234-5678-1234-567812345678"));  // missing '-'
    EXPECT_FALSE(ocmf::is_uuid36("12345678-1234-5678-1234-56781234567Z")); // non-hex
}

TEST(EM580Helper, ExtractTransactionIdFromRecordMissingTTField) {
    const std::string ocmf = R"(OCMF|{"FV":"1.2","RD":[]}|
{"SA":"ECDSA-brainpoolP384r1-SHA256","SD":"00"})";

    const auto tid = ocmf::extract_transaction_id_from_ocmf_record(ocmf);
    EXPECT_FALSE(tid.has_value());
}

TEST(EM580Helper, ExtractTransactionIdFromRecordInvalidUuidAfterMarker) {
    const std::string ocmf = R"(OCMF|{"TT":"foo<=>not-a-uuid","RD":[]}|
{"SA":"ECDSA-brainpoolP384r1-SHA256","SD":"00"})";

    const auto tid = ocmf::extract_transaction_id_from_ocmf_record(ocmf);
    EXPECT_FALSE(tid.has_value());
}

TEST(EM580Helper, DecodeDeviceStateErrorsReturnsMatchingMessages) {
    // Bits 0 and 13 correspond to V1N over-range and Measure module internal
    // fault.
    const auto state = static_cast<std::uint16_t>((1U << 0U) | (1U << 13U));
    const auto errors = device_state_utils::decode_device_state_errors(state);

    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors[0], "V1N over maximum range");
    EXPECT_EQ(errors[1], "Measure module internal fault");
}

TEST(EM580Helper, ModbusToUint16AndToUint32ByteOrder) {
    const transport::DataVector data = {
        0x12, 0x34,            // u16 @0 => 0x1234
        0xAA, 0xBB,            // u16 @2 => 0xAABB
        0xDE, 0xAD, 0xBE, 0xEF // u32 @4 => 0xDEADBEEF
    };

    EXPECT_EQ(modbus_utils::to_uint16(data, modbus_utils::ByteOffset{0}), 0x1234);
    EXPECT_EQ(modbus_utils::to_uint16(data, modbus_utils::ByteOffset{2}), 0xAABB);
    EXPECT_EQ(modbus_utils::to_uint32(data, modbus_utils::ByteOffset{4}), 0xDEADBEEF);
}

TEST(EM580Helper, StringToModbusCharArrayEmptyStringIsJustTerminator) {
    const auto words = modbus_utils::string_to_modbus_char_array("", 126);
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words[0], 0x0000);
}

TEST(EM580Helper, StringToModbusCharArrayPacksOddLengthAndAddsNul) {
    // "ABC\0" => 4 bytes => 2 words: 0x4142 0x4300
    const auto words = modbus_utils::string_to_modbus_char_array("ABC", 126);
    ASSERT_EQ(words.size(), 2U);
    EXPECT_EQ(words[0], 0x4142);
    EXPECT_EQ(words[1], 0x4300);
}

TEST(EM580Helper, StringToModbusCharArrayTruncatesAndStillTerminates) {
    // max_words=2 => 4 bytes total => 3 payload bytes + NUL
    const auto words = modbus_utils::string_to_modbus_char_array("HELLO", 2);
    ASSERT_EQ(words.size(), 2U);
    EXPECT_EQ(words[0], 0x4845); // 'H''E'
    EXPECT_EQ(words[1], 0x4C00); // 'L''\0' (truncated)
}

TEST(EM580Helper, OcmfWaitForReadyTimesOutAfterMaxRetries) {
    FakeModbusTransport transport;
    static constexpr int kNonReadyReads = 11;
    // kNonReadyReads non-ready reads => retries becomes kNonReadyReads and
    // (retries > max_retries(10)) => false
#pragma unroll
    for (int i = 0; i < kNonReadyReads; ++i) {
        transport.push_fetch_response(em580::registers::MODBUS_OCMF_STATE_ADDRESS, 1,
                                      u16_be(em580::registers::MODBUS_OCMF_STATE_NOT_READY));
    }
    const bool success = ocmf::wait_for_ready(transport, std::chrono::milliseconds{0}, 10);
    EXPECT_FALSE(success);
    EXPECT_EQ(transport.fetch_calls().size(), static_cast<std::size_t>(kNonReadyReads));
}

TEST(EM580Helper, DecodeDeviceStateErrorsEmptyIfNoBitsSet) {
    const auto errors = device_state_utils::decode_device_state_errors(0U);
    EXPECT_TRUE(errors.empty());
}

TEST(EM580Helper, ModbusToHexStringUppercaseNoSeparators) {
    const transport::DataVector data = {0x00, 0x2a, 0xAB, 0xCD, 0xEF};
    EXPECT_EQ(modbus_utils::to_hex_string(data, modbus_utils::ByteOffset{0}, modbus_utils::ByteLength{data.size()}),
              "002AABCDEF");
}

TEST(EM580Helper, ModbusToInt16Sign) {
    const transport::DataVector data = {0xFF, 0xFE}; // 0xFFFE => -2
    EXPECT_EQ(modbus_utils::to_int16(data, modbus_utils::ByteOffset{0}), -2);
}

namespace {
using schedule_utils::MeasurementSchedule;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr milliseconds k_interval{1000};
const MeasurementSchedule::Clock::time_point k_t0{seconds{100}};

// Runs one read from start to start + duration and returns what the schedule says next.
MeasurementSchedule::Next run_read(MeasurementSchedule& schedule, MeasurementSchedule::Clock::time_point start,
                                   milliseconds duration) {
    schedule.read_started(start);
    return schedule.read_finished(start + duration);
}
} // namespace

TEST(EM580MeasurementSchedule, ReadsStartAtFixedRate) {
    MeasurementSchedule schedule{k_interval};
    schedule.restart(k_t0);
    auto start = k_t0;
    for (int i = 1; i <= 5; ++i) {
        const auto next = run_read(schedule, start, milliseconds{300 + 100 * i});
        EXPECT_EQ(next.wake_at, k_t0 + i * k_interval);
        EXPECT_FALSE(next.warning.has_value());
        start = next.wake_at;
    }
}

TEST(EM580MeasurementSchedule, OccasionalSlowReadIsMadeUpWithoutWarning) {
    MeasurementSchedule schedule{k_interval};
    schedule.restart(k_t0);
    auto start = k_t0;
    // one read in ten also reads the device state and takes longer than the interval
    for (int i = 1; i <= 100; ++i) {
        const auto duration = (i % 10 == 0) ? milliseconds{1080} : milliseconds{900};
        const auto next = run_read(schedule, start, duration);
        EXPECT_FALSE(next.warning.has_value()) << "read " << i;
        EXPECT_EQ(next.wake_at, k_t0 + i * k_interval) << "read " << i;
        start = std::max(next.wake_at, start + duration);
    }
}

TEST(EM580MeasurementSchedule, FallingAWholeIntervalBehindWarnsOnceAndResyncs) {
    MeasurementSchedule schedule{k_interval};
    schedule.restart(k_t0);
    const auto next = run_read(schedule, k_t0, milliseconds{2500});
    ASSERT_TRUE(next.warning.has_value());
    EXPECT_EQ(next.warning->skipped_reads, 1U);
    EXPECT_EQ(next.warning->slowest_read, milliseconds{2500});
    EXPECT_EQ(next.wake_at, k_t0 + milliseconds{2500});

    const auto after = run_read(schedule, next.wake_at, milliseconds{500});
    EXPECT_FALSE(after.warning.has_value());
    EXPECT_EQ(after.wake_at, next.wake_at + k_interval);
}

TEST(EM580MeasurementSchedule, SustainedOverrunWarnsAtMostOncePerPeriod) {
    MeasurementSchedule schedule{k_interval, seconds{60}};
    schedule.restart(k_t0);
    auto start = k_t0;
    int warnings = 0;
    std::size_t reported_skips = 0;
    // every read takes 1.2 s: one read in six is lost
    for (int i = 0; i < 300; ++i) {
        const auto duration = milliseconds{1200};
        const auto next = run_read(schedule, start, duration);
        if (next.warning.has_value()) {
            ++warnings;
            reported_skips += next.warning->skipped_reads;
            EXPECT_EQ(next.warning->slowest_read, duration);
        }
        start = std::max(next.wake_at, start + duration);
    }
    // 360 s of reads: one warning right away, then at most one per 60 s
    EXPECT_GE(warnings, 6);
    EXPECT_LE(warnings, 7);
    EXPECT_GT(reported_skips, 0U);
}

TEST(EM580MeasurementSchedule, RestartForgetsTheBacklog) {
    MeasurementSchedule schedule{k_interval};
    schedule.restart(k_t0);
    run_read(schedule, k_t0, milliseconds{500});
    const auto later = k_t0 + seconds{30}; // e.g. after a communication error pause
    schedule.restart(later);
    const auto next = run_read(schedule, later, milliseconds{500});
    EXPECT_FALSE(next.warning.has_value());
    EXPECT_EQ(next.wake_at, later + k_interval);
}
