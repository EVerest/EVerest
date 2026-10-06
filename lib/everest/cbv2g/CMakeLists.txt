cmake_minimum_required(VERSION 3.14)

project(cb_v2g
    VERSION 0.3.1
    DESCRIPTION "V2GTP EXI library"
    HOMEPAGE_URL "https://github.com/Everest/libcbv2g"
	LANGUAGES C CXX
)

add_subdirectory(lib)

message(STATUS "library code based on cbexigen generator version: merge of 074de6417 and 1693dd510")

# tests
if (CB_V2G_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

# packaging
install(
    TARGETS
        cbv2g_exi_codec
        cbv2g_din
        cbv2g_iso2
        cbv2g_iso20
        cbv2g_tp
    EXPORT ${EVEREST_EXPORT_SET}
    LIBRARY
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)
