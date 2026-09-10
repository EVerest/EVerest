#
# define which modules need a certain dependency
#

ev_define_dependency(
    DEPENDENCY_NAME sigslot
    DEPENDENT_MODULES_LIST EnergyNode EvseManager MicroMegaWattBSP YetiDriver PhyVersoBSP)

ev_define_dependency(
    DEPENDENCY_NAME pugixml
    DEPENDENT_MODULES_LIST EvseManager)

ev_define_dependency(
    DEPENDENCY_NAME libtimer
    DEPENDENT_MODULES_LIST Auth LemDCBM400600 System)

ev_define_dependency(
    DEPENDENCY_NAME libcurl
    DEPENDENT_MODULES_LIST LemDCBM400600 IsabellenhuetteIemDcr)

ev_define_dependency(
    DEPENDENCY_NAME libocpp
    DEPENDENT_MODULES_LIST OCPP OCPP201)

ev_define_dependency(
    DEPENDENCY_NAME Josev
    DEPENDENT_MODULES_LIST PyEvJosev)

ev_define_dependency(
    DEPENDENCY_NAME libcbv2g
    OUTPUT_VARIABLE_SUFFIX LIBCBV2G
    DEPENDENT_MODULES_LIST EvseV2G)

ev_define_dependency(
    DEPENDENCY_NAME libevse-security
    OUTPUT_VARIABLE_SUFFIX LIBEVSE_SECURITY
    DEPENDENT_MODULES_LIST OCPP OCPP201 EvseSecurity EvseV2G)

ev_define_dependency(
    DEPENDENCY_NAME everest-sqlite
    DEPENDENT_MODULES_LIST ErrorHistory)

ev_define_dependency(
    DEPENDENCY_NAME libiso15118
    OUTPUT_VARIABLE_SUFFIX LIBISO15118
    DEPENDENT_MODULES_LIST Evse15118D20)

ev_define_dependency(
    DEPENDENCY_NAME libnfc-nci
    OUTPUT_VARIABLE_SUFFIX LIBNFC_NCI
    DEPENDENT_MODULES_LIST PN7160TokenProvider)

# the pionix_chargebridge application needs ftxui as well, but applications are no modules and
# therefore cannot be listed in DEPENDENT_MODULES_LIST below - enable the dependency up front,
# still honoring EVEREST_EXCLUDE_DEPENDENCIES and an externally set EVEREST_DEPENDENCY_ENABLED_FTXUI
if(EVEREST_BUILD_APPLICATIONS AND NOT DEFINED EVEREST_DEPENDENCY_ENABLED_FTXUI
        AND NOT "ftxui" IN_LIST EVEREST_EXCLUDE_DEPENDENCIES)
    message(STATUS "Enabling dependency ftxui because EVEREST_BUILD_APPLICATIONS=ON (pionix_chargebridge)")
    set(EVEREST_DEPENDENCY_ENABLED_FTXUI ON)
endif()

ev_define_dependency(
    DEPENDENCY_NAME ftxui
    DEPENDENT_MODULES_LIST BUDisplayMessage BUEvseBoardSupport BUIsolationMonitor BUmMWcar BUNfcReader BUOverVoltageMonitor BUPowermeter BUPowerSupplyDC BUSystem)

ev_define_dependency(
    DEPENDENCY_NAME sdbus-cpp
    OUTPUT_VARIABLE_SUFFIX SDBUS_CPP
    DEPENDENT_MODULES_LIST Linux_Systemd_Rauc)

ev_define_dependency(
    DEPENDENCY_NAME json-rpc-cxx
    OUTPUT_VARIABLE_SUFFIX JSON_RPC_CXX
    DEPENDENT_MODULES_LIST RpcApi)

if(NOT everest-gpio IN_LIST EVEREST_EXCLUDE_DEPENDENCIES)
    set(EVEREST_DEPENDENCY_ENABLED_EVEREST_GPIO ON)
else()
    set(EVEREST_DEPENDENCY_ENABLED_EVEREST_GPIO OFF)
    message(STATUS "Dependency everest-gpio NOT enabled")
endif()

ev_define_dependency(
    DEPENDENCY_NAME grpc
    OUTPUT_VARIABLE_SUFFIX GRPC
    DEPENDENT_MODULES_LIST EEBUS
)

ev_define_dependency(
    DEPENDENCY_NAME eebus-grpc
    OUTPUT_VARIABLE_SUFFIX EEBUS_GRPC
    DEPENDENT_MODULES_LIST EEBUS
)

ev_define_dependency(
    DEPENDENCY_NAME grpc-extended-cpp-plugin
    OUTPUT_VARIABLE_SUFFIX GRPC_EXTENDED_CPP_PLUGIN
    DEPENDENT_MODULES_LIST EEBUS
)
