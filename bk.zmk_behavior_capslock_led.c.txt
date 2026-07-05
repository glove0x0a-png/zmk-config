#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>

#define HID_KBD_LED_CAPS_LOCK 0x02
#define CAPS_LED_NODE DT_ALIAS(led_red)

static const struct gpio_dt_spec caps_led = GPIO_DT_SPEC_GET(CAPS_LED_NODE, gpios);

static int capslock_handler(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool caps = ev->indicators & HID_KBD_LED_CAPS_LOCK;
    gpio_pin_set_dt(&caps_led, caps ? 1 : 0);

    return ZMK_EV_EVENT_HANDLED;
}

// ✅ イベントリスナー登録
ZMK_LISTENER(capslock_led, capslock_handler);

// ✅ イベント購読登録
ZMK_SUBSCRIPTION(capslock_led, zmk_hid_indicators_changed);
