add_library(XVatsimSettingsStore STATIC
    src/SettingsStore.cpp
)

target_compile_features(XVatsimSettingsStore PUBLIC cxx_std_17)

target_include_directories(XVatsimSettingsStore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
