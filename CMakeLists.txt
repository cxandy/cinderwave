cmake_minimum_required(VERSION 3.16)
project(cinderwave LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Platform-independent DSP core -----------------------------------------
add_library(cinderwave_core STATIC
    src/oscillator.cpp
    src/envelope.cpp
    src/filter.cpp
    src/voice.cpp
    src/sequencer.cpp
    src/synth.cpp
)
target_include_directories(cinderwave_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---- Host renderer: bakes a demo pattern to a .wav -------------------------
add_executable(cinderwave_render host/render.cpp)
target_link_libraries(cinderwave_render PRIVATE cinderwave_core)

# ---- Tests -----------------------------------------------------------------
enable_testing()
add_executable(cinderwave_tests
    tests/test_main.cpp
    tests/test_oscillator.cpp
    tests/test_envelope.cpp
    tests/test_filter.cpp
    tests/test_sequencer.cpp
    tests/test_voice.cpp
)
target_include_directories(cinderwave_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(cinderwave_tests PRIVATE cinderwave_core)
add_test(NAME cinderwave_tests COMMAND cinderwave_tests)
