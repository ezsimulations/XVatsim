add_library(XVatsimDeparture STATIC
    src/DepartureModule.cpp
)

target_compile_features(XVatsimDeparture PUBLIC cxx_std_17)

target_include_directories(XVatsimDeparture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_link_libraries(XVatsimDeparture PUBLIC
    XVatsimCtafLookup
)
