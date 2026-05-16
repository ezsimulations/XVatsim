add_library(XVatsimVatsimDataFeed STATIC
    src/VatsimDataFeedClient.cpp
)

target_compile_features(XVatsimVatsimDataFeed PUBLIC cxx_std_17)

target_include_directories(XVatsimVatsimDataFeed PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_include_directories(XVatsimVatsimDataFeed PRIVATE
    "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
)

target_compile_definitions(XVatsimVatsimDataFeed PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(XVatsimVatsimDataFeed PUBLIC
    winhttp
    windowsapp
)
