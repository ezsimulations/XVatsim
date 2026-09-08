cmake_minimum_required(VERSION 3.22)

add_executable(XVatsimStep6PdcVisualProof
    src/main.cpp
)

add_library(XVatsimStep6OverlayVisualProofRaster STATIC
    ${CMAKE_SOURCE_DIR}/modules/overlay/src/OverlayWindow.cpp
)

target_compile_features(XVatsimStep6OverlayVisualProofRaster PUBLIC cxx_std_17)
target_compile_definitions(XVatsimStep6OverlayVisualProofRaster PRIVATE
    XVATSIM_STEP3_OFFLINE_VISUAL_PROOF=1
    IBM=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM400=1
    NOMINMAX WIN32_LEAN_AND_MEAN
)
target_include_directories(XVatsimStep6OverlayVisualProofRaster PUBLIC
    ${CMAKE_SOURCE_DIR}/modules/overlay/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
target_include_directories(XVatsimStep6OverlayVisualProofRaster PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)
target_link_libraries(XVatsimStep6OverlayVisualProofRaster PUBLIC
    XVatsimOverlayAccessoryCore XVatsimBrain gdiplus
)

target_compile_features(XVatsimStep6PdcVisualProof PRIVATE cxx_std_17)
target_compile_definitions(XVatsimStep6PdcVisualProof PRIVATE
    IBM=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM400=1
    NOMINMAX WIN32_LEAN_AND_MEAN
)
target_include_directories(XVatsimStep6PdcVisualProof PRIVATE
    ${CMAKE_SOURCE_DIR}/brain/include
    ${CMAKE_SOURCE_DIR}/modules/overlay/include
    ${CMAKE_SOURCE_DIR}/modules/xpilot_bridge/include
)
target_link_libraries(XVatsimStep6PdcVisualProof PRIVATE
    XVatsimBrain XVatsimOverlayAccessoryCore
    XVatsimStep6OverlayVisualProofRaster XVatsimXPilotBridge
    "${XPLANE_SDK_ROOT}/Libraries/Win/XPLM_64.lib"
    bcrypt gdiplus
)

set_target_properties(XVatsimStep6PdcVisualProof PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
)
