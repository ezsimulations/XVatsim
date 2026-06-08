add_library(XVatsimUpdateChecker STATIC
    src/UpdateChecker.cpp
)

target_compile_features(XVatsimUpdateChecker PUBLIC cxx_std_17)

target_include_directories(XVatsimUpdateChecker PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(XVatsimUpdateChecker PRIVATE
    "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
)

target_compile_definitions(XVatsimUpdateChecker PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(XVatsimUpdateChecker PUBLIC
    winhttp
    windowsapp
)
