add_library(XVatsimFlightPlan STATIC
    src/FlightPlanSampler.cpp
)

target_compile_features(XVatsimFlightPlan PUBLIC cxx_std_17)

target_include_directories(XVatsimFlightPlan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_include_directories(XVatsimFlightPlan PRIVATE
    ${XPLANE_SDK_ROOT}/CHeaders/XPLM
)

target_compile_definitions(XVatsimFlightPlan PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
)
