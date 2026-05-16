add_library(XVatsimOverlay STATIC
    src/OverlayWindow.cpp
)

target_compile_features(XVatsimOverlay PUBLIC cxx_std_17)

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
