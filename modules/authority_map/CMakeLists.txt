add_library(XVatsimAuthorityMap STATIC
    src/CanonicalAuthorityMapCompiler.cpp
    src/CanonicalAuthorityMapStore.cpp
)

target_compile_features(XVatsimAuthorityMap PUBLIC cxx_std_17)

target_include_directories(XVatsimAuthorityMap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(XVatsimAuthorityMap PUBLIC
    XVatsimWorkflowCore
)
