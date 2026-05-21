add_library(XVatsimBrain STATIC
    src/BrainOrchestrator.cpp
    src/BrainControllerRelevanceWorker.cpp
    src/BrainDisplayIntent.cpp
    src/BrainOwnedRuntime.cpp
    src/BrainWorkflow.cpp
    src/BrainRadioRangeWorker.cpp
    src/BrainRoutePolygonWorker.cpp
    src/PhaseSnapshotPublisher.cpp
    src/RadioReachableSnapshot.cpp
    src/RoutePolygonTransition.cpp
    src/BrainWorkModel.cpp
    src/BrainWorkScheduler.cpp
)

target_compile_features(XVatsimBrain PUBLIC cxx_std_17)

target_include_directories(XVatsimBrain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
