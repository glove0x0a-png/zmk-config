#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zmk/keyboard.h>

static const uint8_t row_pins[] = { P1_05, P1_14, P1_07, P1_15 };
static const uint8_t col_pins[] = {
    P1_13, P1_03, P1_12, P0_10, P0_09, P0_29,
    P1_01, P0_28, P0_19, P0_03, P0_15, P0_02
};

struct keyboard_config config = {
    .row_pins = row_pins,
    .col_pins = col_pins,
    .num_rows = ARRAY_SIZE(row_pins),
    .num_cols = ARRAY_SIZE(col_pins),
};
