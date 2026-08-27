cmake_minimum_required(VERSION 3.22)

add_executable(XVatsimStep3VisualProof
    src/main.cpp
)

target_compile_features(XVatsimStep3VisualProof PRIVATE cxx_std_17)

target_compile_definitions(XVatsimStep3VisualProof PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(XVatsimStep3VisualProof PRIVATE
    ${CMAKE_SOURCE_DIR}/brain/include
    ${CMAKE_SOURCE_DIR}/modules/overlay/include
)

target_link_libraries(XVatsimStep3VisualProof PRIVATE
    XVatsimBrain
    XVatsimOverlayAccessoryCore
    XVatsimOverlayVisualProofRaster
    bcrypt
    gdiplus
)

set_target_properties(XVatsimStep3VisualProof PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
)
