add_executable(XVatsimRegressionHarness
    src/main.cpp
    src/PerformanceContractGateACalm1Probe.cpp
    src/PerformanceContractGateACalm2Probe.cpp
    src/PerformanceContractGateAProbe.cpp
    src/PerformanceContractGateBProbe.cpp
    src/PerformanceContractGateBTelemetryProbe.cpp
    src/ProductCalm1Probe.cpp
    src/RuntimeActivationGateProbe.cpp
    src/Step6PdcContractProbe.cpp
)

target_compile_features(XVatsimRegressionHarness PRIVATE cxx_std_17)

target_compile_definitions(XVatsimRegressionHarness PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
)

target_link_libraries(XVatsimRegressionHarness PRIVATE
    XVatsimBrain
    XVatsimFlightPlan
    XVatsimMetarProof
    XVatsimAirportFrequencyCatalog
    XVatsimHarnessLegacyArrival
    XVatsimHarnessLegacyDeparture
    XVatsimHarnessLegacyEnroute
    XVatsimOverlayAccessoryCore
    XVatsimRouteSector
    XVatsimRuntimeWorkers
    XVatsimSettingsStore
    XVatsimTerminalAuthority
    XVatsimTransceiverResolver
    XVatsimUpdateChecker
    XVatsimVatsimDataFeed
    XVatsimVnasData
    XVatsimWorkflowCore
    XVatsimXPilotBridge
)

if (WIN32)
    target_link_libraries(XVatsimRegressionHarness PRIVATE
        "${XPLANE_SDK_ROOT}/Libraries/Win/XPLM_64.lib"
        ws2_32
    )
endif()

set_target_properties(XVatsimRegressionHarness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tools"
)
