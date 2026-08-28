function(configure_xvatsim_metar_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_include_directories(${target_name} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/brain/include
    )
    target_include_directories(${target_name} PRIVATE
        "C:/Program Files (x86)/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
    )
    target_compile_definitions(${target_name} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(${target_name} PUBLIC
        XVatsimBrain
        winhttp
        windowsapp
    )
endfunction()

add_library(XVatsimMetar STATIC src/VatsimMetarClient.cpp)
configure_xvatsim_metar_target(XVatsimMetar)

if (XVATSIM_BUILD_REGRESSION_HARNESS)
    add_library(XVatsimMetarProof STATIC src/VatsimMetarClient.cpp)
    configure_xvatsim_metar_target(XVatsimMetarProof)
    target_compile_definitions(XVatsimMetarProof PUBLIC
        XVATSIM_METAR_PROOF_FIXTURES=1
    )
endif()
