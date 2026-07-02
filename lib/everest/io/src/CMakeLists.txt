
add_library(everest_io SHARED)
add_library(everest::io ALIAS everest_io)

set_target_properties(everest_io PROPERTIES
  VERSION 0.1.0
  SOVERSION 0
  EXPORT_NAME io
)

target_sources(everest_io
    PRIVATE
        event/fd_event_handler.cpp
        event/unique_fd.cpp
        event/event_fd.cpp
        event/timer_fd.cpp
        can/socket_can_handler.cpp
        can/can_recv_filter.cpp
        can/can_payload.cpp
        socket/socket.cpp
        utilities/generic_error_state.cpp
        event/fd_event_client.cpp
        udp/udp_socket.cpp
        udp/udp_unconnected_socket.cpp
        udp/udp_dualstack_server_socket.cpp
        udp/endpoint.cpp
        udp/udp_payload.cpp
        utilities/stop_watch.cpp
        serial/serial.cpp
        serial/pty_handler.cpp
        serial/event_pty.cpp
        mdns/mdns.cpp
        mdns/mdns_socket.cpp
        tun_tap/tap_handler.cpp
        tcp/tcp_socket.cpp
        netlink/vcan_netlink_manager.cpp
        raw/raw_socket.cpp
)

target_include_directories(everest_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

if(EVEREST_IO_WITH_MQTT)
    target_sources(everest_io
        PRIVATE
            mqtt/mosquitto_cpp.cpp
            mqtt/mqtt_client.cpp
    )
    target_include_directories(everest_io
        PUBLIC
            $<BUILD_INTERFACE:${mosquitto_SOURCE_DIR}/include>
    )
endif()

target_link_libraries(everest_io
    PUBLIC
        atomic
        $<BUILD_INTERFACE:everest_util>
        $<INSTALL_INTERFACE:everest::util>
)

include(CheckIncludeFile)
check_include_file(net/ethernet.h HAVE_NET_ETHERNET_H)
if(HAVE_NET_ETHERNET_H AND USING_MUSL)
    target_compile_definitions(everest_io PRIVATE _NETINET_IF_ETHER_H)
endif()

if(EVEREST_NO_PACKET_IGNORE_OUTGOING)
    target_compile_definitions(everest_io PRIVATE EVEREST_NO_PACKET_IGNORE_OUTGOING)
endif()

target_compile_features(everest_io PUBLIC cxx_std_17)

target_compile_options(everest_io
    PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wnon-virtual-dtor
        -Wunused
        -Wpedantic
        -Wformat=2
        -Werror
)

include(CMakePackageConfigHelpers)

set_target_properties(everest_io
    PROPERTIES
        EXPORT_NAME io
        POSITION_INDEPENDENT_CODE ON
)

if(EVEREST_IO_WITH_MQTT)
  if(DISABLE_EDM)
    target_link_libraries(everest_io
      PRIVATE
      mosquitto
    )
  else()
    target_link_libraries(everest_io
      PRIVATE
      libmosquitto
    )
    install(TARGETS libmosquitto
        EXPORT everest-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
  endif()
endif()

install(TARGETS everest_io
    EXPORT everest-core-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(EVEREST_IO_WITH_MQTT)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/everest
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp"
    )
else()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/everest
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp"
        PATTERN "mqtt" EXCLUDE
    )
endif()

option(EVEREST_IO_ENABLE_TLS "Build TLS client/server adapters in everest_io (requires everest::tls)" ON)

if(EVEREST_IO_ENABLE_TLS)
    target_sources(everest_io
        PRIVATE
            tls/tls_server_socket.cpp
            tls/tls_server.cpp
            tls/tls_listener.cpp
            tls/tls_client_socket.cpp
            tls/tls_client.cpp
    )
    target_link_libraries(everest_io PRIVATE $<BUILD_INTERFACE:tls>)
    # libtls is not installed as part of everest-core's export set, so its
    # public include directories are propagated to in-tree consumers only via
    # BUILD_INTERFACE. Mirrors libtls's own PUBLIC include directories.
    target_include_directories(everest_io
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lib/everest/tls>
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lib/everest/tls/include>
    )
endif()

if(DISABLE_EDM)
    evc_setup_package_no_export(
        NAME everest-io
        LIBRARY_NAME io
        IMPORTED_LOCATION_FILENAME libeverest_io.so
        NAMESPACE everest
        TYPE SHARED
    )
endif()
