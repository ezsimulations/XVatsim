add_library(XVatsimWorkflowCore STATIC
    src/ControllerAuthority.cpp
    src/MapDataSource.cpp
    src/RouteGrammar.cpp
    src/RouteResolution.cpp
    src/RouteTraversal.cpp
    src/WorkflowEngine.cpp
)

target_compile_features(XVatsimWorkflowCore PUBLIC cxx_std_17)

target_include_directories(XVatsimWorkflowCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(XVatsimWorkflowCore PUBLIC
    XVatsimBrain
)
