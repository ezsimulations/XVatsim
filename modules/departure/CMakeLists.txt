add_library(XVatsimHarnessLegacyDeparture STATIC
    src/DepartureModule.cpp
)

target_compile_features(XVatsimHarnessLegacyDeparture PUBLIC cxx_std_17)

target_compile_definitions(XVatsimHarnessLegacyDeparture PUBLIC
    XVATSIM_ENABLE_HARNESS_LEGACY_BOARD_MODULES
)

target_include_directories(XVatsimHarnessLegacyDeparture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_link_libraries(XVatsimHarnessLegacyDeparture PUBLIC
    XVatsimCtafLookup
)
