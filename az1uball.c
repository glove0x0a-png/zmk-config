#define DT_DRV_COMPAT zmk_az1uball
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <math.h>
#include <stdlib.h>
#include <zmk/ble.h> // 追加
#include <zmk/usb.h>
#include <zmk/hid.h>    // HID usage定義用
#include "az1uball.h"

//追加
#include <zmk/event_manager.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

//define
#define NORMAL_POLL_INTERVAL K_MSEC(10)       // 通常時: 10ms (100Hz)
#define LOW_POWER_POLL_INTERVAL K_MSEC(500)   // 省電力時:  500ms (2Hz)
#define NON_ACTIVE_POLL_INTERVAL K_MSEC(2000) // 省電力時:  2000ms (0.5Hz)
#define LOW_POWER_TIMEOUT_MS 5000             // 5秒間入力がないと省電力モードへ

#define JIGGLE_INTERVAL_MS 180*1000           // ジグラー間隔
#define JIGGLE_DELTA_X 1                      // X方向にnピクセル分動かす


//struct
struct zmk_behavior_binding binding = {
    .behavior_dev = "key_press",
    .param1 = 0x0D,  //HID_USAGE_KEY_J
    .param2 = 0,
};

//prototype
static int az1uball_init(const struct device *dev);					//初期化処理
static float parse_sensitivity(const char *sensitivity);			//プロパティからマウス精度を変更
void az1uball_read_data_work(struct k_work *work);					//i2c_read_dtあり。I2C通信でデータ取り出し。
static void az1uball_polling(struct k_timer *timer);

///////////////////////////////////////////////////////////////////////////
/* 01.Initialization of AZ1UBALL */
///////////////////////////////////////////////////////////////////////////
static int az1uball_init(const struct device *dev)
{
    struct       az1uball_data   *data   = dev->data;
    const struct az1uball_config *config = dev->config;
    // コンフィグ取得

    uint8_t cmd = 0x91;

    data->dev = dev;
    data->sw_pressed = false;
    data->last_activity_time = k_uptime_get();
    data->scaling_factor = parse_sensitivity(config->sensitivity);

    device_is_ready(config->i2c.bus);
    i2c_write_dt(&config->i2c, &cmd, sizeof(cmd));


    //サイクル：NORMAL_POLL_INTERVAL
    k_work_init(&data->work          , az1uball_read_data_work);
    k_timer_init( &data->polling_timer, az1uball_polling        , NULL);
    k_timer_start(&data->polling_timer, NORMAL_POLL_INTERVAL, NORMAL_POLL_INTERVAL);
    return 0;
}
// 01.1 感度取得
static float parse_sensitivity(const char *sensitivity) {
    float value;
    char *endptr;
    
    value = strtof(sensitivity, &endptr);
    if (endptr == sensitivity || (*endptr != 'x' && *endptr != 'X')) {
        return 1.0f; // デフォルト値
    }
    return value;
}

///////////////////////////////////////////////////////////////////////////
// 02.心臓部 az1uball_read_data_work
///////////////////////////////////////////////////////////////////////////
void az1uball_read_data_work(struct k_work *work)
{
    //buf
    uint8_t buf[5];
    //実行時刻
    uint32_t now = k_uptime_get();
    //az1uball
    struct az1uball_data *data = CONTAINER_OF(work, struct az1uball_data, work);
    //config
    const struct az1uball_config *config = data->dev->config;
    //event
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = now,
        .layer = 0,
    };
    //scall
    float scaling = data->scaling_factor;

    //read
    i2c_read_dt(&config->i2c, buf, sizeof(buf));

    int16_t delta_x,delta_y;
    //移動距離(誤作動防止)
    if( abs((int16_t)buf[1]) > abs(buf[0])) delta_x =    (int16_t)buf[1] * 9;
    else                                    delta_x = -1*(int16_t)buf[0] * 9;
    if( abs((int16_t)buf[3]) > abs(buf[2])) delta_y =    (int16_t)buf[3] * 9;
    else                                    delta_y = -1*(int16_t)buf[2] * 9;
    bool  btn_push  = (buf[4] & MSK_SWITCH_STATE) != 0;

    //現レイヤ
    int layer = zmk_keymap_highest_layer_active();
    
    //shift押下状態取得
    bool lshift_pressed = zmk_hid_get_explicit_mods() & 0x02;  //左Shift

    //マウス操作 or レイヤー操作 or 修飾キー or ボタン状態変化
    if (    delta_x != 0
         || delta_y != 0
         || lshift_pressed 
         || btn_push != data->sw_pressed){
        data->last_activity_time = now;
    }

    //カーソルレイヤー
    if (layer == 2) {
        if (abs(delta_x) > abs(delta_y)) delta_y = 0;
        else delta_x = 0;
        if (delta_y > 1) {
            for(int i=0;i < delta_y;i++){
              input_report_rel(data->dev, INPUT_REL_WHEEL, -1, true, K_NO_WAIT);
            }
            return;
        } else if (delta_y < -1) {
            for(int i=0;i < -1 * delta_y;i++){
                input_report_rel(data->dev, INPUT_REL_WHEEL, 1, true, K_NO_WAIT);
            }
            return;
        } else if (delta_x > 1) {
            for(int i=0;i < delta_x;i++){
                input_report_rel(data->dev, INPUT_REL_HWHEEL, 1, true, K_NO_WAIT);
            }
            return;
        } else if (delta_x < -1) {
            for(int i=0;i < -1 * delta_x;i++){
                input_report_rel(data->dev, INPUT_REL_HWHEEL, -1, true, K_NO_WAIT);
            }
            return;
        }
    // 通常のマウス処理（レイヤー0など）
    } else if (delta_x != 0 || delta_y != 0) {
        //高速モードレイヤー
        if (layer == 3) scaling /= 3.0f;   //1/3へ
        // 動的倍率変更
        if (lshift_pressed ){
            scaling /= 9.0f;   //shift 1/9倍
        }
        for (int i = 0; i < 3; i++) {
            if (delta_x != 0) input_report_rel(data->dev, INPUT_REL_X, delta_x / 3 * scaling, false, K_NO_WAIT);
            if (delta_y != 0) input_report_rel(data->dev, INPUT_REL_Y, delta_y / 3 * scaling, true, K_NO_WAIT);
        }
    }
    //ボタン押下があれば(レイヤー操作が複雑なのでJのみ)
    if ( btn_push != data->sw_pressed) {
        data->sw_pressed = btn_push;
        binding.param1 = 0x0D; 
        zmk_behavior_invoke_binding(&binding, event, data->sw_pressed);  //Jキー扱い
    }

    // ジグラーレイヤーのみ、ジグラー操作
    if ( now - data->last_activity_time >= JIGGLE_INTERVAL_MS
         && layer == 1 ) {
        data->last_activity_time = now;
        input_report_rel(data->dev, INPUT_REL_X, JIGGLE_DELTA_X, true, K_NO_WAIT);
        k_sleep(K_MSEC(10));
        input_report_rel(data->dev, INPUT_REL_X, -JIGGLE_DELTA_X, true, K_NO_WAIT);
    }
    return;
}
///////////////////////////////////////////////////////////////////////////
// 03.polling
///////////////////////////////////////////////////////////////////////////
static void az1uball_polling(struct k_timer *timer)
{
    struct az1uball_data *data = CONTAINER_OF(timer, struct az1uball_data, polling_timer);

    //サイクルセット
    k_timer_stop( &data->polling_timer);
    //BLE未接続 & USB無接続
    if ( !zmk_ble_active_profile_is_connected() && !zmk_usb_is_powered() ) {
        k_timer_start(&data->polling_timer, NON_ACTIVE_POLL_INTERVAL, NON_ACTIVE_POLL_INTERVAL);
    //最後の操作から5秒経過
    } else if (k_uptime_get() - data->last_activity_time > LOW_POWER_TIMEOUT_MS) {
        k_timer_start(&data->polling_timer, LOW_POWER_POLL_INTERVAL, LOW_POWER_POLL_INTERVAL);
    //操作がある状態
    } else {
        k_timer_start(&data->polling_timer, NORMAL_POLL_INTERVAL, NORMAL_POLL_INTERVAL);
    }
    k_work_submit(&data->work);
    return;
}
///////////////////////////////////////////////////////////////////////////
// 11.DEFINE
///////////////////////////////////////////////////////////////////////////
#define AZ1UBALL_DEFINE(n)                                             \
    static struct az1uball_data az1uball_data_##n;                     \
    static const struct az1uball_config az1uball_config_##n = {        \
        .i2c = I2C_DT_SPEC_INST_GET(n),                                \
        .default_mode = DT_INST_PROP_OR(n, default_mode, "mouse"),     \
        .sensitivity = DT_INST_PROP_OR(n, sensitivity, "1x"),          \
    };                                                                 \
    DEVICE_DT_INST_DEFINE(n,                                           \
                          az1uball_init,                               \
                          NULL,                                        \
                          &az1uball_data_##n,                          \
                          &az1uball_config_##n,                        \
                          POST_KERNEL,                                 \
                          CONFIG_INPUT_INIT_PRIORITY,                  \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(AZ1UBALL_DEFINE)
