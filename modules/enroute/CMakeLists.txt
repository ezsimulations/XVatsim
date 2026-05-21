add_library(XVatsimHarnessLegacyEnroute STATIC
    src/EnrouteModule.cpp
)

target_compile_features(XVatsimHarnessLegacyEnroute PUBLIC cxx_std_17)

target_compile_definitions(XVatsimHarnessLegacyEnroute PUBLIC
    XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
)

target_include_directories(XVatsimHarnessLegacyEnroute PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
