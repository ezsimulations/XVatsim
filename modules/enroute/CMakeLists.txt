add_library(XVatsimHarnessLegacyEnroute STATIC
    src/EnrouteModule.cpp
)

target_compile_features(XVatsimHarnessLegacyEnroute PUBLIC cxx_std_17)

target_include_directories(XVatsimHarnessLegacyEnroute PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
