# mcs_ev_control: FTXUI control panel for the simulated EV (EvManager) on the MCS bench. It
# publishes EvManager's external MQTT commands (execute_/modify_charging_session), nothing else,
# so it depends only on ftxui and libmosquitto - both already dependencies of this tree
# (ftxui for pionix_chargebridge, libmosquitto for everest::io).
cmake_minimum_required(VERSION 3.16)

if(NOT TARGET ftxui::component)
    message(WARNING "Not building mcs_ev_control because dependency ftxui is not available.")
    return()
endif()

if(TARGET libmosquitto)
    set(MCS_EV_CONTROL_MOSQUITTO libmosquitto)
    set(MCS_EV_CONTROL_MOSQUITTO_INCLUDE "${mosquitto_SOURCE_DIR}/include")
elseif(TARGET mosquitto)
    set(MCS_EV_CONTROL_MOSQUITTO mosquitto)
    set(MCS_EV_CONTROL_MOSQUITTO_INCLUDE "")
else()
    message(WARNING "Not building mcs_ev_control because libmosquitto is not available "
                    "(EVEREST_IO_WITH_MQTT=OFF?).")
    return()
endif()

add_executable(mcs_ev_control main.cpp)
target_compile_features(mcs_ev_control PRIVATE cxx_std_17)
target_link_libraries(mcs_ev_control
    PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        ${MCS_EV_CONTROL_MOSQUITTO}
        pthread
)
if(MCS_EV_CONTROL_MOSQUITTO_INCLUDE)
    target_include_directories(mcs_ev_control PRIVATE ${MCS_EV_CONTROL_MOSQUITTO_INCLUDE})
endif()

install(TARGETS mcs_ev_control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
