add_executable(XVatsimRegressionHarness
    src/main.cpp
)

target_compile_features(XVatsimRegressionHarness PRIVATE cxx_std_17)

target_link_libraries(XVatsimRegressionHarness PRIVATE
    XVatsimBrain
    XVatsimHarnessLegacyArrival
    XVatsimHarnessLegacyDeparture
    XVatsimHarnessLegacyEnroute
    XVatsimRouteSector
    XVatsimWorkflowCore
)

if (WIN32)
    target_link_libraries(XVatsimRegressionHarness PRIVATE
        "${XPLANE_SDK_ROOT}/Libraries/Win/XPLM_64.lib"
    )
endif()

set_target_properties(XVatsimRegressionHarness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tools"
)
