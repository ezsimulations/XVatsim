add_library(XVatsimRuntimeWorkers STATIC
    src/AsyncFactWorkerHost.cpp
)

target_compile_features(XVatsimRuntimeWorkers PUBLIC cxx_std_17)

target_include_directories(XVatsimRuntimeWorkers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_link_libraries(XVatsimRuntimeWorkers PUBLIC
    XVatsimBrain
    XVatsimMetar
)
