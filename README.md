# 仕様
## 使用部品

| # | 部品 | 名称 | URL |
| --- | --- | --- | --- |
| 1 | PCB（基盤） | CSTS40 | https://ja.aliexpress.com/item/1005004702079962.html |
| 2 | MCU (ﾏｲｺﾝ) | XIAO nRF52840 Plus | https://wiki.seeedstudio.com/ja/XIAO_BLE/ |
| 3 | (自作)behavior | behavior_capslock_led(Caps Lock 状態の検知)　| https://github.com/glove0x0a-png/zmk/blob/main/app/src/behaviors/behavior_capslock_led.c |

## キーボード

  - 47key キーボード(4行×12列)をUSB接続 & BLE接続で実装。
  - Jキーを外してジョイスティックを配置。(https://electropeak.com/learn/interfacing-5-direction-joystick-button-module-with-arduino/#google_vignette)
  - ファームウェアは ZMK Firmware。
  - key scanはCharlieplexにより低電力化とGPIOピンを削減。

---

# 構築履歴

## GPIO Pin
- 以下の紐づけでピンを接続。

| # | rp2040 | XIAO(D) | XIAO(gpio) | コメント                                 |
| --- |--------|---------|------------|------------------------------------------|
| # | GP5    | D18     | gpio1 5    | 0行 (0,5)～(0,11) &11列(0,11)～(3,11) |
| # | GP6    | D9      | gpio1 14   | 1行 (1,5)～(1,11) &10列(0,10)～(3,10) |
| # | GP7    | D19     | gpio1 7    | 2行 (2,5)～(2,11) & 9列(0, 9)～(3, 9) |
| # | GP8    | D10     | gpio1 15   | 3行 (3,5)～(3,11) & 8列(0 ,8)～(3 ,8) |
| # | GP16   | D14     | gpio0 9    | 拡張 (4,5)～(4,10) & 7列(0,7)～(3 ,7) |
| # | GP15   | D3      | gpio0 29   | 5列 (0,5)～(3,5) & 拡張(4,5) |
| # | GP14   | D13     | gpio1 1    | 6列 (0,6)～(3,6) & 拡張(4,6) |
| # | GP13   | D2      | gpio0 28   | 7列 (0,7)～(3,7) & 拡張(4,7) |
| # | GP12   | D12     | gpio0 19   | 8列 (0,8)～(3,8) & 拡張(4,8) |
| # | GP11   | D1      | gpio0 3    | 9列 (0,9)～(3,9) & 拡張(4,9) |
| # | GP10   | D11     | gpio0 15   | 10列 P & 2行(Caps ～F)             |
| # | GP9    | D0      | gpio0 2    | 11列 - & 1行(Tab ～R)              |
| # | GP2    | D4      | gpio0 4    | 未使用 |
| # | GP3    | D5      | gpio0 5    | 未使用 |
| # | GP0    | D6      | gpio1 11   | 未使用 |
| # | GP0    | D7      | gpio1 12   | 未使用 |
| # | GP21   | D8      | gpio1 13   | ws2812 アンダーグローLED制御            |
| # | なし   | D17     | gpio1 3    | 割り込みピンとして追加                  |

---

## -1.0 QMKとZMKでCharlieplexを実装

- PCBを「TAB〜L-CLICK」（4x4）と「R〜→」（8x4）に分割  
- 合計12pinを3グループに分けて以下のスキャンを実施：

| グループ | ピン範囲 |
|---------|----------|
| A-group | 1〜4pin  |
| B-group | 5〜8pin  |
| C-group | 9〜12pin |

#### スキャン構成
- RAW C × COL A = 4x4（"TAB"(0,0)〜"L-Click"(3,3)）  
- RAW A × COL B = 4x4（"R"(0,4)〜"fn2"(3,7)）  
- RAW A × COL C = 4x4（"U"(0,8)〜"→"(3,11)）

#### 配線
- RAW → 入力GPIO  
- COL → 出力GPIO（ダイオードあり）  
- COL → 割込GPIO（ダイオードあり）  

---

## 1.0 拡張キー設定

- 数値「0」を `key_morph` で `"0"` & `"|"` に実装  

---

## 2.0 ロータリーエンコーダ(削除済み) -> 12.0へ

- default：縦  
- layer 1：横  
- layer 2：ボリューム  

---

## 3.0 BLE

- 基本構成で接続構築済みを確認（特に対応なし）  
- layer 3：BLE関連のキー配置  

### LED状態
- 青：割り当て済み＋接続  
- 赤：割り当て済み＋未接続  
- 黄：未割当  

### プロファイル切替
- fn1 + fn2 + F：プロファイル1（PC）  
- fn1 + fn2 + D：プロファイル2（Fire Stick）  
- fn1 + fn2 + S：プロファイル3（社用PC）  
- fn1 + fn2 + A：プロファイル4（空き）  
- fn1 + fn2 + Space：OFF（プロファイル5＋現在の接続クリア）  
- fn1 + fn2 + TAB：現在の接続クリア  
- // コメントアウト：fn1 + fn2 + \：全クリア  

---

## 4.0 省電力設定

```text
CONFIG_ZMK_SLEEP=y
CONFIG_ZMK_IDLE_TIMEOUT=20000
CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=900000
```

- 未操作20秒でidle  
- idle 15分でdeep sleep  
- 接続中はマウスジグラー（3分毎）(※#7参照)が有効のため、deep sleepは未接続状態のみ有効  

---

## 5.0 タップダンス　→　削除済み

- fn1, fn2 のダブルタップでCaps → 無効化  

---

## 6.0 CapsLock検知（CapsLockで赤LED発光）

- カスタムビヘイビア作成  
- `/zmk/app/src/behaviors/behavior_capslock_led.c`  

---

## 7.0 I2Cマウス

- az1uballドライバ追加  
- リスナー定義が必要だった（トラブルあり）  
- USB or BLE接続中(現在のプロファイルがactive)の場合3分毎にマウスジグラー起動。

---

## 8.0 大雑把マウス操作　→　削除

- fn2 + F：→  
- fn2 + S：←  
- fn2 + A：↑  
- fn2 + D：↓  

---

## 9.0 マウスクリックでJキー送信

- 以下のZMKイベント登録で対応  
```
zmk_behavior_invoke_binding(&binding, event, data->sw_pressed);
```

---

## 10.0 マウスの節約設定

| 状態                     | ジグラー＆マウス | polling速度 |
|--------------------------|------------------|--------------|
| 接続中の端末なし（任意） | なし              | 超低速       |
| 接続中の端末あり（未操作） | あり              | 低速         |
| 接続中の端末あり（操作中） | あり              | 高速         |

---

## 11.0 ロータリーエンコーダの精度向上（#429-OK.uf2）　→ 12.0で削除

- 通常2状態監視 → 3状態監視に変更 → 無効化  

---

## 12.0 ロータリーエンコーダ不要？（#433）

- ロータリーエンコーダの位置にGUIを設定  

### お試し構成
- layer 2：マウススクロールをK,Lに（数字レイヤーなので","、"."は `&trans` のまま）  
- layer 3：ボリュームコントロールをK,Lに  
- 問題なさそうだったので、layer 2のCaps（key(0,1)）を `&trans` のままに（誤動作防止）  

---

## 13.0 Ctrl押下でマウスホイールの向き逆転（#444〜）

- ロータリーエンコーダ排除に伴い、以下を設定（対応済み）  
  - Shift：縦 → 横  
  - Ctrl：動作反転  

---

## 14.0 FireStickで音が出ない（#455）
- ボリュームを `&kp C_VOL_UP` で実装  
- hold-tapを追加（デフォルトではタップ時間が短すぎて反応しなかった）  
- 約50msに設定  
---

## 15.0 こまごまバージョンアップ
- キーマップ見直し 475
- Ctrl,Shiftでマウスカーソル速度変更 475
- マウスカーソルの動きを滑らかに 476
