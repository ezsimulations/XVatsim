add_library(XVatsimHarnessLegacyArrival STATIC
    src/ArrivalAirspaceModule.cpp
    src/ArrivalLocalModule.cpp
    src/ArrivalModule.cpp
)

target_compile_features(XVatsimHarnessLegacyArrival PUBLIC cxx_std_17)

target_compile_definitions(XVatsimHarnessLegacyArrival PUBLIC
    XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
)

target_include_directories(XVatsimHarnessLegacyArrival PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_link_libraries(XVatsimHarnessLegacyArrival PUBLIC
    XVatsimCtafLookup
)
