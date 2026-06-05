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
//#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

//define
#define NOR_POLL_MS   K_MSEC(20)     // 通常時ポーリング間隔
#define BLE_POLL_MS   K_MSEC(1000)   // 省電力時ポーリング間隔
#define JIG_POLL_MS   K_MSEC(230000) // ジグラー間隔(ms)  (4分 - 待機時間10秒)
#define JIG_WAIT_MS   229*1000       // ジグラー閾値(ms) = JIG_POLL_MS - ちょっと。


#define BLE_SLEEP_MS    5*1000 // BLE時の未入力待ち時間(ms)
#define IDLE_MS        10*1000 // 待機突入時間
#define DEEP_SLEEP_MS 180*1000 // 完全スリープ。


#define MOUSE_VAL_X     18   // マウス移動量
#define MOUSE_VAL_MAX_X 54   // X最大
#define MOUSE_VAL_Y     12   // マウス移動量
#define MOUSE_VAL_MAX_Y 36   // Y最大
#define ACCEL_VAL     1.2    // 加速度加算倍率
#define ACCEL_CANCEL_MS  500 // 前回移動量の無効化時間(ms)

//struct
struct zmk_behavior_binding binding = {
    .behavior_dev = "key_press",
    .param1 = 0x0D,  //HID_USAGE_KEY_J
    .param2 = 0,
};

int  direction = -1;
extern volatile bool zmk_sleep_inhibit; //スリープ抑止フラグ

///////////////////////////////////////////////////////////////////////////
// #01.心臓部
void az1uball_read_data_work(struct k_work *work)
{
    uint8_t buf[5]; //buf
    uint32_t now = k_uptime_get(); //実行時刻
    struct az1uball_data *data = CONTAINER_OF(work, struct az1uball_data, work); //az1uball
    data->layer = zmk_keymap_highest_layer_active(); //現レイヤ
    zmk_sleep_inhibit = ( data->layer == 1 ); //レイヤー1ならスリープ抑止

    const struct az1uball_config *config = data->dev->config; //config
    float scaling = data->scaling_factor; //比率

    bool lshift_pressed = zmk_hid_get_explicit_mods() & 0x02;  //左Shift
    if ( data->First_flg ) //描画命令があれば画面描画
    {
        direction *= -1;
        for (int i = 0; i < 4; i++) {
            input_report_rel(data->dev, INPUT_REL_Y, direction, true , K_NO_WAIT);
            k_sleep(K_MSEC( 2));
        }
    }
    data->First_flg = false; //描画ロジッククリア

    struct zmk_behavior_binding_event event = { .position = 0,.timestamp = now,.layer = 0,}; //event

    //i2c_read
    i2c_read_dt(&config->i2c, buf, sizeof(buf));

    float delta_x=0,delta_y=0; //移動距離(誤作動防止のためDED_ZONE考慮)
    if     ( abs((int16_t)buf[1]) > abs(buf[0])) delta_x= MOUSE_VAL_X; //buf[1]=右:指の向きで接点が短いので感度2倍
    else if( abs((int16_t)buf[0]) > abs(buf[1])) delta_x=-MOUSE_VAL_X; //buf[0]=左:同上
    if     ( abs((int16_t)buf[3]) > abs(buf[2])) delta_y= MOUSE_VAL_Y; //buf[3]=下
    else if( abs((int16_t)buf[2]) > abs(buf[3])) delta_y=-MOUSE_VAL_Y; //buf[2]=上

    bool  btn_push  = (buf[4] & MSK_SWITCH_STATE) != 0; //true:押下、false:未押下
    if ( now - data->last_activity_time < ACCEL_CANCEL_MS ){ //加速度加算・最後の操作あり
      if(( data->pre_x > 0 && delta_x > 0 ) || ( data->pre_x < 0 && delta_x < 0 )) delta_x = data->pre_x * ACCEL_VAL;
      if(( data->pre_y > 0 && delta_y > 0 ) || ( data->pre_y < 0 && delta_y < 0 )) delta_y = data->pre_y * ACCEL_VAL;
    } else {
        data->pre_x=0; //前回移動量初期化
        data->pre_y=0;
    }

    if      ( delta_x > MOUSE_VAL_MAX_X ) delta_x = MOUSE_VAL_MAX_X; //上限制御
    else if ( delta_x <-MOUSE_VAL_MAX_X ) delta_x =-MOUSE_VAL_MAX_X; //上限制御
    if      ( delta_y > MOUSE_VAL_MAX_Y ) delta_y = MOUSE_VAL_MAX_Y; //上限制御
    else if ( delta_y <-MOUSE_VAL_MAX_Y ) delta_y =-MOUSE_VAL_MAX_Y; //上限制御

    if( delta_x != 0 || delta_y != 0 ){
        float mag = sqrt( delta_x*delta_x + delta_y * delta_y);
        delta_x = delta_x * fabsf(delta_x) / mag; //角度計算 cos変換 
        delta_y = delta_y * fabsf(delta_y) / mag; //         sin変換
        data->pre_x=delta_x;//前回移動量保存。
        data->pre_y=delta_y;
    }
    //マウス操作
    if ( delta_x != 0 || delta_y != 0 || btn_push != data->sw_pressed){
        data->last_activity_time = now; //前回操作時間更新
        data->last_jig_time      = now;
    }

    //ボタン押下があれば(レイヤー操作が複雑なのでJのみ)
    if ( btn_push != data->sw_pressed ){
        data->sw_pressed = btn_push;
        if (data->layer == 0 || data->layer == 1 ) { //スクロールレイヤ
            binding.behavior_dev="key_press";
            binding.param1 = 0x0D; 
            zmk_behavior_invoke_binding(&binding, event, btn_push);  //Jキー扱い
        }
    }
    if (data->layer == 2 || data->layer == 4 ) { //スクロールレイヤ
        if (abs(delta_x) > abs(delta_y)) delta_y = 0; else delta_x = 0; //縦 or 横のみ抽出
        binding.behavior_dev="key_press";
        binding.param1 = 0xE0;

        if      (delta_y >  1) {
           if (lshift_pressed ){
               zmk_behavior_invoke_binding(&binding, event, 1);
               input_report_rel(data->dev, INPUT_REL_HWHEEL, 1, true, K_NO_WAIT);
               zmk_behavior_invoke_binding(&binding, event, 0);
           }
           else
               input_report_rel(data->dev, INPUT_REL_WHEEL, -1, true, K_NO_WAIT);
        }
        else if (delta_y < -1) {
           if (lshift_pressed ){
               zmk_behavior_invoke_binding(&binding, event, 1);
               input_report_rel(data->dev, INPUT_REL_HWHEEL,-1, true, K_NO_WAIT);
               zmk_behavior_invoke_binding(&binding, event, 0);
           }
           else
               input_report_rel(data->dev, INPUT_REL_WHEEL,  1, true, K_NO_WAIT);
        }
        else if (delta_x >  1) {
           if (lshift_pressed ){
               zmk_behavior_invoke_binding(&binding, event, 1);
               input_report_rel(data->dev, INPUT_REL_HWHEEL, 1, true, K_NO_WAIT);
               zmk_behavior_invoke_binding(&binding, event, 0);
           }
           else
               input_report_rel(data->dev, INPUT_REL_WHEEL, -1, true, K_NO_WAIT);
        }
        else if (delta_x < -1) {
           if (lshift_pressed ) {
               zmk_behavior_invoke_binding(&binding, event, 1);
               input_report_rel(data->dev, INPUT_REL_HWHEEL,-1, true, K_NO_WAIT);
               zmk_behavior_invoke_binding(&binding, event, 0);
           }
           else
               input_report_rel(data->dev, INPUT_REL_WHEEL,  1, true, K_NO_WAIT);
        }
        return;
    } else if (delta_x != 0 || delta_y != 0) { //マウス処理
        scaling /= 3.0f; //原則:低速
        if (data->layer == 3 || data->layer == 5 )     scaling      *= 6.0f; //レイヤー:高速
        for (int i = 0; i < 3; i++) { //移動を滑らかに
            input_report_rel(data->dev, INPUT_REL_X, delta_x / 3 * scaling, false, K_NO_WAIT);
            input_report_rel(data->dev, INPUT_REL_Y, delta_y / 3 * scaling, true , K_NO_WAIT);
        }
    }

    //★ジグラーレイヤー:ジグラー操作(Layer1なら、4分ごとに必ず実行・独立)
    if ( data->layer == 1 && now - data->last_jig_time >= JIG_WAIT_MS ) {
        data->last_jig_time = k_uptime_get();
        direction *= -1;
        input_report_rel(data->dev, INPUT_REL_X, direction * 10, true, K_NO_WAIT);
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

    //高サイクル:USB接続
    if ( zmk_usb_is_powered() ){
        k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);
    }
    //高サイクル:キー操作・マウス操作から一定時間以内
    else if ( k_uptime_get() - data->last_activity_time <= BLE_SLEEP_MS ) {
        k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);
    }
    //完全停止：キー割込みでのみ復帰。…多分無意味。ポーリングを生かしてジグラーだけ動作させても、deep sleepが優先された。
    else if (data->layer != 1 && k_uptime_get() - data->last_activity_time >= DEEP_SLEEP_MS){
        ;
    }
    //idle時間経過で、超低速ポーリング。※キー操作・マウス操作なしで30秒経過
    else if (k_uptime_get() - data->last_activity_time >= IDLE_MS){
        k_timer_start(&data->polling_timer, JIG_POLL_MS, JIG_POLL_MS);
    }
    //
    //低サイクル:else 上記以外(BLE接続、キー・マウス操作から5秒～30秒以内)
    else{
        k_timer_start(&data->polling_timer, BLE_POLL_MS, BLE_POLL_MS);
    }
    k_work_submit(&data->work);
    //last_jig_timeは、更新しない。でないと1回ジグラーが動くごとに、復帰してしまう。(JIG_POLL_MSの継続)
    return;
}

///////////////////////////////////////////////////////////////////////////
// #03-XX.初期処理 / 感度取得.
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
    data->last_jig_time = k_uptime_get();
    data->scaling_factor = parse_sensitivity(config->sensitivity);
    data->pre_x=0;
    data->pre_y=0;
    data->layer=0;
    data->First_flg=false;

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
extern struct az1uball_data az1uball_data_0;

///////////////////////////////////////////////////////////////////////////
// #21.割込み検知
static int az1uball_event_handler(const zmk_event_t *eh)
{
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev) {
        return 0;
    }
    // 押下時のみ処理
    if (!ev->state) {
        return 0;
    }

    struct az1uball_data *data = &az1uball_data_0;

    bool rctrl = zmk_hid_get_explicit_mods() & 0x10;
    bool lgui  = zmk_hid_get_explicit_mods() & 0x08;
    bool is_ESC = (ev->usage_page == 0x07 && ev->keycode    == 0x29);    //USAGE_PAGE_KEYBOARD 0x07,KEY_ESC 0x29

    /* ① 右Ctrl or 左GUI */
    if (rctrl || lgui) {
        data->First_flg = true;                     //押されたら描画フラグON -> 次のポーリングで描画・サイクル変更なし
    }
    /* ② ESC 押下  */
    else if (is_ESC) {
        data->last_activity_time = k_uptime_get();  //★時間更新する -> 次のポーリングでサイクルは高頻度へリセット。
    }

    k_timer_stop(&data->polling_timer);
    k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);  //1回はポーリング起動(この時点でジグラー間隔をリセット)

    return 0;
}

ZMK_LISTENER(az1uball, az1uball_event_handler);
ZMK_SUBSCRIPTION(az1uball, zmk_keycode_state_changed);