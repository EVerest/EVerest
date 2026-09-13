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


        io/connection_plain.cpp
        io/logging.cpp
        io/poll_manager.cpp
        io/sdp_packet.cpp
        io/sdp_server.cpp
        io/socket_helper.cpp

        session/feedback.cpp
        session/iso.cpp

        d20/context.cpp
        d20/context_helper.cpp
        d20/control_event_queue.cpp
        d20/session.cpp
        d20/timeout.cpp
        d20/config.cpp

        d20/state/supported_app_protocol.cpp
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

        tbd_controller.cpp    
)


# The EV side is opt-in: consumers that only implement a charger (the SECC) should not pay to
# build or link it. everest-core turns it on for the EvIso15118D20 module.
if(ISO15118_BUILD_EV_SIDE)
    target_sources(iso15118
        PRIVATE
        ev/d20/context.cpp
        ev/d20/context_helper.cpp
        ev/d20/state/supported_app_protocol.cpp
        ev/d20/state/session_setup.cpp
        ev/d20/state/authorization_setup.cpp
        ev/d20/state/authorization.cpp
        ev/d20/state/dc_charge_parameter_discovery.cpp
        ev/d20/state/dc_cable_check.cpp
        ev/d20/state/dc_pre_charge.cpp
        ev/d20/state/power_delivery.cpp
        ev/d20/state/dc_welding_detection.cpp
        ev/d20/state/session_stop.cpp
        ev/session/feedback.cpp
        ev/session.cpp
        ev/controller.cpp
        ev/d20/state/service_discovery.cpp
        ev/d20/state/service_detail.cpp
        ev/d20/state/service_selection.cpp
        ev/d20/state/schedule_exchange.cpp
        ev/d20/state/dc_charge_loop.cpp
        ev/transport/sdp_client.cpp
        ev/transport/data_client.cpp
        ev/d20/state/ac_charge_parameter_discovery.cpp
        ev/d20/state/ac_charge_loop.cpp
        ev/d20/state/ac_der_iec_charge_parameter_discovery.cpp
        ev/d20/state/ac_der_iec_charge_loop.cpp
    )

    # Only the EV side uses everest::io.
    target_link_libraries(iso15118 PUBLIC everest::io)
endif()

target_link_libraries(iso15118
    PUBLIC
        # FIXME (aw): would be nice if we could make this private!
        cbv2g::tp
        cbv2g::iso20
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
)

# FIXME (aw): do we want to have this public here?
target_compile_features(iso15118 PUBLIC cxx_std_17)

target_compile_options(iso15118 PRIVATE ${ISO15118_COMPILE_OPTIONS_WARNING})
