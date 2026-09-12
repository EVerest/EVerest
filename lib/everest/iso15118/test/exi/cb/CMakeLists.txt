add_subdirectory(app_hand)
add_subdirectory(iso20)
# ISO 15118-2 and DIN SPEC 70121 are only coded by the EV side.
if(ISO15118_BUILD_EV_SIDE)
    add_subdirectory(iso2)
    add_subdirectory(din)
endif()
