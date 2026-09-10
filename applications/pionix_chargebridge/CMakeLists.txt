cmake_minimum_required(VERSION 3.16)

# MQTT support in everest::io is mandatory for this application. everest::io exports no property to
# query it, so the cache variable (declared in the top level CMakeLists.txt, default ON) is the only
# signal; if it is not defined at all, MQTT support is assumed to be present. As with ftxui below,
# skip only this application instead of failing the whole project configuration.
if(DEFINED EVEREST_IO_WITH_MQTT AND NOT EVEREST_IO_WITH_MQTT)
    message(WARNING "Not building pionix_chargebridge because everest::io is built without MQTT support. "
            "Set EVEREST_IO_WITH_MQTT=ON or disable EVEREST_BUILD_APPLICATIONS.")
    return()
endif()

# ftxui powers the interactive terminal status UI and is mandatory for this application. It is
# provided either by the dependency manager (EVEREST_DEPENDENCY_ENABLED_FTXUI) or, for system
# dependency builds, by find_package(ftxui 6 QUIET) in the top level CMakeLists.txt. If it is not
# available, skip only this application instead of failing the whole project configuration.
if(NOT TARGET ftxui::component)
    message(WARNING "Not building pionix_chargebridge because dependency ftxui is not available. "
            "Remove ftxui from EVEREST_EXCLUDE_DEPENDENCIES, set EVEREST_DEPENDENCY_ENABLED_FTXUI=ON "
            "or install ftxui 6 for system dependency builds.")
    return()
endif()

find_package(ryml QUIET)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_executable(pionix_chargebridge
  src/everest_api/api_connector.cpp
  src/everest_api/evse_bsp_api.cpp
  src/everest_api/ovm_api.cpp
  src/everest_api/ev_bsp_api.cpp

  src/firmware_update/sync_fw_updater.cpp

  src/utilities/filesystem.cpp
  src/utilities/logging.cpp
  src/utilities/parse_config.cpp
  src/utilities/print_config.cpp
  src/utilities/print_status.cpp
  src/utilities/string.cpp
  src/utilities/symlink.cpp
  src/utilities/sync_udp_client.cpp
  src/utilities/type_converters.cpp
  src/status_ui.cpp

  src/io_bridge.cpp
  src/can_bridge.cpp
  src/charge_bridge.cpp
  src/bsp_bridge.cpp
  src/heartbeat_service.cpp
  src/plc_bridge.cpp
  src/serial_bridge.cpp
  src/discovery.cpp

  main.cpp
)

target_link_libraries(pionix_chargebridge
  everest::io
  everest::everest_api_types
  nlohmann_json::nlohmann_json
  ryml::ryml
  ftxui::component
  ftxui::dom
  ftxui::screen
)

target_include_directories(pionix_chargebridge
  PRIVATE include
  PRIVATE shared
)

set(cb_firmware_binary config/firmware/charge-bridge-fw_complete.cbfw)

add_custom_command(
  TARGET pionix_chargebridge
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  "${CMAKE_CURRENT_SOURCE_DIR}/${cb_firmware_binary}"
  "$<TARGET_FILE_DIR:pionix_chargebridge>/"
  COMMENT "Copying Pionix ChargeBridge firmware binary..."
)


install (TARGETS pionix_chargebridge)
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/${cb_firmware_binary}" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/chargebridge/firmware)
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/config-CB-EVAL.yaml" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/chargebridge RENAME "config-CB-EVAL.yaml-example")
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/config-CB-SAT-AC.yaml" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/chargebridge RENAME "config-CB-SAT-AC.yaml-example")
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/config-CB-EVAL-EV.yaml" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/chargebridge RENAME "config-CB-EVAL-EV.yaml-example")
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/config-CB-EVAL-SIM.yaml" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/chargebridge RENAME "config-CB-EVAL-SIM.yaml-example")
