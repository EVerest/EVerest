find_package(Threads REQUIRED)

add_library(iso15118)
add_library(iso15118::iso15118 ALIAS iso15118)
target_include_directories(iso15118
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_sources(iso15118
    PRIVATE
        misc/helper.cpp
        misc/cb_exi.cpp
        misc/random.cpp

        io/connection_plain.cpp
        io/connection_client_plain.cpp
        io/logging.cpp
        io/poll_manager.cpp
        io/sdp_packet.cpp
        io/sdp_client.cpp
        io/sdp_server.cpp
        io/socket_helper.cpp

        session/feedback.cpp
        session/ev_feedback.cpp
        session/protocol.cpp
        session/iso.cpp
        session/ev.cpp
        session/ev_sap.cpp
        session/secc_sap.cpp
        session/config.cpp
        session/d20_secc_engine.cpp
        session/d20_ev_engine.cpp
        session/d2_ev_engine.cpp
        session/d2_secc_engine.cpp
        session/din_ev_engine.cpp
        session/din_secc_engine.cpp
        session/logger.cpp

        din/context.cpp

        # din (DIN SPEC 70121) SECC state machine
        din/state/session_setup.cpp
        din/state/service_discovery.cpp
        din/state/service_payment_selection.cpp
        din/state/contract_authentication.cpp
        din/state/charge_parameter_discovery.cpp
        din/state/cable_check.cpp
        din/state/pre_charge.cpp
        din/state/power_delivery.cpp
        din/state/current_demand.cpp
        din/state/welding_detection.cpp
        din/state/session_stop.cpp
        din/state/sequence_error.cpp

        din/ev/context.cpp
        din/ev/state/session_setup.cpp
        din/ev/state/service_discovery.cpp
        din/ev/state/service_payment_selection.cpp
        din/ev/state/contract_authentication.cpp
        din/ev/state/charge_parameter_discovery.cpp
        din/ev/state/cable_check.cpp
        din/ev/state/pre_charge.cpp
        din/ev/state/power_delivery.cpp
        din/ev/state/current_demand.cpp
        din/ev/state/welding_detection.cpp
        din/ev/state/session_stop.cpp

        d20/context.cpp
        d20/context_helper.cpp
        d20/control_event_queue.cpp
        d20/session.cpp
        d20/timeout.cpp

        d20/ev/context.cpp
        d20/ev/control_event_queue.cpp

        d20/ev/state/session_setup.cpp
        d20/ev/state/authorization_setup.cpp
        d20/ev/state/authorization.cpp
        d20/ev/state/service_discovery.cpp
        d20/ev/state/service_detail.cpp
        d20/ev/state/service_selection.cpp
        d20/ev/state/session_stop.cpp
        d20/ev/state/dc_charge_parameter_discovery.cpp
        d20/ev/state/ac_charge_parameter_discovery.cpp
        d20/ev/state/schedule_exchange.cpp
        d20/ev/state/dc_cable_check.cpp
        d20/ev/state/dc_pre_charge.cpp
        d20/ev/state/power_delivery.cpp
        d20/ev/state/dc_charge_loop.cpp
        d20/ev/state/ac_charge_loop.cpp
        d20/ev/state/dc_welding_detection.cpp

        # d2 (ISO 15118-2) EV state machine
        d2/context.cpp
        d2/ev/context.cpp
        d2/ev/state/session_setup.cpp
        d2/ev/state/service_discovery.cpp
        d2/ev/state/payment_service_selection.cpp
        d2/ev/state/payment_details.cpp
        d2/ev/state/certificate_installation.cpp
        d2/ev/state/authorization.cpp
        d2/ev/state/charge_parameter_discovery.cpp
        d2/ev/state/power_delivery.cpp
        d2/ev/state/cable_check.cpp
        d2/ev/state/pre_charge.cpp
        d2/ev/state/current_demand.cpp
        d2/ev/state/welding_detection.cpp
        d2/ev/state/charging_status.cpp
        d2/ev/state/session_stop.cpp

        # d2 (ISO 15118-2) SECC state machine
        d2/crypto.cpp
        d2/state/sequence_error.cpp
        d2/state/session_setup.cpp
        d2/state/service_discovery.cpp
        d2/state/service_detail.cpp
        d2/state/payment_service_selection.cpp
        d2/state/payment_details.cpp
        d2/state/certificate_installation.cpp
        d2/state/authorization.cpp
        d2/state/charge_parameter_discovery.cpp
        d2/state/cable_check.cpp
        d2/state/pre_charge.cpp
        d2/state/power_delivery.cpp
        d2/state/current_demand.cpp
        d2/state/charging_status.cpp
        d2/state/welding_detection.cpp
        d2/state/session_stop.cpp
        d2/state/metering_receipt.cpp

        d20/state/session_setup.cpp
        d20/state/authorization_setup.cpp
        d20/state/authorization.cpp
        d20/state/service_discovery.cpp
        d20/state/service_detail.cpp
        d20/state/service_selection.cpp
        d20/state/ac_charge_parameter_discovery.cpp
        d20/state/ac_der_iec_charge_parameter_discovery.cpp
        d20/state/dc_charge_parameter_discovery.cpp
        d20/state/schedule_exchange.cpp
        d20/state/dc_cable_check.cpp
        d20/state/dc_pre_charge.cpp
        d20/state/power_delivery.cpp
        d20/state/dc_charge_loop.cpp        
        d20/state/dc_welding_detection.cpp
        d20/state/ac_charge_loop.cpp
        d20/state/ac_der_iec_charge_loop.cpp
        d20/state/session_stop.cpp
        
        message/variant.cpp
        message/supported_app_protocol.cpp
        message/session_setup.cpp
        message/common_types.cpp
        message/authorization_setup.cpp
        message/authorization.cpp
        message/service_discovery.cpp
        message/service_detail.cpp
        message/service_selection.cpp
        message/ac_charge_parameter_discovery.cpp
        message/dc_charge_parameter_discovery.cpp
        message/schedule_exchange.cpp
        message/dc_cable_check.cpp
        message/dc_pre_charge.cpp
        message/power_delivery.cpp
        message/ac_charge_loop.cpp
        message/dc_charge_loop.cpp
        message/dc_welding_detection.cpp
        message/session_stop.cpp
        message/ac_der_charge_parameter_discovery.cpp
        message/ac_der_charge_loop.cpp
        message/ac_der_sae_charge_parameter_discovery.cpp
        message/ac_der_sae_charge_loop.cpp
        message/ac_der_sae_types.cpp

        # message_din (DIN SPEC 70121)
        message_din/variant.cpp
        message_din/common_types.cpp
        message_din/session_setup.cpp
        message_din/service_discovery.cpp
        message_din/service_payment_selection.cpp
        message_din/contract_authentication.cpp
        message_din/charge_parameter_discovery.cpp
        message_din/cable_check.cpp
        message_din/pre_charge.cpp
        message_din/power_delivery.cpp
        message_din/current_demand.cpp
        message_din/welding_detection.cpp
        message_din/session_stop.cpp

        # message_2 (ISO 15118-2)
        message_2/variant.cpp
        message_2/common_types.cpp
        message_2/session_setup.cpp
        message_2/service_discovery.cpp
        message_2/service_detail.cpp
        message_2/payment_service_selection.cpp
        message_2/payment_details.cpp
        message_2/authorization.cpp
        message_2/charge_parameter_discovery.cpp
        message_2/power_delivery.cpp
        message_2/charging_status.cpp
        message_2/cable_check.cpp
        message_2/pre_charge.cpp
        message_2/current_demand.cpp
        message_2/welding_detection.cpp
        message_2/session_stop.cpp
        message_2/metering_receipt.cpp
        message_2/certificate_installation.cpp

        tbd_controller.cpp
        ev_controller.cpp
)

target_link_libraries(iso15118
    PUBLIC
        # FIXME (aw): would be nice if we could make this private!
        cbv2g::tp
        cbv2g::iso20
        cbv2g::iso2
        cbv2g::din
        everest::util
    PRIVATE
        Threads::Threads
)

target_link_libraries(iso15118
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        everest::tls
)
target_sources(iso15118
    PRIVATE
    io/connection_ssl.cpp
    io/connection_client_ssl.cpp
)

# FIXME (aw): do we want to have this public here?
target_compile_features(iso15118 PUBLIC cxx_std_17)

target_compile_options(iso15118 PRIVATE ${ISO15118_COMPILE_OPTIONS_WARNING})
