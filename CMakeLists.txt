cmake_minimum_required(VERSION 3.22)
project(XVatsim VERSION 1.2.3 LANGUAGES C CXX)

add_subdirectory(brain)
add_subdirectory(core)

set(XPLANE_SDK_ROOT "${CMAKE_SOURCE_DIR}/SDK" CACHE PATH "Path to the X-Plane SDK root")

option(XVATSIM_BUILD_PLUGIN "Build the X-Plane plugin" ON)
option(XVATSIM_BUILD_REGRESSION_HARNESS "Build the offline regression harness" ON)
option(XVATSIM_BUILD_PREFLIGHT_BUILDER "Build the XVatsim Preflight Builder desktop app" ON)
option(XVATSIM_ENABLE_CONTROLLER_MESSAGES "Enable experimental controller/private message overlay UI" OFF)
option(XVATSIM_BUILD_STEP3_VISUAL_PROOF "Build the deterministic Step 3 offline visual exporter" OFF)
option(XVATSIM_BUILD_STEP4_METAR_VISUAL_PROOF "Build the deterministic Step 4 METAR offline visual exporter" OFF)
option(XVATSIM_STEP3_LIVE_PROOF_FIXTURES "Enable synthetic Step 3 live-proof fixture data" OFF)

if (XVATSIM_BUILD_PLUGIN OR XVATSIM_BUILD_REGRESSION_HARNESS OR
    XVATSIM_BUILD_STEP3_VISUAL_PROOF OR XVATSIM_BUILD_STEP4_METAR_VISUAL_PROOF)
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

if (XVATSIM_BUILD_STEP3_VISUAL_PROOF AND WIN32)
    add_subdirectory(tools/step3_visual_proof)
endif()

if (XVATSIM_BUILD_STEP4_METAR_VISUAL_PROOF AND WIN32)
    add_subdirectory(tools/step4_metar_visual_proof)
endif()

if (XVATSIM_STEP3_LIVE_PROOF_FIXTURES AND WIN32)
    add_subdirectory(tools/step3_live_fixture_proof)
endif()

if (XVATSIM_BUILD_PREFLIGHT_BUILDER AND WIN32)
    add_subdirectory(tools/preflight_builder)
endif()
