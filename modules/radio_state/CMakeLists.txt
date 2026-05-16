add_library(XVatsimRadioState STATIC
    src/RadioStateSampler.cpp
)

target_compile_features(XVatsimRadioState PUBLIC cxx_std_17)

target_include_directories(XVatsimRadioState PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_include_directories(XVatsimRadioState PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)

target_compile_definitions(XVatsimRadioState PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
)
