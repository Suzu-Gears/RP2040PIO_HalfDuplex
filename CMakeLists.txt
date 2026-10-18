# Pico SDK: after pico_sdk_init(),
#   add_subdirectory(path/to/RP2040PIO_HalfDuplex rp2040pio_halfduplex)
#   target_link_libraries(your_app rp2040pio_halfduplex)
add_library(rp2040pio_halfduplex INTERFACE)
target_sources(rp2040pio_halfduplex INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/RP2040PIO_HalfDuplex.cpp)
target_include_directories(rp2040pio_halfduplex INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_features(rp2040pio_halfduplex INTERFACE cxx_std_17)
target_link_libraries(rp2040pio_halfduplex INTERFACE hardware_pio hardware_dma hardware_irq hardware_clocks)
