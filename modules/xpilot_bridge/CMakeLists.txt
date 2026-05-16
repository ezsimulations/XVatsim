add_library(XVatsimXPilotBridge STATIC
    src/XPilotBridge.cpp
)

target_compile_features(XVatsimXPilotBridge PUBLIC cxx_std_17)

target_include_directories(XVatsimXPilotBridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)

target_compile_definitions(XVatsimXPilotBridge PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
)
