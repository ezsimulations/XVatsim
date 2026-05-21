cmake_minimum_required(VERSION 3.22)

add_library(XVatsimPlugin SHARED
    src/XVatsimPlugin.cpp
)

set(XVATSIM_UI_TRANSITION_SOUND
    "${CMAKE_SOURCE_DIR}/assets/audio/ui_transition.mp3")
set(XVATSIM_AUTHORITY_SOURCE_REGISTRY
    "${CMAKE_SOURCE_DIR}/assets/source_data/authority_source_registry.json")

target_compile_features(XVatsimPlugin PRIVATE cxx_std_17)

target_include_directories(XVatsimPlugin PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
    ${XPLANE_SDK_ROOT}/CHeaders/Widgets
    ${XPLANE_SDK_ROOT}/CHeaders/Wrappers
)

target_compile_definitions(XVatsimPlugin PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
    XVATSIM_ENABLE_CONTROLLER_MESSAGES=$<BOOL:${XVATSIM_ENABLE_CONTROLLER_MESSAGES}>
)

if (MSVC)
    target_compile_definitions(XVatsimPlugin PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(XVatsimPlugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "XVatsim"
    SUFFIX ".xpl"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
)

target_link_libraries(XVatsimPlugin PRIVATE
    XVatsimAircraftState
    XVatsimBrain
    XVatsimCtafLookup
    XVatsimControllerFeed
    XVatsimDiversionContext
    XVatsimFlightPlan
    XVatsimNetworkPlanLink
    XVatsimOverlay
    XVatsimPilotIdentity
    XVatsimRadioState
    XVatsimRouteSector
    XVatsimSettingsStore
    XVatsimTransceiverResolver
    XVatsimVatsimDataFeed
    XVatsimWorkflowCore
    XVatsimXPilotBridge
)

if (WIN32)
    target_link_libraries(XVatsimPlugin PRIVATE
        gdiplus
        opengl32
        winmm
        "${XPLANE_SDK_ROOT}/Libraries/Win/XPLM_64.lib"
        "${XPLANE_SDK_ROOT}/Libraries/Win/XPWidgets_64.lib"
    )
endif()

add_custom_command(TARGET XVatsimPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${XVATSIM_UI_TRANSITION_SOUND}"
        "$<TARGET_FILE_DIR:XVatsimPlugin>/ui_transition.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${XVATSIM_AUTHORITY_SOURCE_REGISTRY}"
        "$<TARGET_FILE_DIR:XVatsimPlugin>/authority_source_registry.json"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:XVatsimPlugin>/Debug"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:XVatsimPlugin>/Release"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:XVatsimPlugin>/RelWithDebInfo"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:XVatsimPlugin>/MinSizeRel")
