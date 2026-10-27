add_library(iso15118_ev STATIC)
add_library(everest::iso15118_ev ALIAS iso15118_ev)

target_sources(iso15118_ev
    PRIVATE
        # The DIN/-2 response decoders; the SECC only decodes -20 responses.
        message_din/response_variant.cpp
        message_2/response_variant.cpp
        message/ac_der_sae_names.cpp

        crypto.cpp
        session/fsm.cpp
        session/sap_offer.cpp
        session/stop_reason.cpp
        feedback.cpp

        d20/context.cpp
        d20/context_helper.cpp
        d20/engine.cpp
        d20/secc_clock.cpp
        d20/timeouts.cpp
        d20/state/supported_app_protocol.cpp
        d20/state/session_setup.cpp
        d20/state/authorization_setup.cpp
        d20/state/authorization.cpp
        d20/state/service_discovery.cpp
        d20/state/service_detail.cpp
        d20/state/service_selection.cpp
        d20/state/schedule_exchange.cpp
        d20/state/dc_charge_parameter_discovery.cpp
        d20/state/dc_cable_check.cpp
        d20/state/dc_pre_charge.cpp
        d20/state/dc_charge_loop.cpp
        d20/state/dc_welding_detection.cpp
        d20/state/power_delivery.cpp
        d20/state/session_stop.cpp
        d20/state/ac_charge_parameter_discovery.cpp
        d20/state/ac_charge_loop.cpp
        d20/state/ac_der_iec_charge_parameter_discovery.cpp
        d20/state/ac_der_iec_charge_loop.cpp
        d20/state/ac_der_sae_charge_parameter_discovery.cpp
        d20/state/ac_der_sae_charge_loop.cpp
        d20/ac_phase_split.cpp
        d20/der/sae_validation.cpp
        d20/der/sae_requests.cpp

        detail/session/session.cpp
        controller.cpp
        session_deleter.cpp
        config/validation.cpp
        transport/sdp_client.cpp
        transport/connection.cpp
)

add_subdirectory(d2)
add_subdirectory(din)

target_link_libraries(iso15118_ev
    PUBLIC
        everest::iso15118
        everest::io
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_compile_options(iso15118_ev PRIVATE ${ISO15118_COMPILE_OPTIONS_WARNING})
