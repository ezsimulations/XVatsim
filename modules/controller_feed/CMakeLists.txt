add_library(XVatsimControllerFeed STATIC
    src/ControllerFeedClient.cpp
)

target_compile_features(XVatsimControllerFeed PUBLIC cxx_std_17)

target_include_directories(XVatsimControllerFeed PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/brain/include
)

target_compile_definitions(XVatsimControllerFeed PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(XVatsimControllerFeed PUBLIC
    XVatsimVatsimDataFeed
)
