// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// ISO 15118-2 PowerDelivery(Stop) through the engine: the response waits for DcOutputDisabled
// [IEC 61851-23:2023 Table CC.9 t103 before t104], and an emergency shutdown reported while it is held
// answers it FAILED at once [V2G2-539] instead of letting the emergency guard close the connection first.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/session/d2_secc_engine.hpp>

using namespace iso15118;

namespace dt = message_2::datatypes;
namespace m20dt = message_20::datatypes;
using Signal = session::feedback::Signal;

namespace {

session::SessionConfig make_session_config() {
    d20::DcTransferLimits limits{};
    limits.charge_limits.current.max = m20dt::from_float(400.0f);
    limits.charge_limits.current.min = m20dt::from_float(0.0f);
    limits.charge_limits.power.max = m20dt::from_float(150000.0f);
    limits.charge_limits.power.min = m20dt::from_float(0.0f);
    limits.voltage.max = m20dt::from_float(920.0f);
    limits.voltage.min = m20dt::from_float(0.0f);

    session::EvseSetupConfig setup{};
    setup.evse_id = "DE*PNX*E12345*1";
    setup.evse_id_din = "49A80737A45678";
    setup.supported_energy_services = {m20dt::ServiceCategory::DC};
    setup.pre20_energy_transfer_modes = {shared_datatypes::EnergyTransferMode::DC_extended};
    setup.authorization_services = {m20dt::Authorization::EIM};
    setup.dc_limits = limits;
    setup.powersupply_limits = limits;
    return session::SessionConfig(setup);
}

class IsoEngine {
public:
    IsoEngine() :
        engine(io::StreamOutputView{out_buffer.data(), out_buffer.size()}, make_session_config(), pause_ctx,
               make_callbacks(), timeouts, false) {
    }

    // Responses are drained after every request, so has_response() only sees what the last step sent.
    template <typename Request> void drive(Request request) {
        request.header.session_id = session_id;
        send(request);
    }

    void control(const d20::ControlEvent& event) {
        engine.on_control_event(event);
    }

    void timeout(d20::TimeoutType type) {
        engine.on_timeout(type);
    }

    bool has_response() const {
        return engine.has_outgoing();
    }

    // Decodes the pending response; nullopt if the engine has not sent one.
    std::optional<iso2_exiDocument> take_response() {
        const auto outgoing = engine.take_outgoing();
        if (not outgoing.has_value()) {
            return std::nullopt;
        }
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, out_buffer.data(), outgoing->payload_size, 0, nullptr);
        iso2_exiDocument doc;
        if (decode_iso2_exiDocument(&stream, &doc) != 0) {
            return std::nullopt;
        }
        return doc;
    }

    void to_current_demand() {
        message_2::SessionSetupRequest setup;
        setup.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        send(setup);
        const auto setup_res = take_response();
        REQUIRE(setup_res.has_value());
        const auto& id = setup_res->V2G_Message.Header.SessionID;
        std::copy_n(id.bytes, std::min<size_t>(id.bytesLen, session_id.size()), session_id.begin());

        drive(message_2::ServiceDiscoveryRequest{});
        message_2::PaymentServiceSelectionRequest payment;
        payment.selected_payment_option = dt::PaymentOption::ExternalPayment;
        payment.selected_service_list.push_back({1, std::nullopt});
        drive(payment);
        control(d20::AuthorizationResponse{true});
        drive(message_2::AuthorizationRequest{});

        message_2::ChargeParameterDiscoveryRequest parameters;
        parameters.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        auto& ev_params = parameters.dc_ev_charge_parameter.emplace();
        ev_params.ev_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        ev_params.ev_maximum_voltage_limit = dt::to_physical_value(400.0, dt::Unit::V);
        drive(parameters);

        control(d20::CpStateChanged{d20::CpState::C});
        drive(message_2::CableCheckRequest{});
        control(d20::CableCheckFinished{true});
        drive(message_2::CableCheckRequest{});

        message_2::PreChargeRequest pre_charge;
        pre_charge.ev_target_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        pre_charge.ev_target_current = dt::to_physical_value(2.0, dt::Unit::A);
        drive(pre_charge);
        drive(power_delivery(dt::ChargeProgress::Start));

        control(d20::PresentVoltageCurrent{400.0f, 100.0f});
        message_2::CurrentDemandRequest demand;
        demand.ev_target_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        demand.ev_target_current = dt::to_physical_value(100.0, dt::Unit::A);
        drive(demand);
        const auto demand_res = take_response();
        REQUIRE(demand_res.has_value());
        REQUIRE(demand_res->V2G_Message.Body.CurrentDemandRes_isUsed);
        REQUIRE(demand_res->V2G_Message.Body.CurrentDemandRes.ResponseCode == iso2_responseCodeType_OK);
    }

    static message_2::PowerDeliveryRequest power_delivery(dt::ChargeProgress progress) {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = progress;
        req.sa_schedule_tuple_id = 1;
        return req;
    }

    bool saw_signal(Signal signal) const {
        return std::find(signals.begin(), signals.end(), signal) != signals.end();
    }

    D2SeccEngine engine;
    std::vector<Signal> signals;

private:
    template <typename Request> void send(const Request& request) {
        engine.take_outgoing();
        const io::StreamOutputView view{request_buffer.data(), request_buffer.size()};
        const auto len = message_2::serialize(request, view);
        engine.on_packet(io::v2gtp::PayloadType::SAP, io::StreamInputView{request_buffer.data(), len});
    }

    session::feedback::Callbacks make_callbacks() {
        session::feedback::Callbacks callbacks;
        callbacks.signal = [this](Signal signal) { signals.push_back(signal); };
        return callbacks;
    }

    const bool logging_silenced{[] {
        io::set_logging_callback([](LogLevel, std::string) {});
        return true;
    }()};
    std::array<uint8_t, 4096> out_buffer{};
    std::array<uint8_t, 4096> request_buffer{};
    d20::Timeouts timeouts;
    dt::SessionId session_id{};
    std::optional<d2::PauseContext> pause_ctx;
};

} // namespace

SCENARIO("ISO 15118-2 SECC engine holds PowerDeliveryRes(stop) until the DC output is disabled") {
    IsoEngine secc;
    secc.to_current_demand();

    GIVEN("An emergency shutdown was reported before the EV stops charging") {
        secc.control(d20::EvseError{d20::EvseErrorCode::EmergencyShutdown});
        secc.drive(IsoEngine::power_delivery(dt::ChargeProgress::Stop));

        THEN("PowerDeliveryRes goes out FAILED at once instead of waiting for the DC output [V2G2-539]") {
            REQUIRE(secc.saw_signal(Signal::DC_OPEN_CONTACTOR));
            REQUIRE(secc.has_response());
            const auto res = secc.take_response();
            REQUIRE(res.has_value());
            REQUIRE(res->V2G_Message.Body.PowerDeliveryRes_isUsed);
            REQUIRE(res->V2G_Message.Body.PowerDeliveryRes.ResponseCode == iso2_responseCodeType_FAILED);
            REQUIRE(secc.engine.is_finished());
        }
    }

    GIVEN("The EV stops charging with PowerDeliveryReq(Stop)") {
        secc.drive(IsoEngine::power_delivery(dt::ChargeProgress::Stop));

        THEN("The contactor is opened and no response is sent yet") {
            REQUIRE(secc.saw_signal(Signal::CHARGE_LOOP_FINISHED));
            REQUIRE(secc.saw_signal(Signal::DC_OPEN_CONTACTOR));
            REQUIRE_FALSE(secc.has_response());
            REQUIRE_FALSE(secc.engine.is_finished());
        }

        WHEN("The module reports the DC output disabled") {
            secc.control(d20::DcOutputDisabled{});

            THEN("PowerDeliveryRes goes out OK") {
                const auto res = secc.take_response();
                REQUIRE(res.has_value());
                REQUIRE(res->V2G_Message.Body.PowerDeliveryRes_isUsed);
                REQUIRE(res->V2G_Message.Body.PowerDeliveryRes.ResponseCode == iso2_responseCodeType_OK);
                REQUIRE_FALSE(secc.engine.is_finished());
            }
        }

        WHEN("An emergency shutdown is reported while the response is held") {
            secc.control(d20::EvseError{d20::EvseErrorCode::EmergencyShutdown});

            // The emergency guard (2 s) would otherwise close the connection before the 3 s stop cap.
            THEN("PowerDeliveryRes goes out FAILED at once and the session ends with it [V2G2-539]") {
                REQUIRE(secc.has_response());
                REQUIRE_FALSE(secc.engine.is_finished());
                const auto res = secc.take_response();
                REQUIRE(res.has_value());
                REQUIRE(res->V2G_Message.Body.PowerDeliveryRes_isUsed);
                REQUIRE(res->V2G_Message.Body.PowerDeliveryRes.ResponseCode == iso2_responseCodeType_FAILED);
                REQUIRE(secc.engine.is_finished());
            }

            THEN("The emergency guard afterwards has nothing left to do") {
                secc.timeout(d20::TimeoutType::EMERGENCY_SHUTDOWN);
                const auto res = secc.take_response();
                REQUIRE(res.has_value());
                REQUIRE(res->V2G_Message.Body.PowerDeliveryRes.ResponseCode == iso2_responseCodeType_FAILED);
            }
        }
    }
}
