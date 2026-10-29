cmake_minimum_required(VERSION 3.21..3.31)
project(psyz_tests C)

add_subdirectory(../../ztest ztest)
add_subdirectory(../ build)

set(PSYZ_TEST_SOURCES
    test_main.c
    test_libapi.c
    test_events.c
    test_libgte.c
    test_libgpu.c
    test_libcd.c
    test_libspu.c
    test_mdec.c
    frontbuffer_psyz.c
)

add_executable(${PROJECT_NAME} ${PSYZ_TEST_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(${PROJECT_NAME} PRIVATE ztest psyz)
if(NOT PSP AND PSYZ_RENDERER STREQUAL "sdl3_gpu")
    target_compile_definitions(${PROJECT_NAME} PRIVATE PSYZ_TEST_SDL_GPU=1)
endif()
if(IS_PPSSPP_EMU)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IS_PPSSPP_EMU=1)
endif()
psyz_title(${PROJECT_NAME} "PsyZ Tests")

if(PLATFORM_PSP)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_NAME psyz_tests_psp)
else()
    enable_testing()
    # Standalone fake host clock: exercise long idle deadlines without sleeping.
    add_executable(psyz_clock_tests test_events.c)
    target_compile_definitions(psyz_clock_tests PRIVATE PSYZ_TEST_CLOCK=1 __psyz)
    target_include_directories(psyz_clock_tests PRIVATE ../include)
    set_target_properties(psyz_clock_tests PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON)
    add_test(NAME psyz_clock_tests COMMAND psyz_clock_tests)
    add_test(NAME psyz_tests COMMAND ${PROJECT_NAME}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
