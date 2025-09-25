#  構築履歴
## -2.0 QMKとZMKの共存のために、以下を実施。
	- 前提
　		- PCB(基盤)：CSTS40
  		- MCU：Raspberry Pi Pico W(rp2040)
　  		- PCBの仕様はRAW2COLの4x12。
    		- 各接点からrp2040へはんだ付け。
    		- AZ1UBALLをI2Cで接続。(key(7,1)Jキーを削除,ボタン押下で対応)
    		- ロータリーエンコーダを増設(key(8,3)に設置)
	- 対応内容
		- XIAOは電源OFF中は特定pin間で通電あり。rp2040の動作に悪影響。
		- このため、rp2040とXIAOを接続。rp2040起動中は、XIAOを起動(全pin絶縁用のuf2を作成)。
		- 逆にXIAO起動中は、rp2040OFF(全pin絶縁確認済み)。
		- I2Cデバイスの電源部分が共有だったため、ここにダイオードを設置。

## -1.0 QMKとZMKでCharlieplexを実装。
  - PCBを、"TAB~L-CLICK"まで(4x4)と、"R~→"まで(8x4)に分割。
  - 合計12pinを3グループに分けて以下のスキャンを実施。
    A-group 1～4pin
    B-group 5～8pin
    C-group 9～12in
    ---
    RAW C x COL A = 4x4		("TAB"(0,0)	~"L-Click"(3,3))
    RAW A x COL B = 4x4		("R"	(0,4)	~"fn2"		(7,3))
    RAW A x COL C = 4x4		("U"	(0,8)	~"→"			(11,3))
    ---
  - Charlieplexに対応する為、以下の配線を実施。
    RAW→入力GPIO
    COL→出力GPIO(ダイオードあり)
    COL→割込GPIO(ダイオードあり)
   
## 1.0 拡張キー設定
	数値0を、key_morphで、"0" & "|" 実装。

## 2.0 ロータリーエンコーダ
	- default:縦
	- layer 1:横
	- layer 2:ボリューム

## 3.0 BLE
	- 基本構成で接続構築済みを確認。特に対応なし。
	- layer 3:BLE関連のキー配置。
	  - LED青：割り当て済み＋接続
		- LED赤：割り当て済み＋未接続
		- LED黄：未割当
    ---
		- fn1 + fn2 + F     :プロファイル1 (PC)
		- fn1 + fn2 + D     :プロファイル2 (Fire Stick)
		- fn1 + fn2 + S     :プロファイル3 (社用PC)
		- fn1 + fn2 + A     :プロファイル4 空き
		- fn1 + fn2 + Space :OFF(プロファイル5 + 現在の接続クリアで実現)
		- fn1 + fn2 + TAB   :現在の接続クリア
		//コメントアウト：fn1 + fn2 + \     :全クリア

## 4.0 省電力
	`CONFIG_ZMK_SLEEP=y
	CONFIG_ZMK_IDLE_TIMEOUT=20000
	CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=900000`
	- 未操作20秒でidle。
	- idle  15分でdeep sleep。
	- ただし、接続中はマウスジグラー(3分毎)が有効の為、deep sleepは未接続状態のみ有効。

## 5.0 タップダンス
  - fn1 ,fn2  doubleタップでcaps →　無効化

## 6.0 capslockの検知(CapsLOCKで赤LED発行)
	カスタムビヘイビアの作成。
	/zmk/app/src/behaviors/behavior_capslock_led.c

## 7.0 I2C マウス
	- az1uballドライバ追加。
	- トラブルあり。リスナー定義が必要だった。

## 8.0 大雑把マウス
	- fn2 + L          :→
	- fn2 + K          :←
	- fn2 + H          :↑
	- fn2 + N          :↓

## 9.0 マウスクリックでJキー送信
	//以下zmkイベント登録で対応
	- zmk_behavior_invoke_binding(&binding, event, data->sw_pressed);

## 10.0 マウスの節約設定
	- 接続中の端末なし	任意　　：ジグラー＆マウス-なし&polling"超低速"
	- 接続中の端末あり	未操作　：ジグラー＆マウス-あり&polling"低速"
	- 接続中の端末あり	操作中　：        〃         &polling"高速"

## 11.0 ロータリーエンコーダの精度向上  #429-OK.uf2
	- 通常2状態監視だったのを3状態監視に変更。→無効化。

## 12.0 ロータリーエンコーダ不要？ #433、ロータリーエンコーダの位置に、GUIを設定。
	- お試し	layer 2でマウススクロールをK,Lに。(数字レイヤーなので",","."は&transのまま)
 					layer 3でボリュームコントロールをK,Lに。
	- 問題なさそうだったので、layer 2のCaps(key(0,1))を、&transのままに。(誤動作防止)-

## 13.0 Ctrl押下でマウスホイールの向き逆転。						#444～
	- ロータリーエンコーダ排除に伴い、以下を設定。対応済み。
		shiftで縦→横
		Ctrlで動作反転

## 14.0 FireStickで音が出ない ★最新 455
	- ボリュームを&kp C_VOL_UPで実装。
	- hold-tapを追加。デフォルトではタップ時間が短すぎて反応しなかったのが原因とのこと。
	- 50ms程度。

