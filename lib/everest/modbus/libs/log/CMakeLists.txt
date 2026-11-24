add_library(modbus-log logs.cpp)
target_include_directories(modbus-log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(everest::modbus_log ALIAS modbus-log)
