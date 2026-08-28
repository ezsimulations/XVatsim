add_library(XVatsimOverlay STATIC
    src/OverlayWindow.cpp
)

add_library(XVatsimOverlayAccessoryCore STATIC
    src/OverlayAccessoryCore.cpp
)

target_compile_features(XVatsimOverlayAccessoryCore PUBLIC cxx_std_17)

target_include_directories(XVatsimOverlayAccessoryCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

if(WIN32)
    target_compile_definitions(XVatsimOverlayAccessoryCore PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(XVatsimOverlayAccessoryCore PUBLIC gdiplus)
endif()

if((XVATSIM_BUILD_STEP3_VISUAL_PROOF OR XVATSIM_BUILD_STEP4_METAR_VISUAL_PROOF) AND WIN32)
    add_library(XVatsimOverlayVisualProofRaster STATIC
        src/OverlayWindow.cpp
    )
    target_compile_features(XVatsimOverlayVisualProofRaster PUBLIC cxx_std_17)
    target_compile_definitions(XVatsimOverlayVisualProofRaster PRIVATE
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
    target_include_directories(XVatsimOverlayVisualProofRaster PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/brain/include
    )
    target_include_directories(XVatsimOverlayVisualProofRaster PRIVATE
        ${XPLANE_SDK_ROOT}/CHeaders/XPLM
    )
    target_link_libraries(XVatsimOverlayVisualProofRaster PUBLIC
        XVatsimOverlayAccessoryCore
        XVatsimBrain
        gdiplus
    )
endif()

target_compile_features(XVatsimOverlay PUBLIC cxx_std_17)

target_link_libraries(XVatsimOverlay PUBLIC
    XVatsimOverlayAccessoryCore
)

target_include_directories(XVatsimOverlay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_include_directories(XVatsimOverlay PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)

target_compile_definitions(XVatsimOverlay PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
