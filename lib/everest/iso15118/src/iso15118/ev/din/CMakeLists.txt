# DIN SPEC 70121 EV engine
target_sources(iso15118
    PRIVATE
        context.cpp
        timeouts.cpp
        engine.cpp
        state/session_setup.cpp
        state/session_stop.cpp
        state/service_discovery.cpp
        state/service_payment_selection.cpp
        state/contract_authentication.cpp
        state/charge_parameter_discovery.cpp
        state/cable_check.cpp
        state/pre_charge.cpp
        state/power_delivery.cpp
        state/current_demand.cpp
        state/welding_detection.cpp
)
