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
#define NOR_POLL_MS   K_MSEC(20)   // 通常時ポーリング間隔
#define BLE_POLL_MS   K_MSEC(1000) // 省電力時ポーリング間隔
#define BLE_SLEEP_MS  5*1000 // BLE時の未入力待ち時間(ms)
#define JIG_WAIT_MS 180*1000 // ジグラー間隔(ms)
#define MOUSE_VAL    15      // マウス移動量
#define ACCEL_VAL     1.2    // 加速度加算倍率
#define ACCEL_CANCEL_MS  500 // 前回移動量の無効化時間(ms)

//struct
struct zmk_behavior_binding binding = {
    .behavior_dev = "key_press",
    .param1 = 0x0D,  //HID_USAGE_KEY_J
    .param2 = 0,
};

///////////////////////////////////////////////////////////////////////////
// #01.心臓部
void az1uball_read_data_work(struct k_work *work)
{
    uint8_t buf[5]; //buf
    uint32_t now = k_uptime_get(); //実行時刻
    struct az1uball_data *data = CONTAINER_OF(work, struct az1uball_data, work); //az1uball
    const struct az1uball_config *config = data->dev->config; //config
    float scaling = data->scaling_factor; //比率
    int layer = zmk_keymap_highest_layer_active(); //現レイヤ
    bool lshift_pressed = zmk_hid_get_explicit_mods() & 0x02;  //左Shift
    struct zmk_behavior_binding_event event = { .position = 0,.timestamp = now,.layer = 0,}; //event

    //i2c_read
    i2c_read_dt(&config->i2c, buf, sizeof(buf));

    float delta_x=0,delta_y=0; //移動距離(誤作動防止のためDED_ZONE考慮)
    if     ( abs((int16_t)buf[1]) > abs(buf[0])) delta_x= MOUSE_VAL*2; //buf[1]=右:指の向きで接点が短いので感度2倍
    else if( abs((int16_t)buf[0]) > abs(buf[1])) delta_x=-MOUSE_VAL*2; //buf[0]=左:同上
    if     ( abs((int16_t)buf[3]) > abs(buf[2])) delta_y= MOUSE_VAL; //buf[3]=下
    else if( abs((int16_t)buf[2]) > abs(buf[3])) delta_y=-MOUSE_VAL; //buf[2]=上
    bool  btn_push  = (buf[4] & MSK_SWITCH_STATE) != 0; //true:押下、false:未押下
    if ( now - data->last_activity_time < ACCEL_CANCEL_MS ){ //加速度加算
      if(( data->pre_x > 0 && delta_x > 0 ) || ( data->pre_x < 0 && delta_x < 0 )) delta_x = data->pre_x * ACCEL_VAL;
      if(( data->pre_y > 0 && delta_y > 0 ) || ( data->pre_y < 0 && delta_y < 0 )) delta_y = data->pre_y * ACCEL_VAL;
    }
    if( delta_x != 0 && delta_y != 0 ){ //角度計算
        delta_x = delta_x * abs(delta_x) / sqrt( delta_x*delta_x + delta_y * delta_y); //cos変換
        delta_y = delta_y * abs(delta_y) / sqrt( delta_x*delta_x + delta_y * delta_y); //sin変換
    }
    data->pre_x=delta_x; //前回移動量保存。
    data->pre_y=delta_y;

    if (    delta_x != 0 || delta_y != 0 //マウス操作 or レイヤー操作 or 修飾キー or ボタン状態変化
         || lshift_pressed 
         || btn_push != data->sw_pressed){
        data->last_activity_time = now; //前回操作時間更新
    }

    //ボタン押下があれば(レイヤー操作が複雑なのでJのみ)
    if ( btn_push != data->sw_pressed) {
        data->sw_pressed = btn_push;
        binding.param1 = 0x0D; 
        zmk_behavior_invoke_binding(&binding, event, data->sw_pressed);  //Jキー扱い
    }

    if (layer == 2) { //スクロールレイヤ
        if (abs(delta_x) > abs(delta_y)) delta_y = 0; else delta_x = 0; //縦 or 横のみ抽出
        if      (delta_y >  1) input_report_rel(data->dev, INPUT_REL_WHEEL, -1, true, K_NO_WAIT);
        else if (delta_y < -1) input_report_rel(data->dev, INPUT_REL_WHEEL,  1, true, K_NO_WAIT);
        else if (delta_x >  1) input_report_rel(data->dev, INPUT_REL_HWHEEL, 1, true, K_NO_WAIT);
        else if (delta_x < -1) input_report_rel(data->dev, INPUT_REL_HWHEEL,-1, true, K_NO_WAIT);
        return;
    } else if (delta_x != 0 || delta_y != 0) { //マウス処理
        if (layer == 3)     scaling *= 3.0f; //レイヤー:高速
        if (lshift_pressed )scaling /= 3.0f; //shift:低速
        for (int i = 0; i < 2; i++) { //移動を滑らかに
            input_report_rel(data->dev, INPUT_REL_X, delta_x / 2 * scaling, false, K_NO_WAIT);
            input_report_rel(data->dev, INPUT_REL_Y, delta_y / 2 * scaling, true , K_NO_WAIT);
        }
    }

    // ジグラーレイヤー:ジグラー操作
    if ( layer == 1 && now - data->last_activity_time >= JIG_WAIT_MS ) {
        data->last_activity_time = now;
        input_report_rel(data->dev, INPUT_REL_X, 1, true, K_NO_WAIT);
        k_sleep(K_MSEC(10));
        input_report_rel(data->dev, INPUT_REL_X, -1, true, K_NO_WAIT);
    }
    return;
}

///////////////////////////////////////////////////////////////////////////
// #02.ポーリング
static void az1uball_polling(struct k_timer *timer)
{
    struct az1uball_data *data = CONTAINER_OF(timer, struct az1uball_data, polling_timer);

    //サイクルセット
    k_timer_stop( &data->polling_timer);
    //高サイクル:USB接続 || 最終操作から一定時間以内
    if ( zmk_usb_is_powered() || (k_uptime_get() - data->last_activity_time <= BLE_SLEEP_MS) ) {
        k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);
    }
    //低サイクル:else
    else{
        k_timer_start(&data->polling_timer, BLE_POLL_MS, BLE_POLL_MS);
    }
    k_work_submit(&data->work);
    return;
}

///////////////////////////////////////////////////////////////////////////
// #03-XX.初期処理 / 感度取得
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
// #03 初期処理
static int az1uball_init(const struct device *dev)
{
    struct       az1uball_data   *data   = dev->data;
    const struct az1uball_config *config = dev->config; // コンフィグ取得
    uint8_t cmd = 0x91;

    data->dev = dev; //構造体セット
    data->sw_pressed = false;
    data->last_activity_time = k_uptime_get();
    data->scaling_factor = parse_sensitivity(config->sensitivity);
    data->pre_x=0;
    data->pre_y=0;

    device_is_ready(config->i2c.bus); //i2c_初期
    i2c_write_dt(&config->i2c, &cmd, sizeof(cmd));


    //サイクル：NOR_POLL_MS
    k_work_init(&data->work          , az1uball_read_data_work);
    k_timer_init( &data->polling_timer, az1uball_polling        , NULL);
    k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);
    return 0;
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
