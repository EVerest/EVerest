set(TEST_TARGET ${PROJECT_NAME}_EvSimulator_tests)
add_executable(${TEST_TARGET})

# Depend on module build so ev-cli has generated Conf + Intf headers
add_dependencies(${TEST_TARGET} ${MODULE_NAME})
get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)
target_include_directories(${TEST_TARGET} PRIVATE
    ..                                                  # module root (FsmContext.hpp etc.)
    ${GENERATED_INCLUDE_DIR}                            # framework-generated common headers
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME} # module-specific generated headers (Conf, *Intf)
)

target_sources(${TEST_TARGET} PRIVATE
    EventsTest.cpp
    StateTransitionsTest.cpp
    AcLimitTest.cpp
    SocIntegratorTest.cpp
    BcbTogglingTest.cpp
    PausedResumeGateTest.cpp
    ScenarioDispatcherTest.cpp
    RampInterpolatorTest.cpp
    TimerFdFlushTest.cpp
    CommCheckHandlerTeardownTest.cpp
    SilentFailureFixesTest.cpp
    CommandRouterTest.cpp
    ConfigureSessionTest.cpp
    RuntimeTickTimerTest.cpp
    PeerMocks.cpp
    PublisherSink.cpp
    TimerSink.cpp
    ../main/EventDispatch.cpp
    ../main/Events.cpp
    ../main/FsmContext.cpp
    ../main/StateBase.cpp
    ../main/states/Disabled.cpp
    ../main/states/Unplugged.cpp
    ../main/states/Plugged.cpp
    ../main/states/SlacMatching.cpp
    ../main/states/V2GNegotiating.cpp
    ../main/states/BcbToggling.cpp
    ../main/states/Charging.cpp
    ../main/states/ChargingPwmPaused.cpp
    ../main/states/Paused.cpp
    ../main/states/Stopping.cpp
    ../main/states/Faulted.cpp
    ../main/RampInterpolator.cpp
    ../main/ScenarioDispatcher.cpp
    ../main/SocIntegrator.cpp)
target_compile_definitions(${TEST_TARGET} PRIVATE BUILD_TESTING_MODULE_EV_SIMULATOR)
target_link_libraries(${TEST_TARGET}
    PRIVATE everest::framework everest::log everest::util everest::everest_api_types everest::io
            Catch2::Catch2WithMain Threads::Threads)

if (NOT DISABLE_EDM)
    list(APPEND CMAKE_MODULE_PATH ${CPM_PACKAGE_catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(${TEST_TARGET})
endif ()

add_test(${TEST_TARGET} ${TEST_TARGET})
ev_register_test_target(${TEST_TARGET})
