add_library(XVatsimAirportFrequencyCatalog STATIC
    src/AirportFrequencyCatalogResolver.cpp
)

target_compile_features(XVatsimAirportFrequencyCatalog PUBLIC cxx_std_17)

target_include_directories(XVatsimAirportFrequencyCatalog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
