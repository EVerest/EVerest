#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@533c630d-bca6-4639-a4c8-113c2f12d3f0:v1
target_compile_options(${MODULE_NAME} PUBLIC -Wall -Wextra -pedantic -Werror=switch)
target_link_libraries(${MODULE_NAME}
    PRIVATE
        atomic
        everest::everest_api_types
        everest::framework
        everest::util
        everest::io
        Threads::Threads
)
# ev@533c630d-bca6-4639-a4c8-113c2f12d3f0:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "ev_manager/ev_managerImpl.cpp"
        "main/CommandRouter.cpp"
        "main/EventDispatch.cpp"
        "main/Events.cpp"
        "main/EvSimRuntime.cpp"
        "main/FsmContext.cpp"
        "main/PeerSubscriptions.cpp"
        "main/RampInterpolator.cpp"
        "main/ScenarioDispatcher.cpp"
        "main/SocIntegrator.cpp"
        "main/StateBase.cpp"
        "main/states/Disabled.cpp"
        "main/states/Unplugged.cpp"
        "main/states/Plugged.cpp"
        "main/states/SlacMatching.cpp"
        "main/states/V2GNegotiating.cpp"
        "main/states/BcbToggling.cpp"
        "main/states/Charging.cpp"
        "main/states/ChargingPwmPaused.cpp"
        "main/states/Paused.cpp"
        "main/states/Stopping.cpp"
        "main/states/Faulted.cpp"
)

# ev@f525452c-6355-4e83-a2aa-7c89bb9b7c35:v1
# insert other things like install cmds etc here
target_compile_features(${MODULE_NAME} PRIVATE cxx_std_17)
set_target_properties(${MODULE_NAME} PROPERTIES CXX_EXTENSIONS OFF)

if (EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif ()
# ev@f525452c-6355-4e83-a2aa-7c89bb9b7c35:v1
