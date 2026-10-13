// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// DIN SPEC 70121 PowerDelivery(ReadyToChargeState=FALSE) through the engine: the response waits for
// DcOutputDisabled [IEC 61851-23:2023 Table CC.9 t103 before t104], and the engine-level CP checks
// ([V2G-DC-668], [V2G-DC-962]) must treat the wait as after the stop request.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/din/din_msgDefDecoder.h>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/welding_detection.hpp>
#include <iso15118/session/din_secc_engine.hpp>

using namespace iso15118;

namespace dt = message_din::datatypes;
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

class DinEngine {
public:
    DinEngine() :
        engine(io::StreamOutputView{out_buffer.data(), out_buffer.size()}, make_session_config(), make_callbacks(),
               timeouts) {
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
    std::optional<din_exiDocument> take_response() {
        const auto outgoing = engine.take_outgoing();
        if (not outgoing.has_value()) {
            return std::nullopt;
        }
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, out_buffer.data(), outgoing->payload_size, 0, nullptr);
        din_exiDocument doc;
        if (decode_din_exiDocument(&stream, &doc) != 0) {
            return std::nullopt;
        }
        return doc;
    }

    void to_current_demand() {
        message_din::SessionSetupRequest setup;
        setup.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        send(setup);
        const auto setup_res = take_response();
        REQUIRE(setup_res.has_value());
        const auto& id = setup_res->V2G_Message.Header.SessionID;
        std::copy_n(id.bytes, std::min<size_t>(id.bytesLen, session_id.size()), session_id.begin());

        drive(message_din::ServiceDiscoveryRequest{});
        message_din::ServicePaymentSelectionRequest payment;
        payment.selected_payment_option = dt::PaymentOption::ExternalPayment;
        payment.selected_service_list.push_back({1, std::nullopt});
        drive(payment);
        control(d20::AuthorizationResponse{true});
        drive(message_din::ContractAuthenticationRequest{});

        message_din::ChargeParameterDiscoveryRequest parameters;
        parameters.ev_requested_energy_transfer_type = dt::EnergyTransferMode::DC_extended;
        auto& ev_params = parameters.dc_ev_charge_parameter.emplace();
        ev_params.ev_maximum_current_limit = 200.0;
        ev_params.ev_maximum_voltage_limit = 400.0;
        drive(parameters);

        control(d20::CpStateChanged{d20::CpState::C});
        drive(message_din::CableCheckRequest{});
        control(d20::CableCheckFinished{true});
        drive(message_din::CableCheckRequest{});

        message_din::PreChargeRequest pre_charge;
        pre_charge.ev_target_voltage = 400.0;
        pre_charge.ev_target_current = 2.0;
        drive(pre_charge);
        drive(power_delivery(true));

        control(d20::PresentVoltageCurrent{400.0f, 100.0f});
        message_din::CurrentDemandRequest demand;
        demand.ev_target_voltage = 400.0;
        demand.ev_target_current = 100.0;
        drive(demand);
        const auto demand_res = take_response();
        REQUIRE(demand_res.has_value());
        REQUIRE(demand_res->V2G_Message.Body.CurrentDemandRes_isUsed);
        REQUIRE(demand_res->V2G_Message.Body.CurrentDemandRes.ResponseCode == din_responseCodeType_OK);
    }

    static message_din::PowerDeliveryRequest power_delivery(bool ready_to_charge) {
        message_din::PowerDeliveryRequest req;
        req.ready_to_charge_state = ready_to_charge;
        return req;
    }

    bool saw_signal(Signal signal) const {
        return std::find(signals.begin(), signals.end(), signal) != signals.end();
    }

    DinSeccEngine engine;
    std::vector<Signal> signals;

private:
    template <typename Request> void send(const Request& request) {
        engine.take_outgoing();
        const io::StreamOutputView view{request_buffer.data(), request_buffer.size()};
        const auto len = message_din::serialize(request, view);
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
};

double physical_value(const din_PhysicalValueType& value) {
    return value.Value * std::pow(10.0, value.Multiplier);
}

void require_power_delivery_ok(DinEngine& secc) {
    const auto res = secc.take_response();
    REQUIRE(res.has_value());
    REQUIRE(res->V2G_Message.Body.PowerDeliveryRes_isUsed);
    REQUIRE(res->V2G_Message.Body.PowerDeliveryRes.ResponseCode == din_responseCodeType_OK);
}

} // namespace

SCENARIO("DIN SPEC 70121 SECC engine holds PowerDeliveryRes(stop) until the DC output is disabled") {
    DinEngine secc;
    secc.to_current_demand();

    GIVEN("An emergency shutdown was reported before the EV stops charging") {
        secc.control(d20::EvseError{d20::EvseErrorCode::EmergencyShutdown});
        secc.drive(DinEngine::power_delivery(false));

        THEN("PowerDeliveryRes goes out FAILED at once instead of waiting for the DC output [V2G-DC-866]") {
            REQUIRE(secc.saw_signal(Signal::DC_OPEN_CONTACTOR));
            REQUIRE(secc.has_response());
            const auto res = secc.take_response();
            REQUIRE(res.has_value());
            REQUIRE(res->V2G_Message.Body.PowerDeliveryRes_isUsed);
            REQUIRE(res->V2G_Message.Body.PowerDeliveryRes.ResponseCode == din_responseCodeType_FAILED);
            REQUIRE(secc.engine.is_finished());
        }
    }

    GIVEN("The EV stops charging with PowerDeliveryReq(ReadyToChargeState=FALSE)") {
        secc.drive(DinEngine::power_delivery(false));

        THEN("The contactor is opened and no response is sent yet") {
            REQUIRE(secc.saw_signal(Signal::CHARGE_LOOP_FINISHED));
            REQUIRE(secc.saw_signal(Signal::DC_OPEN_CONTACTOR));
            REQUIRE_FALSE(secc.has_response());
            REQUIRE_FALSE(secc.engine.is_finished());
        }

        WHEN("The EV already changes to CP State B while the response is held") {
            secc.control(d20::CpStateChanged{d20::CpState::B});

            THEN("It is not an unexpected CP State B [V2G-DC-668] and the session goes on") {
                REQUIRE_FALSE(secc.engine.is_finished());
                REQUIRE_FALSE(secc.engine.is_finished_with_error());
                REQUIRE_FALSE(secc.has_response());

                secc.control(d20::DcOutputDisabled{});
                require_power_delivery_ok(secc);
            }
        }

        WHEN("Present values from after the switch off arrive, then DcOutputDisabled") {
            secc.control(d20::PresentVoltageCurrent{0.0f, 0.0f});
            REQUIRE_FALSE(secc.has_response());
            secc.control(d20::DcOutputDisabled{});

            THEN("PowerDeliveryRes goes out OK") {
                require_power_delivery_ok(secc);
            }

            AND_WHEN("The EV changes to CP State B and runs welding detection") {
                secc.take_response();
                secc.control(d20::CpStateChanged{d20::CpState::B});
                secc.drive(message_din::WeldingDetectionRequest{});

                THEN("WeldingDetectionRes reports the voltage measured after the switch off") {
                    const auto res = secc.take_response();
                    REQUIRE(res.has_value());
                    REQUIRE(res->V2G_Message.Body.WeldingDetectionRes_isUsed);
                    const auto& welding = res->V2G_Message.Body.WeldingDetectionRes;
                    REQUIRE(welding.ResponseCode == din_responseCodeType_OK);
                    REQUIRE(physical_value(welding.EVSEPresentVoltage) == 0.0);
                }
            }
        }

        WHEN("The DC output is not reported disabled in time") {
            secc.timeout(d20::TimeoutType::CONTACTOR);

            THEN("PowerDeliveryRes goes out OK anyway") {
                require_power_delivery_ok(secc);
                REQUIRE_FALSE(secc.engine.is_finished());
            }
        }

        WHEN("The EV is unplugged while the response is held") {
            secc.control(d20::CpStateChanged{d20::CpState::A});

            THEN("The session ends without a response [V2G-DC-962]") {
                REQUIRE_FALSE(secc.has_response());
                REQUIRE(secc.engine.is_finished());
                REQUIRE(secc.engine.is_finished_with_error());
            }
        }
    }

    GIVEN("The EV changes to CP State B before it stops with PowerDeliveryReq") {
        secc.control(d20::CpStateChanged{d20::CpState::B});

        THEN("That is still an unexpected CP State B during charging [V2G-DC-668]") {
            REQUIRE(secc.engine.is_finished_with_error());
        }
    }
}
