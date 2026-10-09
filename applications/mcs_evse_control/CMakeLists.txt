# mcs_evse_control: FTXUI control panel for the EVSE side of the MCS bench. It drives EvseManager
# and the EVSE's energy node through the EVerest API (evse_manager_consumer_API,
# external_energy_limits_consumer_API) over MQTT, so it depends only on ftxui, libmosquitto and
# nlohmann_json - all already dependencies of this tree.
if(NOT TARGET ftxui::component)
    message(WARNING "Not building mcs_evse_control because dependency ftxui is not available.")
    return()
endif()

if(DISABLE_EDM)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(mosquitto REQUIRED IMPORTED_TARGET libmosquitto)
    set(MCS_EVSE_CONTROL_MOSQUITTO PkgConfig::mosquitto)
    set(MCS_EVSE_CONTROL_MOSQUITTO_INCLUDE "")
else()
    set(MCS_EVSE_CONTROL_MOSQUITTO libmosquitto)
    set(MCS_EVSE_CONTROL_MOSQUITTO_INCLUDE "${mosquitto_SOURCE_DIR}/include")
endif()

add_executable(mcs_evse_control main.cpp)
target_compile_features(mcs_evse_control PRIVATE cxx_std_17)
target_link_libraries(mcs_evse_control
    PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        nlohmann_json::nlohmann_json
        ${MCS_EVSE_CONTROL_MOSQUITTO}
        pthread
)
if(MCS_EVSE_CONTROL_MOSQUITTO_INCLUDE)
    target_include_directories(mcs_evse_control PRIVATE ${MCS_EVSE_CONTROL_MOSQUITTO_INCLUDE})
endif()

install(TARGETS mcs_evse_control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
