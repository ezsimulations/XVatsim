add_library(XVatsimDiversionContext STATIC
    src/DiversionContextModule.cpp
)

target_compile_features(XVatsimDiversionContext PUBLIC cxx_std_17)

target_include_directories(XVatsimDiversionContext PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)

target_compile_definitions(XVatsimDiversionContext PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

