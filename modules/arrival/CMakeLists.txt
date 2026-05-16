add_library(XVatsimArrival STATIC
    src/ArrivalAirspaceModule.cpp
    src/ArrivalLocalModule.cpp
    src/ArrivalModule.cpp
)

target_compile_features(XVatsimArrival PUBLIC cxx_std_17)

target_include_directories(XVatsimArrival PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_link_libraries(XVatsimArrival PUBLIC
    XVatsimCtafLookup
)
