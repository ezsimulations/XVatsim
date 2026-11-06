if (WIN32)
    add_library(XVatsimXplmTestStub SHARED
        src/XplmTestStub.cpp
    )
    target_compile_features(XVatsimXplmTestStub PRIVATE cxx_std_17)
    target_compile_definitions(XVatsimXplmTestStub PRIVATE
        IBM=1
        XPLM=1
        XPLM200=1
        XPLM210=1
        XPLM300=1
        XPLM301=1
        XPLM400=1
    )
    target_include_directories(XVatsimXplmTestStub PRIVATE
        ${XPLANE_SDK_ROOT}/CHeaders/XPLM
    )
    set_target_properties(XVatsimXplmTestStub PROPERTIES
        OUTPUT_NAME "XPLM_64"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tools"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tools"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools"
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tools"
    )
endif()

add_executable(XVatsimRegressionHarness
    src/AustraliaBrainEvidenceProbe.cpp
    src/CandidateDecisionDiagnosticsProbe.cpp
    src/ControllerOperationalEligibilityProbe.cpp
    src/main.cpp
    src/PerformanceContractGateACalm1Probe.cpp
    src/PerformanceContractGateACalm2Probe.cpp
    src/PerformanceContractGateAProbe.cpp
    src/PerformanceContractGateBProbe.cpp
    src/PerformanceContractGateBTelemetryProbe.cpp
    src/PdcLogMonitorContractProbe.cpp
    src/ProductCalm1Probe.cpp
    src/RadioReceiveActivityProbe.cpp
    src/RuntimeActivationGateProbe.cpp
    src/Step6PdcContractProbe.cpp
    src/XPilot4BridgeContractProbe.cpp
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
    XVatsimOverlayAccessoryCore
    XVatsimRadioState
    XVatsimRouteSector
    XVatsimRuntimeWorkers
    XVatsimSettingsStore
    XVatsimTransceiverResolver
    XVatsimUpdateChecker
    XVatsimVatsimDataFeed
    XVatsimVnasData
    XVatsimWorkflowCore
    XVatsimXPilotBridge
)

if (WIN32)
    target_link_libraries(XVatsimRegressionHarness PRIVATE
        XVatsimXplmTestStub
        psapi
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
