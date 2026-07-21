ev_add_module(AcTemperatureDerating)
ev_add_module(EnergyManager)
ev_add_module(EnergyNode)

set(EVEREST_BUILD_MODULE_EEBUS "AUTO" CACHE STRING "Build the EEBUS module (AUTO, ON, OFF)")
set_property(CACHE EVEREST_BUILD_MODULE_EEBUS PROPERTY STRINGS "AUTO" "ON" "OFF")
string(TOUPPER "${EVEREST_BUILD_MODULE_EEBUS}" EEBUS_BUILD_MODE)
if (NOT EEBUS_BUILD_MODE MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "EVEREST_BUILD_MODULE_EEBUS must be AUTO, ON or OFF (got '${EVEREST_BUILD_MODULE_EEBUS}')")
endif()

if (NOT EEBUS_BUILD_MODE STREQUAL "OFF")
    include(${PROJECT_SOURCE_DIR}/cmake/eebus-dependency-check.cmake)
    eebus_check_dependencies(
        RESULT_VAR EEBUS_DEPENDENCIES_AVAILABLE
        REASON_VAR EEBUS_MISSING_DEPENDENCIES
    )
    if (EEBUS_DEPENDENCIES_AVAILABLE)
        ev_add_module(EEBUS)
    elseif (EEBUS_BUILD_MODE STREQUAL "ON")
        message(FATAL_ERROR "EVEREST_BUILD_MODULE_EEBUS=ON but dependencies are missing: ${EEBUS_MISSING_DEPENDENCIES}")
    else()
        message(WARNING "Not building the EEBUS module, missing dependencies: ${EEBUS_MISSING_DEPENDENCIES}")
    endif()
endif()
