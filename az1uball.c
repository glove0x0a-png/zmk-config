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
//#include <zmk/pm.h>

//追加
#include <zmk/events/position_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

//define
#define NOR_POLL_MS   K_MSEC(20)   // 通常時ポーリング間隔
//#define BLE_POLL_MS   K_MSEC(1000) // 省電力時ポーリング間隔
//#define BLE_SLEEP_MS  5*1000 // BLE時の未入力待ち時間(ms)
#define JIG_WAIT_MS K_MSEC(240000) // ジグラー間隔(ms)
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

bool First_flg = false;
int  direction = -1;
//bool GUI_flg = false;

//extern struct az1uball_data az1uball_data_0;

//void zmk_pm_disable(void);
//void zmk_pm_enable(void);

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
                                //    if( layer == 4 ){//        if (!First_flg )//        {//            First_flg = true;//            for (int i = 0; i < 30; i++) input_report_rel(data->dev, INPUT_REL_Y,-1, true , K_NO_WAIT);//        }//    } else First_flg = false;
    bool lshift_pressed = zmk_hid_get_explicit_mods() & 0x02;  //左Shift
    bool rCtrl_pressed  = zmk_hid_get_explicit_mods() & 0x10;  //右Ctr
    bool lgui_pressed   = zmk_hid_get_explicit_mods()  & 0x08;   //左GUI
    if( lgui_pressed || rCtrl_pressed ){
        if (!First_flg )
        {
            First_flg = true;
            direction *= -1;
            for (int i = 0; i < 10; i++) input_report_rel(data->dev, INPUT_REL_Y, direction, true , K_NO_WAIT);
        }
    } else First_flg = false;


    struct zmk_behavior_binding_event event = { .position = 0,.timestamp = now,.layer = 0,}; //event

    //i2c_read
    i2c_read_dt(&config->i2c, buf, sizeof(buf));

    float delta_x=0,delta_y=0; //移動距離(誤作動防止のためDED_ZONE考慮)
    if     ( abs((int16_t)buf[1]) > abs(buf[0])) delta_x= MOUSE_VAL_X; //buf[1]=右:指の向きで接点が短いので感度2倍
    else if( abs((int16_t)buf[0]) > abs(buf[1])) delta_x=-MOUSE_VAL_X; //buf[0]=左:同上
    if     ( abs((int16_t)buf[3]) > abs(buf[2])) delta_y= MOUSE_VAL_Y; //buf[3]=下
    else if( abs((int16_t)buf[2]) > abs(buf[3])) delta_y=-MOUSE_VAL_Y; //buf[2]=上

    bool  btn_push  = (buf[4] & MSK_SWITCH_STATE) != 0; //true:押下、false:未押下
    if ( now - data->last_activity_time < ACCEL_CANCEL_MS ){ //加速度加算
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
        delta_x = delta_x * abs(delta_x) / sqrt( delta_x*delta_x + delta_y * delta_y); //角度計算 cos変換 
        delta_y = delta_y * abs(delta_y) / sqrt( delta_x*delta_x + delta_y * delta_y); //         sin変換
        data->pre_x=delta_x;//前回移動量保存。
        data->pre_y=delta_y;
    }

    //マウス操作 or レイヤー操作 or 修飾キー or ボタン状態変化
    //if ( delta_x != 0 || delta_y != 0 || lshift_pressed || lCtrl_pressed || btn_push != data->sw_pressed) data->last_activity_time = now; //前回操作時間更新
    if ( delta_x != 0 || delta_y != 0 || lshift_pressed || btn_push != data->sw_pressed) data->last_activity_time = now; //前回操作時間更新

    //ボタン押下があれば(レイヤー操作が複雑なのでJのみ)
    if ( btn_push != data->sw_pressed ){
        data->sw_pressed = btn_push;
        if (layer == 0 || layer == 1 ) { //スクロールレイヤ
            binding.behavior_dev="key_press";
            binding.param1 = 0x0D; 
            zmk_behavior_invoke_binding(&binding, event, btn_push);  //Jキー扱い
        }
                                  //        else if(layer == 2){//            binding.behavior_dev="key_press";//            binding.param1 = 0x3E; //F5//            zmk_behavior_invoke_binding(&binding, event, btn_push);//        } else {//            binding.behavior_dev="key_press";//            binding.param1 = 0x3E; //F5//            zmk_behavior_invoke_binding(&binding, event, btn_push);//        }
    }
    if (layer == 2 || layer == 4 ) { //スクロールレイヤ
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
        if (layer == 3 || layer == 5 )     scaling      *= 6.0f; //レイヤー:高速
                        //        if (layer == 3)     scaling      *= 4.0f; //レイヤー:高速 //        else if (layer == 4) scaling *= 9.0f; //Ctrl:超高速
        for (int i = 0; i < 3; i++) { //移動を滑らかに
            input_report_rel(data->dev, INPUT_REL_X, delta_x / 3 * scaling, false, K_NO_WAIT);
            input_report_rel(data->dev, INPUT_REL_Y, delta_y / 3 * scaling, true , K_NO_WAIT);
        }
    }

    // ジグラーレイヤー:ジグラー操作
    /* layer=1 → ジグラー ON */
    if (layer == 1 && !data->jiggler_on) {
        data->jiggler_on = true;
        k_work_schedule(&data->jiggler_work, JIG_WAIT_MS);    /* ジグラー開始*/
        //deep sleepは常時許可。ジグラー中は有効にならないけど。  zmk_pm_disable(); /* deep sleep を禁止（light sleep のみ許可） */
    }
    /* layer!=1 → ジグラー OFF */
    if (layer != 1 && data->jiggler_on) {
        data->jiggler_on = false;
        //zmk_pm_enable();  /* deep sleep 再許可 */
        k_work_cancel_delayable(&data->jiggler_work);                 /* ジグラー停止 */
    }

    return;
}

///////////////////////////////////////////////////////////////////////////
// #02.1 ポーリング
static void az1uball_polling(struct k_timer *timer)
{
    struct az1uball_data *data = CONTAINER_OF(timer, struct az1uball_data, polling_timer);

    /* ポーリングモードに応じて再スケジュール */
    switch (data->poll_mode) {
    case POLL_MODE_NOR:
        k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);
        break;

//    case POLL_MODE_BLE:
//        k_timer_start(&data->polling_timer, BLE_POLL_MS, BLE_POLL_MS);
//        break;
//
    case POLL_MODE_JIG:
        k_timer_start(&data->polling_timer, JIG_WAIT_MS, JIG_WAIT_MS);
        break;
    }

    /* I2C 読み取りワークを実行 */
    k_work_submit(&data->work);
}

///////////////////////////////////////////////////////////////////////////
// #02.2 ポーリング切り替え関数
static void az1uball_set_poll_mode(struct az1uball_data *data, uint8_t mode)
{
    data->poll_mode = mode;
    k_timer_stop(&data->polling_timer);

    switch (mode) {
    case POLL_MODE_NOR:
        k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);
        break;

//    case POLL_MODE_BLE:
//        k_timer_start(&data->polling_timer, BLE_POLL_MS, BLE_POLL_MS);
//        break;
//
    case POLL_MODE_JIG:
        k_timer_start(&data->polling_timer, JIG_WAIT_MS, JIG_WAIT_MS);
        break;
    }
}

///////////////////////////////////////////////////////////////////////////
// #02.3 ポーリング停止 スリープ時(CONFIG_ZMK_IDLE_TIMEOUT)
void zmk_sleep(void)
{
    struct az1uball_data *data = &az1uball_data_0;
        /* ジグラー ON → 超低頻度ポーリング（4分） */
        az1uball_set_poll_mode(data, POLL_MODE_JIG);
    } else {
        /* ジグラー OFF → ポーリング停止（deep sleep 可能） */
        k_timer_stop(&data->polling_timer);
    }
}
///////////////////////////////////////////////////////////////////////////
// #02.4 ポーリング再開
void zmk_wake(void)
{
    struct az1uball_data *data = &az1uball_data_0;
    az1uball_set_poll_mode(data, POLL_MODE_NOR);
//    if (data->jiggler_on) {
//        /* ジグラー ON → BLE_POLL_MS に戻す */
//        az1uball_set_poll_mode(data, POLL_MODE_BLE);
//    } else {
//        /* ジグラー OFF → 通常ポーリング */
//        az1uball_set_poll_mode(data, POLL_MODE_NOR);
//    }

}

///////////////////////////////////////////////////////////////////////////
// #03.割込み検知
static int az1uball_event_handler(const zmk_event_t *eh)
{
    const struct zmk_position_state_changed *ev =
        as_zmk_position_state_changed(eh);

    if (!ev) {
        return 0;
    }

    /* 押下時のみ処理 */
    if (ev->state) {
        struct az1uball_data *data = &az1uball_data_0;
        data->last_activity_time = k_uptime_get();
        az1uball_set_poll_mode(data, POLL_MODE_NOR);
//        if (data->jiggler_on) {
//            /* ジグラーON → BLE_POLL_MS に戻す */
//            az1uball_set_poll_mode(data, POLL_MODE_BLE);
//        } else {
//            /* 通常 → NOR_POLL_MS */
//            az1uball_set_poll_mode(data, POLL_MODE_NOR);
//        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////
// #04.ジグラー関数
static void az1uball_jiggler_work(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct az1uball_data *data = CONTAINER_OF(dwork, struct az1uball_data, jiggler_work);

    if (!data->jiggler_on) {
        return;
    }

    /* マウスジグラー動作 */
    input_report_rel(data->dev, INPUT_REL_X, 1, true, K_NO_WAIT);
    k_sleep(K_MSEC(10));
    input_report_rel(data->dev, INPUT_REL_X, -1, true, K_NO_WAIT);
    /* 次のジグラーを予約 */
    k_work_schedule(&data->jiggler_work, JIG_WAIT_MS);
}


///////////////////////////////////////////////////////////////////////////
// #05-XX.初期処理 / 感度取得.
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
// #05 初期処理
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
    data->jiggler_on = false;
    data->poll_mode = POLL_MODE_NOR;
    device_is_ready(config->i2c.bus); //i2c_初期化
    i2c_write_dt(&config->i2c, &cmd, sizeof(cmd));
    k_work_init(&data->work          , az1uball_read_data_work);/* work/timer 初期化 */
    k_timer_init( &data->polling_timer, az1uball_polling        , NULL);
    k_work_init_delayable(&data->jiggler_work, az1uball_jiggler_work);  /* ★ ジグラー用遅延ワーク初期化 */
    k_timer_start(&data->polling_timer, NOR_POLL_MS, NOR_POLL_MS);      /* 初期ポーリング開始 */
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// 11.DEFINE
///////////////////////////////////////////////////////////////////////////
ZMK_LISTENER(az1uball, az1uball_event_handler);
ZMK_SUBSCRIPTION(az1uball, zmk_position_state_changed);

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