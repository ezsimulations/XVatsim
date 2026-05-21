add_executable(XVatsimPreflightBuilder WIN32
    src/main.cpp
)

target_compile_features(XVatsimPreflightBuilder PRIVATE cxx_std_17)

target_link_libraries(XVatsimPreflightBuilder PRIVATE
    XVatsimWorkflowCore
    comctl32
)

if (MSVC)
    target_compile_definitions(XVatsimPreflightBuilder PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(XVatsimPreflightBuilder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/dist/XVatsim/win_x64"
)
