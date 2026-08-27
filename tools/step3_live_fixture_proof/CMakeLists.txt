cmake_minimum_required(VERSION 3.22)

add_executable(XVatsimStep3LiveFixtureProof
    src/main.cpp
)

target_compile_features(XVatsimStep3LiveFixtureProof PRIVATE cxx_std_17)

target_link_libraries(XVatsimStep3LiveFixtureProof PRIVATE
    XVatsimStep3LiveProofFixtures
)

set_target_properties(XVatsimStep3LiveFixtureProof PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
)
