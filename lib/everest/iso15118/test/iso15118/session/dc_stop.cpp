// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <sys/syscall.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/iso.hpp>

#include "mock_connection.hpp"

namespace {
auto now = iso15118::TimePoint{} + std::chrono::seconds(1);
}

// Interpose the monotonic clock only in this executable; certificate wall time stays real.
extern "C" int clock_gettime(clockid_t clock, timespec* value) noexcept {
    if (clock != CLOCK_MONOTONIC) {
        return static_cast<int>(syscall(SYS_clock_gettime, clock, value));
    }
    const auto elapsed = now.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    value->tv_sec = seconds.count();
    value->tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds).count();
    return 0;
}

namespace {

using namespace std::chrono_literals;
namespace dt = iso15118::message_20::datatypes;
using iso15118::test::MockConnection;
using Signal = iso15118::session::feedback::Signal;

iso15118::session::SessionConfig make_dc_config() {
    iso15118::session::EvseSetupConfig setup{};
    setup.evse_id = "DE*EVR*E00000*1";
    setup.supported_energy_services = {dt::ServiceCategory::DC};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    setup.dc_limits.charge_limits.power.max = {50, 3};
    setup.dc_limits.charge_limits.current.max = {200, 0};
    setup.dc_limits.voltage.max = {500, 0};
    setup.powersupply_limits = setup.dc_limits;
    return iso15118::session::SessionConfig{setup};
}

template <typename Request>
void queue_request(MockConnection& conn, const Request& req,
                   iso15118::io::v2gtp::PayloadType payload = iso15118::io::v2gtp::PayloadType::Part20Main) {
    std::array<uint8_t, iso15118::io::MAX_V2G_PACKET_SIZE> buffer{};
    const auto size = serialize(req, iso15118::io::StreamOutputView{buffer.data(), buffer.size()});
    conn.written.clear();
    conn.queue_v2gtp_packet(payload, buffer.data(), size);
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
}

template <typename Response> Response response_from_wire(const MockConnection& conn) {
    const auto header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
    REQUIRE(conn.written.size() > header_size);
    uint16_t wire_type{};
    std::memcpy(&wire_type, conn.written.data() + 2, sizeof(wire_type));
    const auto payload = static_cast<iso15118::io::v2gtp::PayloadType>(ntohs(wire_type));
    const iso15118::message_20::Variant response{
        payload, iso15118::io::StreamInputView{conn.written.data() + header_size, conn.written.size() - header_size}};
    return response.get<Response>();
}

// Drives a complete DC scheduled-mode session to PowerDelivery and, unless
// requested otherwise, through the first DC_ChargeLoopRequest.  The transport
// is a MockConnection and time is advanced explicitly.
struct DcSession {
    std::vector<Signal> signals;
    std::vector<iso15118::session::feedback::SessionStopAction> stop_actions;
    iso15118::session::feedback::Callbacks callbacks;
    std::optional<iso15118::d20::PauseContext> pause_ctx;
    std::optional<iso15118::d2::PauseContext> d2_pause_ctx;
    std::unique_ptr<MockConnection> connection = std::make_unique<MockConnection>();
    MockConnection* conn = connection.get();
    std::unique_ptr<iso15118::Session> session;
    dt::SessionId id{};

    explicit DcSession(bool send_first_charge_loop = true, bool throttle_same_type_only = false) {
        callbacks.signal = [this](auto signal) { signals.push_back(signal); };
        callbacks.session_stop_res_sent = [this](auto action) { stop_actions.push_back(action); };
        session = std::make_unique<iso15118::Session>(std::move(connection), make_dc_config(), callbacks, pause_ctx,
                                                      d2_pause_ctx, /*skip_app_protocol_negotiation=*/true,
                                                      throttle_same_type_only);
        conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);

        iso15118::message_20::SessionSetupRequest setup{};
        setup.evccid = "EVCC";
        round(setup);
        id = response_from_wire<iso15118::message_20::SessionSetupResponse>(*conn).header.session_id;

        iso15118::message_20::AuthorizationSetupRequest auth_setup{};
        auth_setup.header = {id, 0};
        round(auth_setup);
        REQUIRE(response_from_wire<iso15118::message_20::AuthorizationSetupResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        session->push_control_event(iso15118::d20::AuthorizationResponse{true});
        iso15118::message_20::AuthorizationRequest auth{};
        auth.header = {id, 0};
        auth.selected_authorization_service = dt::Authorization::EIM;
        auth.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();
        round(auth);
        REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).evse_processing ==
                dt::Processing::Finished);

        iso15118::message_20::ServiceDiscoveryRequest discovery{};
        discovery.header = {id, 0};
        round(discovery);
        REQUIRE(response_from_wire<iso15118::message_20::ServiceDiscoveryResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        iso15118::message_20::ServiceDetailRequest detail{};
        detail.header = {id, 0};
        detail.service = iso15118::message_20::to_underlying_value(dt::ServiceCategory::DC);
        round(detail);
        auto detail_res = response_from_wire<iso15118::message_20::ServiceDetailResponse>(*conn);
        REQUIRE(detail_res.response_code == dt::ResponseCode::OK);
        REQUIRE_FALSE(detail_res.service_parameter_list.empty());

        iso15118::message_20::ServiceSelectionRequest select{};
        select.header = {id, 0};
        select.selected_energy_transfer_service = {dt::ServiceCategory::DC,
                                                   detail_res.service_parameter_list.front().id};
        round(select);
        REQUIRE(response_from_wire<iso15118::message_20::ServiceSelectionResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        iso15118::message_20::DC_ChargeParameterDiscoveryRequest cpd{};
        cpd.header = {id, 0};
        auto& cpd_mode = cpd.transfer_mode.emplace<dt::DC_CPDReqEnergyTransferMode>();
        cpd_mode.max_charge_power = {50, 3};
        cpd_mode.min_charge_power = {0, 0};
        cpd_mode.max_charge_current = {200, 0};
        cpd_mode.min_charge_current = {0, 0};
        cpd_mode.max_voltage = {500, 0};
        cpd_mode.min_voltage = {0, 0};
        round(cpd, iso15118::io::v2gtp::PayloadType::Part20DC);
        REQUIRE(response_from_wire<iso15118::message_20::DC_ChargeParameterDiscoveryResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        iso15118::message_20::ScheduleExchangeRequest schedule{};
        schedule.header = {id, 0};
        schedule.max_supporting_points = 12;
        schedule.control_mode.emplace<dt::Scheduled_SEReqControlMode>();
        round(schedule);
        REQUIRE(response_from_wire<iso15118::message_20::ScheduleExchangeResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        session->push_control_event(iso15118::d20::CableCheckFinished{true});
        iso15118::message_20::DC_CableCheckRequest cable{};
        cable.header = {id, 0};
        round(cable, iso15118::io::v2gtp::PayloadType::Part20DC);
        REQUIRE(response_from_wire<iso15118::message_20::DC_CableCheckResponse>(*conn).processing ==
                dt::Processing::Finished);

        iso15118::message_20::DC_PreChargeRequest precharge{};
        precharge.header = {id, 0};
        precharge.processing = dt::Processing::Finished;
        precharge.target_voltage = {400, 0};
        round(precharge, iso15118::io::v2gtp::PayloadType::Part20DC);
        REQUIRE(response_from_wire<iso15118::message_20::DC_PreChargeResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        iso15118::message_20::PowerDeliveryRequest start{};
        start.header = {id, 0};
        start.processing = dt::Processing::Finished;
        start.charge_progress = dt::Progress::Start;
        round(start);
        REQUIRE(response_from_wire<iso15118::message_20::PowerDeliveryResponse>(*conn).response_code ==
                dt::ResponseCode::OK);

        if (send_first_charge_loop) {
            send_charge_loop();
        }
    }

    template <typename Request>
    void round(const Request& req,
               iso15118::io::v2gtp::PayloadType payload = iso15118::io::v2gtp::PayloadType::Part20Main) {
        queue_request(*conn, req, payload);
        now += 101ms;
        session->poll();
    }

    void poll() {
        now += 1ms;
        session->poll();
    }

    void advance(std::chrono::milliseconds delta) {
        now += delta;
    }

    void queue_charge_loop() {
        iso15118::message_20::DC_ChargeLoopRequest req{};
        req.header = {id, 0};
        auto& mode = req.control_mode.emplace<dt::Scheduled_DC_CLReqControlMode>();
        mode.target_current = {20, 0};
        mode.target_voltage = {400, 0};
        req.present_voltage = {400, 0};
        queue_request(*conn, req, iso15118::io::v2gtp::PayloadType::Part20DC);
    }

    void send_charge_loop() {
        queue_charge_loop();
        now += 101ms;
        session->poll();
    }

    void send_stop() {
        iso15118::message_20::PowerDeliveryRequest req{};
        req.header = {id, 0};
        req.processing = dt::Processing::Finished;
        req.charge_progress = dt::Progress::Stop;
        round(req);
    }

    int count(Signal signal) const {
        return static_cast<int>(std::count(signals.begin(), signals.end(), signal));
    }
};

} // namespace

// StopCharging is latched in DC_ChargeLoop, but the first-entry signal does
// not consult it.  The first charge-loop request must not re-authorize the EV.
SCENARIO("A stopped DC charge loop does not authorize its first request") {
    GIVEN("a DC session that has completed PowerDelivery(Start)") {
        DcSession session{/*send_first_charge_loop=*/false};
        REQUIRE(session.count(Signal::CHARGE_LOOP_STARTED) == 0);

        WHEN("the EVSE requests stop charging before the first charge-loop request") {
            session.session->push_control_event(iso15118::d20::StopCharging{true});
            session.poll();

            THEN("the request is answered Terminate without publishing CHARGE_LOOP_STARTED") {
                session.send_charge_loop();

                const auto response = response_from_wire<iso15118::message_20::DC_ChargeLoopResponse>(*session.conn);
                REQUIRE(response.status.has_value());
                CHECK(response.status->notification == dt::EvseNotification::Terminate);
                CHECK(session.count(Signal::CHARGE_LOOP_STARTED) == 0);
            }
        }
    }
}

SCENARIO("A valid first DC charge-loop request authorizes once") {
    GIVEN("a DC session with an accepted first charge-loop request") {
        DcSession session;
        REQUIRE(session.count(Signal::CHARGE_LOOP_STARTED) == 1);

        WHEN("another valid request arrives") {
            session.send_charge_loop();

            THEN("the start signal is not repeated") {
                const auto response = response_from_wire<iso15118::message_20::DC_ChargeLoopResponse>(*session.conn);
                CHECK(response.response_code == dt::ResponseCode::OK);
                CHECK(session.count(Signal::CHARGE_LOOP_STARTED) == 1);
            }
        }
    }
}

SCENARIO("A rejected first DC charge-loop request does not authorize") {
    GIVEN("a DC session without a first charge-loop request") {
        DcSession session{/*send_first_charge_loop=*/false};
        REQUIRE(session.count(Signal::CHARGE_LOOP_STARTED) == 0);

        WHEN("the first request uses a mismatched control mode") {
            iso15118::message_20::DC_ChargeLoopRequest bad{};
            bad.header = {session.id, 0};
            auto& bad_mode = bad.control_mode.emplace<dt::BPT_Scheduled_DC_CLReqControlMode>();
            bad_mode.target_current = {20, 0};
            bad_mode.target_voltage = {400, 0};
            bad.present_voltage = {400, 0};
            session.round(bad, iso15118::io::v2gtp::PayloadType::Part20DC);

            THEN("the request fails without publishing CHARGE_LOOP_STARTED") {
                const auto response = response_from_wire<iso15118::message_20::DC_ChargeLoopResponse>(*session.conn);
                CHECK(response.response_code == dt::ResponseCode::FAILED);
                CHECK(session.count(Signal::CHARGE_LOOP_STARTED) == 0);
            }
        }
    }
}
