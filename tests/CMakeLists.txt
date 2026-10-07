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

if(BROPULSE_ENABLE_API)
    add_executable(test_api test_api.cpp)
    bropulse_warnings(test_api)
    target_include_directories(test_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/api
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(test_api PRIVATE
        bropulse_api
        bropulse
        bronze_runtime_shared
    )
    if(WIN32)
        target_compile_definitions(test_api PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
        add_custom_command(TARGET test_api POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:test_api>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_LINKER_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:test_api>
        )
    endif()
    add_test(NAME test_api COMMAND test_api)
    set_tests_properties(test_api PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

