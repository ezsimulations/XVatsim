add_library(XVatsimTerminalAuthority STATIC
    src/TerminalAuthorityResolver.cpp
)

target_compile_features(XVatsimTerminalAuthority PUBLIC cxx_std_17)

target_include_directories(XVatsimTerminalAuthority PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_include_directories(XVatsimTerminalAuthority PRIVATE
    "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
)

target_compile_definitions(XVatsimTerminalAuthority PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(XVatsimTerminalAuthority PUBLIC
    XVatsimWorkflowCore
    winhttp
    windowsapp
)
