add_library(XVatsimPilotIdentity STATIC
    src/PilotIdentityResolver.cpp
)

target_compile_features(XVatsimPilotIdentity PUBLIC cxx_std_17)

target_include_directories(XVatsimPilotIdentity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)
