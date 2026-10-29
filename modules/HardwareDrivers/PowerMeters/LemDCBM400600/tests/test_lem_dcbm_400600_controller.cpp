// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Unit tests for the LemDCBM400600Controller

#include "http_client_interface.hpp"
#include "lem_dcbm_400600_controller.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace module::main {

class HTTPClientMock : public HttpClientInterface {
public:
    MOCK_METHOD(HttpResponse, get, (const std::string& path), (override, const));
    MOCK_METHOD(HttpResponse, post, (const std::string& path, const std::string& body), (override, const));
    MOCK_METHOD(HttpResponse, put, (const std::string& path, const std::string& body), (override, const));
};

class LemDCBMTimeSyncHelperMock : public LemDCBMTimeSyncHelper {
public:
    MOCK_METHOD(void, sync_if_deadline_expired, (const HttpClientInterface& httpClient), (override));
    MOCK_METHOD(void, sync, (const HttpClientInterface& httpClient), (override));
    MOCK_METHOD(void, restart_unsafe_period, (), (override));
    LemDCBMTimeSyncHelperMock() : LemDCBMTimeSyncHelper({}, {}){};
};

// The fields of Conf that the tests actually vary. Everything else is fixed by make_controller_conf()
// below, so that adding a field to Conf does not silently shift the meaning of every test's config.
struct ControllerConfOverrides {
    int init_number_of_http_retries = 0;
    int init_retry_wait_in_milliseconds = 0;
    // IT = -1 so that init() does not call set_identification_type()
    int IT = -1;
    int transaction_ocmf_fetch_interval_s = 0;
    bool ast_transaction_body = false;
    int tariff_id = 0;
};

static LemDCBM400600Controller::Conf make_controller_conf(const ControllerConfOverrides& overrides = {}) {
    return LemDCBM400600Controller::Conf{overrides.init_number_of_http_retries,
                                         overrides.init_retry_wait_in_milliseconds,
                                         /*transaction_number_of_http_retries=*/1,
                                         /*transaction_retry_wait_in_milliseconds=*/0,
                                         /*cable_id=*/0,
                                         overrides.tariff_id,
                                         /*meter_timezone=*/{},
                                         /*meter_dst=*/{},
                                         /*SC=*/0,
                                         /*UV=*/{},
                                         /*UD=*/{},
                                         overrides.IT,
                                         /*command_timeout_ms=*/0,
                                         overrides.transaction_ocmf_fetch_interval_s,
                                         overrides.ast_transaction_body};
}

// Performs one iteration of the live measurement poll loop, in the same order as powermeterImpl does:
// fetch and publish the live measurements first, refresh the fallback OCMF record afterwards.
static void poll_once(LemDCBM400600Controller& controller) {
    const auto powermeter = controller.get_powermeter();
    controller.update_transaction_fallback_ocmf(powermeter.timestamp);
}

// Fixture class providing
//   - a http client mock
//   - default responses & request objects
class LemDCBM400600ControllerTest : public ::testing::Test {

protected:
    std::unique_ptr<HTTPClientMock> http_client;
    std::unique_ptr<LemDCBMTimeSyncHelperMock> time_sync_helper;

    const std::string livemeasure_response{R"({
                                                "voltage": 4.2,
                                                "current": 4,
                                                "power": 3,
                                                "temperatureH": 0,
                                                "temperatureL": 0,
                                                "energyImportTotal": 1,
                                                "energyExportTotal": 2,
                                                "timestamp": "2023-09-10T21:10:08.068773"
                                                })"};

    const types::powermeter::TransactionReq transaction_request{
        "mock_evse_id",
        "mock_transaction_id",
        types::powermeter::OCMFUserIdentificationStatus::ASSIGNED,
        {},
        types::powermeter::OCMFIdentificationType::ISO14443,
        std::nullopt,
        std::nullopt,
        std::nullopt};

    const std::string expected_start_transaction_request_body{
        R"({"evseId":"mock_evse_id","transactionId":"mock_transaction_id","clientId":",mock_transaction_id","tariffId":0,"cableId":0,"userData":""})"};

    const std::string put_legal_response = R"({
                                                "paginationCounter": 6,
                                                "transactionId": "mock_transaction_id",
                                                "evseId": "+49*DEF*E123ABC",
                                                "clientId": "C12",
                                                "tariffId": 2,
                                                "cableSp": {
                                                  "cableSpName": "2mR_Comp",
                                                  "cableSpId": 1,
                                                  "cableSpRes": 2
                                                },
                                                "userData": "",
                                                "meterValue": {
                                                  "timestampStart": "2020-12-10T16:39:15+01:00",
                                                  "timestampStop": "2020-12-10T16:39:15+01:00",
                                                  "transactionDuration": 70,
                                                  "intermediateRead": false,
                                                  "transactionStatus": 17,
                                                  "sampleValue": {
                                                    "energyUnit": "kWh",
                                                    "energyImport": 7.637,
                                                    "energyImportTotalStart": 188.977,
                                                    "energyImportTotalStop": 196.614,
                                                    "energyExport": 0.000,
                                                    "energyExportTotalStart": 0.000,
                                                    "energyExportTotalStop": 0.000
                                                  }},
                                                "meterId": "12024072805",
                                                "signature": "304502203DC38FBC722D216568D6ECB4B352577A999B6D184EA6AD48BDCAE7766DB1D628022100A7687B4CB5573829D407DD4B17D41C297917B7E8307E5017711B5A3A987F6801",
                                                "publicKey": "A80F10D968E1122F8820F288B23C4E1C0DA912F35B48481274ADFEFE66D7E87E130C7CF2B8047C45CF105041C8C3A57DD242782F755C9443F42DABA9404A67BF"
                                                })";

    const std::string status_transaction_ongoing{R"({"status":{"bits":{"transactionIsOnGoing":true}}})"};
    const std::string status_no_transaction{R"({"status":{"bits":{"transactionIsOnGoing":false}}})"};

    const LemDCBM400600Controller::Conf controller_config{make_controller_conf()};

    // A transaction start first asks the device whether a transaction is still running on it
    void expect_no_transaction_on_device(testing::Sequence& seq) {
        EXPECT_CALL(*this->http_client, get("/v1/status"))
            .Times(1)
            .InSequence(seq)
            .WillOnce(testing::Return(HttpResponse{200, this->status_no_transaction}));
    }

    void SetUp() override {
        this->http_client = std::make_unique<HTTPClientMock>();
        this->time_sync_helper = std::make_unique<LemDCBMTimeSyncHelperMock>();
    }
};

// Extended fixture for parametrizing tests for invalid response checks
class LemDCBM400600ControllerTestInvalidResponses
    : public LemDCBM400600ControllerTest,
      public ::testing::WithParamInterface<testing::internal::ReturnAction<HttpResponse>> {};

//****************************************************************
// Test get_powermeter behavior

/// \brief Test get_powermeter returns correct live measure
TEST_F(LemDCBM400600ControllerTest, test_get_powermeter) {

    // Setup
    testing::Sequence seq;
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    const types::powermeter::Powermeter& powermeter = controller.get_powermeter();

    // Verify
    EXPECT_EQ(powermeter.timestamp, "2023-09-10T21:10:08.068773");
    EXPECT_THAT(powermeter.energy_Wh_import.total, testing::FloatEq(1000.0));
    EXPECT_THAT(powermeter.energy_Wh_export->total, testing::FloatEq(2000.0));
    EXPECT_THAT(powermeter.power_W->total, testing::FloatEq(3000.0));
    EXPECT_THAT(powermeter.current_A->DC.value(), testing::FloatEq(4.0));
    EXPECT_THAT(powermeter.voltage_V->DC.value(), testing::FloatEq(4.2));
    EXPECT_THAT(powermeter.meter_id.value(), ""); // not initialized
}

/// \brief Test get_powermeter fails due to an invalid response status code
TEST_F(LemDCBM400600ControllerTest, test_get_powermeter_fail_invalid_response_code) {

    // Setup
    testing::Sequence seq;
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{403, this->livemeasure_response}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act & Verify
    EXPECT_THROW(controller.get_powermeter(), LemDCBM400600Controller::DCBMUnexpectedResponseException);
}

/// \brief Test get_powermeter fails due to an invalid response status body
TEST_F(LemDCBM400600ControllerTest, test_get_powermeter_fail_invalid_response_body) {

    // Setup
    testing::Sequence seq;
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "invalid"}));

    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);
    // Act & Verify
    EXPECT_THROW(controller.get_powermeter(), LemDCBM400600Controller::DCBMUnexpectedResponseException);
}

/// \brief Test get_powermeter fails due to an http client error
TEST_F(LemDCBM400600ControllerTest, test_get_powermeter_fail_http_error) {

    // Setup
    testing::Sequence seq;
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Throw(HttpClientError("http client mock error")));

    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);
    // Act & Verify
    EXPECT_THROW(controller.get_powermeter(), HttpClientError);
}

//****************************************************************
// Test start_transaction behavior

// \brief Test a successful start transaction
TEST_F(LemDCBM400600ControllerTest, test_start_transaction) {
    // Setup
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    auto res = controller.start_transaction(this->transaction_request);

    // Verify
    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
    EXPECT_FALSE(res.error.has_value());
    EXPECT_THAT(res.transaction_min_stop_time.value(),
                testing::MatchesRegex("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3}Z$"));
    EXPECT_THAT(res.transaction_max_stop_time.value(),
                testing::MatchesRegex("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3}Z$"));

    auto delta = Everest::Date::from_rfc3339(res.transaction_max_stop_time.value()) -
                 Everest::Date::from_rfc3339(res.transaction_min_stop_time.value());
    EXPECT_EQ(
        int(delta.count() / 1E9 / 60),
        48 * 60 -
            3); // delta of max and min stopping time should be 48 hours - 2 minutes wait time and 1 minute safety time
}

/// \brief Test the AST DC650 start body: tariffId as a string and no cableId
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_ast_body) {
    const std::string expected_ast_body{
        R"({"evseId":"mock_evse_id","transactionId":"mock_transaction_id","clientId":",mock_transaction_id","tariffId":"0","userData":""})"};
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", expected_ast_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
    EXPECT_FALSE(res.error.has_value());
}

/// \brief Test the AST DC650 stop: the response has no transactionStatus, the stop is confirmed via /v1/status
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_ast_confirms_via_status) {
    const std::string ast_stop_response{
        R"({"transactionId":"mock_transaction_id","meterValue":{"timestampStart":"2026-10-08T11:42:30Z","timestampStop":"2026-10-08T11:42:34Z"}})"};
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(0);
    testing::Sequence seq;
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, ast_stop_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"status":{"bits":{"transactionIsOnGoing":false}}})"}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.stop_transaction("mock_transaction_id");

    ASSERT_EQ(transaction_request_status_to_string(res.status), "OK");
    ASSERT_TRUE(res.signed_meter_value.has_value());
    ASSERT_EQ(res.signed_meter_value.value().signed_meter_data, "mock_ocmf_string");
}

/// \brief Test the AST DC650 start body carries the configured tariff as a string
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_ast_body_tariff_id_as_string) {
    const std::string expected_ast_body{
        R"({"evseId":"mock_evse_id","transactionId":"mock_transaction_id","clientId":",mock_transaction_id","tariffId":"7","userData":""})"};
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1);
    EXPECT_CALL(*this->http_client, post("/v1/legal", expected_ast_body))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    overrides.tariff_id = 7;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
}

/// \brief Test the AST DC650 stop fails if the device status keeps reporting the transaction as ongoing; a retry
/// checks the status again but does not send the stop a second time
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_ast_fails_when_still_ongoing) {
    const std::string ast_stop_response{R"({"transactionId":"mock_transaction_id","meterValue":{}})"};
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, ast_stop_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(2)
        .WillRepeatedly(testing::Return(HttpResponse{200, R"({"status":{"bits":{"transactionIsOnGoing":true}}})"}));
    EXPECT_CALL(*this->http_client, get(testing::StartsWith("/v1/ocmf"))).Times(0);
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::HasSubstr("still ongoing"));
    EXPECT_FALSE(res.signed_meter_value.has_value());
}

/// \brief Test the AST DC650 stop succeeds when the device status confirms it on the retry
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_ast_confirmed_on_retry) {
    const std::string ast_stop_response{R"({"transactionId":"mock_transaction_id","meterValue":{}})"};
    testing::Sequence seq;
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, ast_stop_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(2)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"status":{"bits":{"transactionIsOnGoing":true}}})"}))
        .WillOnce(testing::Return(HttpResponse{200, R"({"status":{"bits":{"transactionIsOnGoing":false}}})"}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.stop_transaction("mock_transaction_id");

    ASSERT_EQ(transaction_request_status_to_string(res.status), "OK");
    ASSERT_TRUE(res.signed_meter_value.has_value());
    EXPECT_EQ(res.signed_meter_value.value().signed_meter_data, "mock_ocmf_string");
}

/// \brief Test the AST DC650 stop fails if the device status cannot be read
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_ast_fails_when_status_fails) {
    const std::string ast_stop_response{R"({"transactionId":"mock_transaction_id","meterValue":{}})"};
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, ast_stop_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status")).Times(2).WillRepeatedly(testing::Return(HttpResponse{500, ""}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::HasSubstr("/v1/status"));
}

/// \brief Test a malformed device status is reported against /v1/status, with the status body
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_ast_malformed_status_names_status_endpoint) {
    const std::string ast_stop_response{R"({"transactionId":"mock_transaction_id","meterValue":{}})"};
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, ast_stop_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(2)
        .WillRepeatedly(testing::Return(HttpResponse{200, R"({"status":{"value":16}})"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::HasSubstr("/v1/status"));
    EXPECT_THAT(res.error.value(), testing::HasSubstr(R"({"status":{"value":16}})"));
}

/// \brief Test a start with a dangling AST transaction stops it, confirms the stop via /v1/status, then starts
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_ast_recovers_dangling_transaction) {
    const std::string expected_ast_body{
        R"({"evseId":"mock_evse_id","transactionId":"mock_transaction_id","clientId":",mock_transaction_id","tariffId":"0","userData":""})"};
    testing::Sequence seq;
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(2);
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", expected_ast_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    // the first transaction was never stopped and still runs on the device
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->status_transaction_ongoing}));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "mock_transaction_id"})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId":"mock_transaction_id","meterValue":{}})"}));
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->status_no_transaction}));
    EXPECT_CALL(*this->http_client, post("/v1/legal", expected_ast_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    ASSERT_EQ(transaction_request_status_to_string(controller.start_transaction(this->transaction_request).status),
              "OK");
    // the first transaction was never stopped: the next start closes it first
    EXPECT_EQ(transaction_request_status_to_string(controller.start_transaction(this->transaction_request).status),
              "OK");
}

/// \brief Test a start fails if the dangling AST transaction is still ongoing after the stop request
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_ast_fails_when_dangling_transaction_stays) {
    const std::string expected_ast_body{
        R"({"evseId":"mock_evse_id","transactionId":"mock_transaction_id","clientId":",mock_transaction_id","tariffId":"0","userData":""})"};
    // only the first start reaches the device: the second one stops at the transaction still running
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1);
    EXPECT_CALL(*this->http_client, post("/v1/legal", expected_ast_body))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    // the stop is sent once, the retry only checks the device status again
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId":"mock_transaction_id","meterValue":{}})"}));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "mock_transaction_id"})"}));
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    // the check before the second start, then the stop confirmation and its retry
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(3)
        .InSequence(seq)
        .WillRepeatedly(testing::Return(HttpResponse{200, this->status_transaction_ongoing}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    ASSERT_EQ(transaction_request_status_to_string(controller.start_transaction(this->transaction_request).status),
              "OK");
    const auto res = controller.start_transaction(this->transaction_request);
    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::HasSubstr("could not be stopped"));
    EXPECT_THAT(res.error.value(), testing::HasSubstr("still ongoing"));
}

/// \brief Test the AST DC650 stop uses transactionStatus when the response does carry it
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_ast_uses_transaction_status_when_present) {
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status")).Times(0);
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));

    auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
}

/// \brief Test the default (LEM) format still rejects a stop response without transactionStatus
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_lem_rejects_response_without_transaction_status) {
    const std::string ast_stop_response{R"({"transactionId":"mock_transaction_id","meterValue":{}})"};
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .WillRepeatedly(testing::Return(HttpResponse{200, ast_stop_response}));
    EXPECT_CALL(*this->http_client, get("/v1/status")).Times(0);
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
}

/// \brief Test a v2-capable device keeps the v2 body even with the AST format configured
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_v2_ignores_ast_format) {
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(2) // init, then the check for a transaction still running before the start
        .WillRepeatedly(testing::Return(HttpResponse{
            200,
            R"({ "meterId": "mock_meter_id", "publicKeyOcmf": "KEY", "status": {"bits": {"transactionIsOnGoing": false}}, "version":{"applicationFirmwareVersion":"1.2.0.0"} })"}));
    EXPECT_CALL(*this->http_client, get("/v2/legal"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "thetransactionid"})"}));
    EXPECT_CALL(*this->time_sync_helper, restart_unsafe_period()).Times(testing::AnyNumber());
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1);
    EXPECT_CALL(*this->http_client, post("/v2/legal", testing::AllOf(testing::HasSubstr(R"("tariffId":0)"),
                                                                     testing::HasSubstr(R"("cableId":0)"))))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));
    controller.init();

    auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
}

/// \brief Test a v2-capable device keeps the strict stop check with the AST format configured
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_v2_ignores_ast_format) {
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{
            200,
            R"({ "meterId": "mock_meter_id", "publicKeyOcmf": "KEY", "status": {"bits": {"transactionIsOnGoing": false}}, "version":{"applicationFirmwareVersion":"1.2.0.0"} })"}));
    EXPECT_CALL(*this->http_client, get("/v2/legal"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "thetransactionid"})"}));
    EXPECT_CALL(*this->time_sync_helper, restart_unsafe_period()).Times(testing::AnyNumber());
    EXPECT_CALL(*this->http_client, put("/v2/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .WillRepeatedly(
            testing::Return(HttpResponse{200, R"({"transactionId":"mock_transaction_id","meterValue":{}})"}));
    ControllerConfOverrides overrides;
    overrides.ast_transaction_body = true;
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       make_controller_conf(overrides));
    controller.init();

    auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
}

/// \brief Test fallback OCMF is fetched immediately after start and then throttled
TEST_F(LemDCBM400600ControllerTest, test_fallback_ocmf_fetch_is_immediate_then_throttled) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));

    const LemDCBM400600Controller::Conf throttled_controller_config{
        make_controller_conf({.transaction_ocmf_fetch_interval_s = std::numeric_limits<int>::max()})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       throttled_controller_config);

    EXPECT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    poll_once(controller);
    poll_once(controller);
}

/// \brief Test get_powermeter does not fetch the fallback OCMF record itself
///
/// The fallback record must be fetched only via update_transaction_fallback_ocmf(), so that the caller
/// can publish the live measurements before paying for that round trip.
TEST_F(LemDCBM400600ControllerTest, test_get_powermeter_does_not_fetch_fallback_ocmf) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    // Only reached once update_transaction_fallback_ocmf() is called explicitly, never by get_powermeter().
    bool ocmf_fetched = false;
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::DoAll(testing::InvokeWithoutArgs([&ocmf_fetched] { ocmf_fetched = true; }),
                                 testing::Return(HttpResponse{200, "mock_ocmf_string"})));

    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    EXPECT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);

    const auto powermeter = controller.get_powermeter();
    EXPECT_FALSE(ocmf_fetched) << "get_powermeter() must not fetch the fallback OCMF record";

    controller.update_transaction_fallback_ocmf(powermeter.timestamp);
    EXPECT_TRUE(ocmf_fetched);
}

/// \brief Test a failing fallback OCMF fetch does not bypass the throttle
///
/// A slow or erroring OCMF endpoint must not be retried on every single poll: that is precisely when the
/// device can least afford the extra request, and each attempt delays the publication of the live
/// measurements by up to the command timeout.
TEST_F(LemDCBM400600ControllerTest, test_failing_fallback_ocmf_fetch_is_still_throttled) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    // The single OCMF attempt fails - it must not be repeated on the following polls.
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Throw(HttpClientError("mock connection failure")));
    // Two further polls, each fetching only the live measurements. Any further OCMF request would be
    // reported by gmock as an unexpected call, since its expectation above is already saturated.
    for (int poll = 0; poll < 2; poll++) {
        EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
        EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
            .Times(1)
            .InSequence(seq)
            .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    }

    const LemDCBM400600Controller::Conf throttled_controller_config{
        make_controller_conf({.transaction_ocmf_fetch_interval_s = std::numeric_limits<int>::max()})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       throttled_controller_config);

    EXPECT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    poll_once(controller);
    poll_once(controller);
    poll_once(controller);
}

// \brief Test a failed start transaction with the DCBM returning an invalid response
TEST_P(LemDCBM400600ControllerTestInvalidResponses, test_start_transaction_fail_invalid_response) {

    // Setup
    // request fails due to an invalid response
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillRepeatedly(GetParam());
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillRepeatedly(GetParam());
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    auto res = controller.start_transaction(this->transaction_request);

    // Verify
    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    EXPECT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::MatchesRegex("Failed to start transaction mock_transaction_id.*"));
    EXPECT_FALSE(res.transaction_min_stop_time.has_value());
    EXPECT_FALSE(res.transaction_max_stop_time.has_value());
}

// Setup parametrized invalid responses
static const std::string TEST_NAMES_START_TRANSACTION_INVALID_RESPONSES[2] = {"InvalidReturnCode",
                                                                              "InvalidResponseBody"};
INSTANTIATE_TEST_SUITE_P(
    LemDCBM400600ControllerTestStartTransactionInvalidResponses, LemDCBM400600ControllerTestInvalidResponses,
    testing::Values(testing::Return(HttpResponse{403, ""}), testing::Return(HttpResponse{201, "invalid"})),
    [](const testing::TestParamInfo<LemDCBM400600ControllerTestInvalidResponses::ParamType>& info) {
        return TEST_NAMES_START_TRANSACTION_INVALID_RESPONSES[info.index];
    });

// \brief Test a failed start transaction with the http request failing
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_http_fail) {
    // Setup
    // request fails and throws an HttpClientError
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    auto res = controller.start_transaction(this->transaction_request);

    // Verify
    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    EXPECT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::MatchesRegex("Failed to start transaction mock_transaction_id.*"));
    EXPECT_FALSE(res.transaction_min_stop_time.has_value());
    EXPECT_FALSE(res.transaction_max_stop_time.has_value());
}

//****************************************************************
// Test stop_transaction behavior

// \brief Test to stop a transaction and receive OCMF report.
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction) {

    // Setup
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(0);

    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));

    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    auto res = controller.stop_transaction("mock_transaction_id");

    // Verify
    ASSERT_EQ(transaction_request_status_to_string(res.status), "OK");
    ASSERT_TRUE(res.signed_meter_value.has_value());
    ASSERT_EQ(res.signed_meter_value.value().signed_meter_data, "mock_ocmf_string");
}

// \brief Test a failed stop transaction with the DCBM returning an invalid response
TEST_P(LemDCBM400600ControllerTestInvalidResponses, test_stop_transaction_fail_invalid_response) {
    // Setup
    // request fails repeatedly
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(0);

    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .WillRepeatedly(GetParam());
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    auto res = controller.stop_transaction("mock_transaction_id");

    // Verify
    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    EXPECT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::MatchesRegex("Failed to stop transaction mock_transaction_id:.*"));
    EXPECT_FALSE(res.signed_meter_value.has_value());
}

// Setup parametrized invalid responses
static const std::string TEST_NAMES_STOP_TRANSACTION_INVALID_RESPONSES[2] = {"InvalidReturnCode",
                                                                             "InvalidResponseBody"};
INSTANTIATE_TEST_SUITE_P(
    LemDCBM400600ControllerTestStopTransactionInvalidResponses, LemDCBM400600ControllerTestInvalidResponses,
    testing::Values(testing::Return(HttpResponse{403, ""}), testing::Return(HttpResponse{200, "invalid"})),
    [](const testing::TestParamInfo<LemDCBM400600ControllerTestInvalidResponses::ParamType>& info) {
        return TEST_NAMES_STOP_TRANSACTION_INVALID_RESPONSES[info.index];
    });

// \brief Test a failed stop transaction with the http request failing
TEST_F(LemDCBM400600ControllerTest, test_stop_transaction_http_fail) {
    // Setup
    // request fails repeatedly
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(0);
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Act
    auto res = controller.stop_transaction("mock_transaction_id");

    // Verify
    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    EXPECT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::MatchesRegex("Failed to stop transaction mock_transaction_id.*"));
    EXPECT_FALSE(res.signed_meter_value.has_value());
}

//****************************************************************
// Test a transaction left running on the device

/// \brief Test a start stops a transaction the device still runs, e.g. after its stop failed, then starts
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_stops_transaction_left_running_on_device) {
    testing::Sequence seq;
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->status_transaction_ongoing}));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "dangling_transaction_id"})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=dangling_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    const auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
}

/// \brief Test a start that cannot stop the transaction left running reports it instead of trying to start
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_fails_if_transaction_left_running_cannot_be_stopped) {
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, this->status_transaction_ongoing}));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "dangling_transaction_id"})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=dangling_transaction_id", R"({"running": false})"))
        .Times(2)
        .WillRepeatedly(testing::Return(HttpResponse{403, ""}));
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(0);
    EXPECT_CALL(*this->http_client, post(testing::_, testing::_)).Times(0);
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    const auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(),
                testing::HasSubstr("dangling_transaction_id is still running on the device and could not be stopped"));
}

/// \brief Test a stop that fails while the device is unreachable keeps the transaction: a later cleanup stop
/// without transaction id still stops it on the device
TEST_F(LemDCBM400600ControllerTest, test_failed_stop_keeps_the_transaction) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    ASSERT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    ASSERT_EQ(transaction_request_status_to_string(controller.stop_transaction("mock_transaction_id").status),
              "UNEXPECTED_ERROR");
    const auto res = controller.stop_transaction("");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
    ASSERT_TRUE(res.signed_meter_value.has_value());
    EXPECT_EQ(res.signed_meter_value.value().signed_meter_data, "mock_ocmf_string");
    // the transaction is forgotten once its stop is confirmed
    EXPECT_EQ(controller.stop_transaction("").error, "No dangling transaction open");
}

/// \brief Test a stop rejected by the device keeps the transaction as well
TEST_F(LemDCBM400600ControllerTest, test_rejected_stop_keeps_the_transaction) {
    // transactionStatus with the third bit "transactionIsOnGoing" set
    const std::string still_ongoing_response{
        R"({"transactionId":"mock_transaction_id","meterValue":{"transactionStatus":4}})"};
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .InSequence(seq)
        .WillRepeatedly(testing::Return(HttpResponse{200, still_ongoing_response}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    ASSERT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    ASSERT_EQ(transaction_request_status_to_string(controller.stop_transaction("mock_transaction_id").status),
              "UNEXPECTED_ERROR");

    EXPECT_EQ(transaction_request_status_to_string(controller.stop_transaction("").status), "OK");
}

/// \brief Test a confirmed stop forgets the transaction: a cleanup stop then has nothing to close
TEST_F(LemDCBM400600ControllerTest, test_confirmed_stop_forgets_the_transaction) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    ASSERT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    ASSERT_EQ(transaction_request_status_to_string(controller.stop_transaction("mock_transaction_id").status), "OK");
    const auto res = controller.stop_transaction("");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_EQ(res.error.value(), "No dangling transaction open");
}

/// \brief Test the device decides at start: a transaction kept after a failed stop is not stopped again if the
/// device no longer runs it
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_follows_the_device_after_a_failed_stop) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    ASSERT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    ASSERT_EQ(transaction_request_status_to_string(controller.stop_transaction("mock_transaction_id").status),
              "UNEXPECTED_ERROR");

    EXPECT_EQ(transaction_request_status_to_string(controller.start_transaction(this->transaction_request).status),
              "OK");
}

/// \brief Test a start fails without being sent if the device status cannot be read
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_fails_if_device_status_cannot_be_read) {
    EXPECT_CALL(*this->http_client, get("/v1/status")).Times(2).WillRepeatedly(testing::Return(HttpResponse{500, ""}));
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(0);
    EXPECT_CALL(*this->http_client, post(testing::_, testing::_)).Times(0);
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    const auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(), testing::HasSubstr("/v1/status"));
}

/// \brief Test a connection error while stopping the transaction left running names that transaction
TEST_F(LemDCBM400600ControllerTest, test_start_transaction_names_transaction_left_running_on_connection_error) {
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, this->status_transaction_ongoing}));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "dangling_transaction_id"})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=dangling_transaction_id", R"({"running": false})"))
        .Times(2)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    EXPECT_CALL(*this->http_client, post(testing::_, testing::_)).Times(0);
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    const auto res = controller.start_transaction(this->transaction_request);

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    ASSERT_TRUE(res.error.has_value());
    EXPECT_THAT(res.error.value(),
                testing::HasSubstr("dangling_transaction_id is still running on the device and could not be stopped"));
}

/// \brief Test a retry after a failed OCMF fetch does not send the stop to the device again
TEST_F(LemDCBM400600ControllerTest, test_stop_is_sent_once_if_only_the_ocmf_fetch_fails) {
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(2)
        .WillOnce(testing::Return(HttpResponse{500, ""}))
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    const auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
    ASSERT_TRUE(res.signed_meter_value.has_value());
    EXPECT_EQ(res.signed_meter_value.value().signed_meter_data, "mock_ocmf_string");
}

/// \brief Test a stop that fails while the device is unreachable returns the OCMF record last fetched for it
TEST_F(LemDCBM400600ControllerTest, test_failed_stop_returns_the_last_ocmf_of_the_transaction) {
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "intermediate_ocmf_string"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(2)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    ASSERT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    poll_once(controller);
    const auto res = controller.stop_transaction("mock_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "OK");
    ASSERT_TRUE(res.signed_meter_value.has_value());
    EXPECT_EQ(res.signed_meter_value.value().signed_meter_data, "intermediate_ocmf_string");
}

/// \brief Test a failed stop never returns the OCMF record of a previous transaction
TEST_F(LemDCBM400600ControllerTest, test_failed_stop_does_not_return_the_ocmf_of_another_transaction) {
    auto second_request = this->transaction_request;
    second_request.transaction_id = "second_transaction_id";
    testing::Sequence seq;
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->livemeasure_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "first_ocmf_string"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "first_ocmf_string"}));
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", testing::HasSubstr("second_transaction_id")))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=second_transaction_id", R"({"running": false})"))
        .Times(2)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    ASSERT_EQ(controller.start_transaction(this->transaction_request).status,
              types::powermeter::TransactionRequestStatus::OK);
    poll_once(controller);
    ASSERT_EQ(transaction_request_status_to_string(controller.stop_transaction("mock_transaction_id").status), "OK");
    ASSERT_EQ(controller.start_transaction(second_request).status, types::powermeter::TransactionRequestStatus::OK);
    const auto res = controller.stop_transaction("second_transaction_id");

    EXPECT_EQ(transaction_request_status_to_string(res.status), "UNEXPECTED_ERROR");
    EXPECT_FALSE(res.signed_meter_value.has_value());
}

/// \brief Test a status update does not overwrite a transaction started while it was reading the device
TEST_F(LemDCBM400600ControllerTest, test_status_update_keeps_a_transaction_started_meanwhile) {
    LemDCBM400600Controller* controller_ptr = nullptr;
    testing::Sequence seq;
    // the status update reads no transaction on the device, then a start completes before it applies that
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Invoke([this, &controller_ptr](const std::string&) {
            EXPECT_EQ(controller_ptr->start_transaction(this->transaction_request).status,
                      types::powermeter::TransactionRequestStatus::OK);
            return HttpResponse{200, this->status_no_transaction};
        }));
    this->expect_no_transaction_on_device(seq);
    EXPECT_CALL(*this->time_sync_helper, sync(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, post("/v1/legal", this->expected_start_transaction_request_body))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{201, R"({"running": true})"}));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "previous_transaction_id"})"}));
    // the started transaction is still known: a cleanup stop stops it on the device
    EXPECT_CALL(*this->http_client, put("/v1/legal?transactionId=mock_transaction_id", R"({"running": false})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, this->put_legal_response}));
    EXPECT_CALL(*this->http_client, get("/v1/ocmf?transactionId=mock_transaction_id"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "mock_ocmf_string"}));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);
    controller_ptr = &controller;

    controller.update_lem_status();

    EXPECT_EQ(transaction_request_status_to_string(controller.stop_transaction("").status), "OK");
}

//****************************************************************
// Test init behavior

// \brief Test the init method fetches the meter_id
TEST_F(LemDCBM400600ControllerTest, test_init_meter_id) {
    // Setup
    testing::Sequence seq;
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{
            200,
            this->livemeasure_response,
        }));
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{
            200,
            R"({ "meterId": "mock_meter_id",  "publicKeyOcmf": "KEY",   "status": {"bits": {"transactionIsOnGoing": false}},   "version":{"applicationFirmwareVersion":"0.1.2.3"},   "some_other_field": "other_value" })",
        }));
    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "thetransactionid"})"}));
    EXPECT_CALL(*this->time_sync_helper, restart_unsafe_period()).Times(1).InSequence(seq);
    EXPECT_CALL(*this->time_sync_helper, sync_if_deadline_expired(testing::_)).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{
            200,
            this->livemeasure_response,
        }));
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config);

    // Assert: no meter id set before init call
    EXPECT_EQ(controller.get_powermeter().meter_id, "");

    // Act: initialize
    struct ntp_server_spec ntp_spec;
    controller.init();

    // verify by calling the powermeter interface that should provide the mocked metric id
    EXPECT_EQ(controller.get_powermeter().meter_id, "mock_meter_id");
}

// \brief Test the init method retries to fetch the meter id in case of a HttpClientError
TEST_F(LemDCBM400600ControllerTest, test_init_meter_id_retry_success) {

    // Setup
    int number_of_retries = 3;
    testing::Sequence seq;
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(number_of_retries - 1)
        .InSequence(seq)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));

    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{
            200,
            R"({ "meterId": "mock_meter_id",  "publicKeyOcmf": "KEY",   "status": {"bits": {"transactionIsOnGoing": false}},   "version":{"applicationFirmwareVersion":"0.1.2.3"},   "some_other_field": "other_value" })",
        }));

    EXPECT_CALL(*this->http_client, get("/v1/legal"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "thetransactionid"})"}));

    EXPECT_CALL(*this->time_sync_helper, restart_unsafe_period()).Times(1).InSequence(seq);
    EXPECT_CALL(*this->http_client, get("/v1/livemeasure"))
        .Times(1)
        .InSequence(seq)
        .WillRepeatedly(testing::Return(HttpResponse{
            200,
            this->livemeasure_response,
        }));
    const LemDCBM400600Controller::Conf controller_config{
        make_controller_conf({.init_number_of_http_retries = number_of_retries, .init_retry_wait_in_milliseconds = 1})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       controller_config);

    // Act
    struct ntp_server_spec ntp_spec;
    controller.init();

    // Verify
    EXPECT_EQ(controller.get_powermeter().meter_id, "mock_meter_id");
}

// \brief Test at init the HttpClientError is re-raised after the provided number of attempts all failed
TEST_F(LemDCBM400600ControllerTest, test_init_meter_id_retry_fail_eventually) {
    // Setup
    int number_of_retries = 3;
    EXPECT_CALL(*this->http_client, get("/v1/status"))
        .Times(1 + number_of_retries)
        .WillRepeatedly(testing::Throw(HttpClientError{"mock error"}));
    EXPECT_CALL(*this->time_sync_helper, restart_unsafe_period()).Times(0);

    const LemDCBM400600Controller::Conf controller_config{
        make_controller_conf({.init_number_of_http_retries = number_of_retries, .init_retry_wait_in_milliseconds = 1})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       controller_config);

    // Act & Verify
    struct ntp_server_spec ntp_spec;
    EXPECT_THROW(controller.init(), HttpClientError);
}

//****************************************************************
// Test set_identification_type behavior (IT field)

// Helper to set up a mock that successfully completes init() (fetch_meter_id + restart + set_command_timeout)
#define SETUP_SUCCESSFUL_INIT(seq)                                                                                                                                                \
    EXPECT_CALL(*this->http_client, get("/v1/status"))                                                                                                                            \
        .Times(1)                                                                                                                                                                 \
        .InSequence(seq)                                                                                                                                                          \
        .WillOnce(testing::Return(HttpResponse{                                                                                                                                   \
            200,                                                                                                                                                                  \
            R"({ "meterId": "mock_meter_id", "publicKeyOcmf": "KEY", "status": {"bits": {"transactionIsOnGoing": false}}, "version":{"applicationFirmwareVersion":"0.1.2.3"} })", \
        }));                                                                                                                                                                      \
    EXPECT_CALL(*this->http_client, get("/v1/legal"))                                                                                                                             \
        .Times(1)                                                                                                                                                                 \
        .InSequence(seq)                                                                                                                                                          \
        .WillOnce(testing::Return(HttpResponse{200, R"({"transactionId": "thetransactionid"})"}));                                                                                \
    EXPECT_CALL(*this->time_sync_helper, restart_unsafe_period()).Times(1).InSequence(seq)

/// \brief Test init() with IT = -1 does not call set_identification_type
TEST_F(LemDCBM400600ControllerTest, test_init_skips_set_it_when_minus_one) {
    // Setup
    testing::Sequence seq;
    SETUP_SUCCESSFUL_INIT(seq);

    // No PUT /v1/settings expected
    EXPECT_CALL(*this->http_client, put("/v1/settings", testing::_)).Times(0);

    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper),
                                       this->controller_config); // IT = -1

    // Act
    controller.init();
}

/// \brief Test init() with IT >= 0 calls set_identification_type successfully
TEST_F(LemDCBM400600ControllerTest, test_init_sets_it_when_valid) {
    // Setup
    testing::Sequence seq;

    EXPECT_CALL(*this->http_client, get("/v1/settings"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"ocmfId":{"IT":3}})"}));

    EXPECT_CALL(*this->http_client, put("/v1/settings", R"({"ocmfId":{"IT":5}})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"result": 1})"}));

    SETUP_SUCCESSFUL_INIT(seq);

    const LemDCBM400600Controller::Conf config_with_it{make_controller_conf({.IT = 5})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper), config_with_it);

    // Act - should not throw
    controller.init();
}

/// \brief Test set_identification_type throws on non-200 response code
TEST_F(LemDCBM400600ControllerTest, test_init_set_it_fails_on_bad_status_code) {
    // Setup - IT is attempted first; fetch_meter_id is never reached when IT fails
    testing::Sequence seq;

    EXPECT_CALL(*this->http_client, get("/v1/settings"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"ocmfId":{"IT":0}})"}));

    EXPECT_CALL(*this->http_client, put("/v1/settings", R"({"ocmfId":{"IT":3}})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{500, "Internal Server Error"}));

    const LemDCBM400600Controller::Conf config_with_it{make_controller_conf({.IT = 3})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper), config_with_it);

    // Act & Verify
    EXPECT_THROW(controller.init(), LemDCBM400600Controller::DCBMUnexpectedResponseException);
}

/// \brief Test set_identification_type throws when device rejects the setting (result != 1)
TEST_F(LemDCBM400600ControllerTest, test_init_set_it_fails_on_rejected) {
    // Setup - IT is attempted first; fetch_meter_id is never reached when IT fails
    testing::Sequence seq;

    EXPECT_CALL(*this->http_client, get("/v1/settings"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"ocmfId":{"IT":0}})"}));

    EXPECT_CALL(*this->http_client, put("/v1/settings", R"({"ocmfId":{"IT":3}})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"result": 0})"}));

    const LemDCBM400600Controller::Conf config_with_it{make_controller_conf({.IT = 3})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper), config_with_it);

    // Act & Verify
    EXPECT_THROW(controller.init(), LemDCBM400600Controller::DCBMUnexpectedResponseException);
}

/// \brief Test set_identification_type throws on malformed JSON response body
TEST_F(LemDCBM400600ControllerTest, test_init_set_it_fails_on_malformed_json) {
    // Setup - IT is attempted first; fetch_meter_id is never reached when IT fails
    testing::Sequence seq;

    EXPECT_CALL(*this->http_client, get("/v1/settings"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"ocmfId":{"IT":0}})"}));

    EXPECT_CALL(*this->http_client, put("/v1/settings", R"({"ocmfId":{"IT":3}})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, "not json"}));

    const LemDCBM400600Controller::Conf config_with_it{make_controller_conf({.IT = 3})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper), config_with_it);

    // Act & Verify
    EXPECT_THROW(controller.init(), LemDCBM400600Controller::DCBMUnexpectedResponseException);
}

/// \brief Test set_identification_type throws when JSON body is missing "result" key
TEST_F(LemDCBM400600ControllerTest, test_init_set_it_fails_on_missing_result_key) {
    // Setup - IT is attempted first; fetch_meter_id is never reached when IT fails
    testing::Sequence seq;

    EXPECT_CALL(*this->http_client, get("/v1/settings"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"ocmfId":{"IT":0}})"}));

    EXPECT_CALL(*this->http_client, put("/v1/settings", R"({"ocmfId":{"IT":3}})"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"status": "ok"})"}));

    const LemDCBM400600Controller::Conf config_with_it{make_controller_conf({.IT = 3})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper), config_with_it);

    // Act & Verify
    EXPECT_THROW(controller.init(), LemDCBM400600Controller::DCBMUnexpectedResponseException);
}

/// \brief Test init() with IT >= 0 skips set_identification_type when device already has the configured value
TEST_F(LemDCBM400600ControllerTest, test_init_skips_set_it_when_already_configured) {
    // Setup
    testing::Sequence seq;

    EXPECT_CALL(*this->http_client, get("/v1/settings"))
        .Times(1)
        .InSequence(seq)
        .WillOnce(testing::Return(HttpResponse{200, R"({"ocmfId":{"IT":5}})"}));

    EXPECT_CALL(*this->http_client, put("/v1/settings", testing::_)).Times(0);

    SETUP_SUCCESSFUL_INIT(seq);

    const LemDCBM400600Controller::Conf config_with_it{make_controller_conf({.IT = 5})};
    LemDCBM400600Controller controller(std::move(this->http_client), std::move(this->time_sync_helper), config_with_it);

    // Act - should not throw
    controller.init();
}

} // namespace module::main
