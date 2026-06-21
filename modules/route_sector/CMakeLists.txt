add_library(XVatsimRouteSector STATIC
    src/RouteSectorResolver.cpp
)

target_compile_features(XVatsimRouteSector PUBLIC cxx_std_17)

target_include_directories(XVatsimRouteSector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_include_directories(XVatsimRouteSector PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
    "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
)

target_compile_definitions(XVatsimRouteSector PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(XVatsimRouteSector PUBLIC
    XVatsimBrain
    XVatsimWorkflowCore
    winhttp
    windowsapp
)
