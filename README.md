# 仕様
## 使用部品

| # | 部品 | 名称 | URL |
| --- | --- | --- | --- |
| 1 | PCB（基盤） | CSTS40 | https://ja.aliexpress.com/item/1005004702079962.html |
| 2 | MCU (ﾏｲｺﾝ) | XIAO nRF52840 Plus | https://wiki.seeedstudio.com/ja/XIAO_BLE/ |
| 3 | (自作)behavior | behavior_capslock_led(Caps Lock 状態の検知)　| https://github.com/glove0x0a-png/zmk/blob/main/app/src/behaviors/behavior_capslock_led.c |
| 4 | track ball | AZ1UBALL | https://booth.pm/ja/items/4202085?srsltid=AfmBOorsZkPYZq80An_3UKa-HHpciWQeS2lXHikH4J_Y13bmAngWI2rI |

## キーボード

  - 47key キーボード(4行×12列)をUSB接続 & BLE接続で実装。
  - Jキーを外してtrack ball(az1uball) 配置。
  - ファームウェアは ZMK Firmware。
  - key scanはCharlieplexにより低電力化とGPIOピンを削減。
  - RGB-LED widget.cをローカルへ統合。

---

# 構築履歴

## GPIO Pin
- 以下の紐づけでピンを接続。

| # | Pos | XIAO(D) | XIAO(gpio) | Matrix1 | Matrix2 |
| - | --- |---------|------------|---------|---------|
| 0   | LEFT  | D0      | gpio0 2    | 1行("Tab"～"R")    |  7列("-"～"Right") |
| 1-0 | LEFT  | D11     | gpio0 15   | 2行("Caps"～"F")   |  6列("P"～"Down")  |
| 1-1 | LEFT  | D1      | gpio0 3    | 3行("Shift"～"V")  |  5列("O"～"Left" ( 無効化:拡張5行目"○") |
| 2-0 | LEFT  | D12     | gpio0 19   | 4行("Ctrl"～"Fn1") |  4列("I"～"UP"   ( 無効化:拡張5行目"↑") |
| 2-1 | LEFT  | D2      | gpio0 28   | なし               |  3列("U"～"Fn2") ( 無効化:拡張5行目"←") |
| 3-0 | LEFT  | D13     | gpio1 1    | なし               |  2列("Y"～"N")   ( 無効化:拡張5行目"→") |
| 3-1 | LEFT  | D3      | gpio0 29   | なし               |  1列("T"～"△")  ( 無効化:拡張5行目"↓") |
| 4-0 | LEFT  | D14     | gpio0 9    | 5列("R"～"Fn1")    |  (無効化:拡張5行目(十字キー + ボタン)) |
| 4-1 | LEFT  | D4      | gpio0 4    | I2C-SDA |
| 5-0 | LEFT  | D15     | gpio0 10   | 未使用 |
| 5-1 | LEFT  | D5      | gpio0 5    | I2C-SCL  |
| 6-0 | LEFT  | D16     | gpio0 31   | 未使用 |
| 6-1 | LEFT  | D6      | gpio1 11   | 未使用 |
| 7-1 | RIGHT | D7      | gpio1 12   | 未使用 |
| 7-2 | RIGHT | D17     | gpio1 3    | 割り込みピン |
| 8-1 | RIGHT | D8      | gpio1 13   | 未使用 |
| 8-2 | RIGHT | D18     | gpio1 5    | 1列("Esc"～"Ctrl") |  1行("T"～"-")     |
| 9-1 | RIGHT | D9      | gpio1 14   | 2列("Q"～"Win")    |  2行("G"～":")     |
| 9-2 | RIGHT | D19     | gpio1 7    | 3列("W"～"ALT")    |  3行("B"～"Enter") |
| 10  | RIGHT | D10     | gpio1 15   | 4列("E"～"L-CLK")  |  4行("△"～"Right") |


---

## ZMKでCharlieplexを実装

- PCBを Matrix1("TAB"〜"Fn1")(4x5)とMatrix2("T"〜"Right")(4x7)に分割

---

