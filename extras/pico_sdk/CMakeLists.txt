cmake_minimum_required(VERSION 3.13)

if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(half_duplex_read_write C CXX ASM)
pico_sdk_init()

add_subdirectory(../.. rp2040pio_halfduplex)

add_executable(half_duplex_read_write main.cpp)
target_link_libraries(half_duplex_read_write pico_stdlib rp2040pio_halfduplex)
pico_enable_stdio_usb(half_duplex_read_write 1)
pico_add_extra_outputs(half_duplex_read_write)
