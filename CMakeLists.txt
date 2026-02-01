# Add include path for analog input driver
zephyr_library_include_directories(${ZMK_ANALOG_INPUT_DRIVER_DIR}/include)

# Add driver source
add_subdirectory(${ZMK_ANALOG_INPUT_DRIVER_DIR} zmk-analog-input-driver)
