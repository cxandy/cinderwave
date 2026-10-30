cmake_minimum_required(VERSION 3.13)

# Pull in the Raspberry Pi Pico SDK (must be located before project()).
include(pico_sdk_import.cmake)

project(cinderwave_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(cinderwave_firmware
    main.cpp
    # Hardware-facing modules for this board (see pins.h for the pin map).
    audio.cpp
    display.cpp
    keypad.cpp
    # The platform-independent DSP core, compiled straight into the firmware.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oscillator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/envelope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/filter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/voice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/sequencer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/synth.cpp
)

target_include_directories(cinderwave_firmware PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(cinderwave_firmware
    pico_stdlib
    hardware_pwm
    hardware_adc
    hardware_irq
    hardware_spi
    hardware_gpio
    hardware_timer
)

# Enable USB serial for debug logging; disable UART.
pico_enable_stdio_usb(cinderwave_firmware 1)
pico_enable_stdio_uart(cinderwave_firmware 0)

# Produce .uf2 / .hex / .bin / .map alongside the .elf.
pico_add_extra_outputs(cinderwave_firmware)
