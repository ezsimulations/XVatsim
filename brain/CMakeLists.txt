add_library(XVatsimBrain STATIC
    src/BrainOrchestrator.cpp
)

target_compile_features(XVatsimBrain PUBLIC cxx_std_17)

target_include_directories(XVatsimBrain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
