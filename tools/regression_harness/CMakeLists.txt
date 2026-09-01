add_executable(XVatsimRegressionHarness
    src/main.cpp
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
    XVatsimMetarProof
    XVatsimAirportFrequencyCatalog
    XVatsimHarnessLegacyArrival
    XVatsimHarnessLegacyDeparture
    XVatsimHarnessLegacyEnroute
    XVatsimOverlayAccessoryCore
    XVatsimRouteSector
    XVatsimSettingsStore
    XVatsimTerminalAuthority
    XVatsimTransceiverResolver
    XVatsimUpdateChecker
    XVatsimVatsimDataFeed
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
