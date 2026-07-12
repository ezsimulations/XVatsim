cmake_minimum_required(VERSION 3.22)
project(XVatsim VERSION 1.2.2 LANGUAGES C CXX)

add_subdirectory(brain)
add_subdirectory(core)

set(XPLANE_SDK_ROOT "${CMAKE_SOURCE_DIR}/SDK" CACHE PATH "Path to the X-Plane SDK root")

option(XVATSIM_BUILD_PLUGIN "Build the X-Plane plugin" ON)
option(XVATSIM_BUILD_REGRESSION_HARNESS "Build the offline regression harness" ON)
option(XVATSIM_BUILD_PREFLIGHT_BUILDER "Build the XVatsim Preflight Builder desktop app" ON)
option(XVATSIM_ENABLE_CONTROLLER_MESSAGES "Enable experimental controller/private message overlay UI" OFF)

if (XVATSIM_BUILD_PLUGIN OR XVATSIM_BUILD_REGRESSION_HARNESS)
    add_subdirectory(modules)
endif()

if (XVATSIM_BUILD_PLUGIN)
    if (NOT EXISTS "${XPLANE_SDK_ROOT}/CHeaders/XPLM/XPLMPlugin.h")
        message(FATAL_ERROR "XPLANE_SDK_ROOT must point to a valid X-Plane SDK root.")
    endif()

    add_subdirectory(plugin)
endif()

if (XVATSIM_BUILD_REGRESSION_HARNESS)
    add_subdirectory(tools/regression_harness)
endif()

if (XVATSIM_BUILD_PREFLIGHT_BUILDER AND WIN32)
    add_subdirectory(tools/preflight_builder)
endif()
