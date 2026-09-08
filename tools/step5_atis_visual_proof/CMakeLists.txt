cmake_minimum_required(VERSION 3.22)

add_executable(XVatsimStep5AtisVisualProof
    src/main.cpp
)

add_library(XVatsimStep5OverlayVisualProofRaster STATIC
    ${CMAKE_SOURCE_DIR}/modules/overlay/src/OverlayWindow.cpp
)

target_compile_features(XVatsimStep5OverlayVisualProofRaster PUBLIC cxx_std_17)
target_compile_definitions(XVatsimStep5OverlayVisualProofRaster PRIVATE
    XVATSIM_STEP3_OFFLINE_VISUAL_PROOF=1
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_include_directories(XVatsimStep5OverlayVisualProofRaster PUBLIC
    ${CMAKE_SOURCE_DIR}/modules/overlay/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
target_include_directories(XVatsimStep5OverlayVisualProofRaster PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)
target_link_libraries(XVatsimStep5OverlayVisualProofRaster PUBLIC
    XVatsimOverlayAccessoryCore
    XVatsimBrain
    gdiplus
)

target_compile_features(XVatsimStep5AtisVisualProof PRIVATE cxx_std_17)

target_compile_definitions(XVatsimStep5AtisVisualProof PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(XVatsimStep5AtisVisualProof PRIVATE
    ${CMAKE_SOURCE_DIR}/brain/include
    ${CMAKE_SOURCE_DIR}/modules/overlay/include
    ${CMAKE_SOURCE_DIR}/modules/vatsim_data_feed/include
)

target_link_libraries(XVatsimStep5AtisVisualProof PRIVATE
    XVatsimBrain
    XVatsimOverlayAccessoryCore
    XVatsimStep5OverlayVisualProofRaster
    XVatsimVatsimDataFeed
    bcrypt
    gdiplus
)

set_target_properties(XVatsimStep5AtisVisualProof PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
)
