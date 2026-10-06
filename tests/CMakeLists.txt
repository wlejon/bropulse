cmake_minimum_required(VERSION 3.24)

set(TEST_TARGETS
    test_stream_graph
    test_volume
    test_policy
    test_pipewire
    test_pulse
    test_router
)

foreach(t ${TEST_TARGETS})
    add_executable(${t} ${t}.cpp)
    bropulse_warnings(${t})
    target_link_libraries(${t} PRIVATE bropulse::bropulse)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
