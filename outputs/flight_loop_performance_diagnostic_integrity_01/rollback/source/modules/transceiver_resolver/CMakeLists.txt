add_library(XVatsimTransceiverResolver STATIC
    src/TransceiverResolver.cpp
)

target_compile_features(XVatsimTransceiverResolver PUBLIC cxx_std_17)

target_include_directories(XVatsimTransceiverResolver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_include_directories(XVatsimTransceiverResolver PRIVATE
    "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
)

target_compile_definitions(XVatsimTransceiverResolver PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(XVatsimTransceiverResolver PUBLIC
    winhttp
    windowsapp
)
