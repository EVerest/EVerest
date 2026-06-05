if(NOT TARGET ryml::ryml)
    find_package(ryml REQUIRED)
endif()

add_executable(mosquitto_client test_mosquitto_client.cpp)

target_link_libraries(mosquitto_client
    PRIVATE
        everest::io
)

target_compile_features(mosquitto_client PUBLIC cxx_std_17)
target_compile_options(mosquitto_client PRIVATE -fsanitize=address -g)
target_link_options(mosquitto_client PRIVATE -static-libasan -fsanitize=address)


add_executable(test_serial test_serial.cpp)

target_link_libraries(test_serial
  PRIVATE
  everest::io
)

target_compile_features(test_serial PUBLIC cxx_std_17)
target_compile_options(test_serial PRIVATE -fsanitize=address -g)
target_link_options(test_serial PRIVATE -static-libasan -fsanitize=address)

###

add_executable(test_socket_can test_socket_can.cpp)

target_link_libraries(test_socket_can
  PRIVATE
  everest::io
)

target_compile_features(test_socket_can PUBLIC cxx_std_17)
target_compile_options(test_socket_can PRIVATE -fsanitize=address -g)
target_link_options(test_socket_can PRIVATE -static-libasan -fsanitize=address)

###

add_executable(test_tcp_client test_tcp_client.cpp)

target_link_libraries(test_tcp_client
  PRIVATE
  everest::io
)

target_compile_features(test_tcp_client PUBLIC cxx_std_17)
target_compile_options(test_tcp_client PRIVATE -fsanitize=address -g)
target_link_options(test_tcp_client PRIVATE -static-libasan -fsanitize=address)

###

add_executable(test_tun_tap test_tun_tap.cpp)

target_link_libraries(test_tun_tap
  PRIVATE
  everest::io
)

target_compile_features(test_tun_tap PUBLIC cxx_std_17)
target_compile_options(test_tun_tap PRIVATE -fsanitize=address -g)
target_link_options(test_tun_tap PRIVATE -static-libasan -fsanitize=address)

###

add_executable(test_udp_client test_udp_client.cpp)

target_link_libraries(test_udp_client
  PRIVATE
  everest::io
)

target_compile_features(test_udp_client PUBLIC cxx_std_17)
target_compile_options(test_udp_client PRIVATE -fsanitize=address -g)
target_link_options(test_udp_client PRIVATE -static-libasan -fsanitize=address)

###

add_executable(test_udp_server test_udp_server.cpp)

target_link_libraries(test_udp_server
  PRIVATE
  everest::io
)

target_compile_features(test_udp_server PUBLIC cxx_std_17)
target_compile_options(test_udp_server PRIVATE -fsanitize=address -g)
target_link_options(test_udp_server PRIVATE -static-libasan -fsanitize=address)

###

add_executable(test_udp_unconnected_client test_udp_unconnected_client.cpp)

target_link_libraries(test_udp_unconnected_client
    PRIVATE
        everest::io
)

target_compile_features(test_udp_unconnected_client PUBLIC cxx_std_17)
target_compile_options(test_udp_unconnected_client PRIVATE -fsanitize=address -g)
target_link_options(test_udp_unconnected_client PRIVATE -static-libasan -fsanitize=address)

###

add_library(shm_to_mqtt_bridge_config STATIC
    shm_to_mqtt_bridge/bridge_config.cpp
)

target_link_libraries(shm_to_mqtt_bridge_config
    PUBLIC
        everest::io
        ryml::ryml
)

target_include_directories(shm_to_mqtt_bridge_config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/shm_to_mqtt_bridge
)

target_compile_features(shm_to_mqtt_bridge_config PUBLIC cxx_std_17)

add_executable(shm_to_mqtt_bridge shm_to_mqtt_bridge/shm_to_mqtt_bridge.cpp)

target_link_libraries(shm_to_mqtt_bridge
    PRIVATE
        everest::io
        shm_to_mqtt_bridge_config
)

target_compile_features(shm_to_mqtt_bridge PUBLIC cxx_std_17)

###

add_executable(shm_ping_pong_server shm_ping_pong/shm_ping_pong_server.cpp)

target_link_libraries(shm_ping_pong_server
    PRIVATE
        everest::io
)

target_compile_features(shm_ping_pong_server PUBLIC cxx_std_17)

###

add_executable(shm_ping_pong_client_a shm_ping_pong/shm_ping_pong_client_a.cpp)

target_link_libraries(shm_ping_pong_client_a
    PRIVATE
        everest::io
)

target_compile_features(shm_ping_pong_client_a PUBLIC cxx_std_17)

###

add_executable(shm_ping_pong_client_b shm_ping_pong/shm_ping_pong_client_b.cpp)

target_link_libraries(shm_ping_pong_client_b
    PRIVATE
        everest::io
)

target_compile_features(shm_ping_pong_client_b PUBLIC cxx_std_17)
