add_library(can_device STATIC EXCLUDE_FROM_ALL)
ev_register_library_target(can_device)

target_sources(can_device
    PRIVATE
        CanDevice.cpp
)

target_include_directories(can_device
    PUBLIC
        include
)

target_link_libraries(can_device
    PUBLIC
        everest::io
    PRIVATE
        everest::log
)
