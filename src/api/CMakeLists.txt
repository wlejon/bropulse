add_library(bropulse_api STATIC
    api.cpp
    native_pulse.cpp
)

target_include_directories(bropulse_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(bropulse_api PUBLIC
    bropulse
    bronze_runtime_shared
)

target_compile_features(bropulse_api PUBLIC cxx_std_20)

bropulse_warnings(bropulse_api)
