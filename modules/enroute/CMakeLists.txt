add_library(XVatsimEnroute STATIC
    src/EnrouteModule.cpp
)

target_compile_features(XVatsimEnroute PUBLIC cxx_std_17)

target_include_directories(XVatsimEnroute PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
