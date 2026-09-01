# WinDepth — C port (native Win32 + WASM)

Bio_100% の Windows フリーソフト **WinDepth 0.41** (1994-1998) を、
実行ファイルを Ghidra で逆コンパイルして解析し、**C 言語で書き直した**ものです。

* **ネイティブ (Win32)** — `windepth.exe`、8bpp DIB + GDI、依存は gdi32/winmm のみ
* **WASM** — 同じコアを Emscripten でビルド。描画は全部ソフトウェア（**WebGL は使いません**）

音は効果音（原作の WAV）と BGM（原作の MIDI）の両方が鳴ります。BGM はネイティブ版が
MCI 経由で Windows のシンセに投げ、WASM 版は**自前の MIDI シンセ**（`src/synth.c`）で
合成します。ブラウザに MIDI 出力が無く、再配布できるサウンドフォントも無いためです。

遊ぶ: https://yomei-o.github.io/windepth_wasm/

```
←/→ (H/L)     駆逐艦を左右に
Z / Space     左の爆雷投下
X / Enter     右の爆雷投下
F2            ゲーム開始
Esc           タイトルへ
F3 / F4       効果音 / 音楽の ON-OFF
```

## 何をどう作ったか

原作は 512×384 の 8bpp サーフェスに毎フレーム描き直す、Bio_100% 自製ライブラリ
"WinGL" の上に載った 20fps (50ms タイマー) のゲームです。移植は原作の

* グラフィック書庫 **DAR** のフォーマット（`src/dar.c`、下記 RESUME.md 参照）
* ステージ表 **windepth.dat**（テキスト、原作がそのまま読む）
* ゲームロジック（`FUN_00404860`、12KB の 1 関数）

を解析して、C で書き直しています。逆コンパイル出力の丸写しではなく、
定数と挙動は原作どおり・コードの形は普通の C というつくりです。
原作の癖（爆発の連鎖判定が敵が生きている間だけ自機を判定する等）は
意図的にそのまま残してコメントを付けてあります。

| ファイル | 中身 |
|---|---|
| `src/dar.c` `src/dar.h` | DAR 書庫の解析とスプライト描画 |
| `src/video.c` `src/video.h` | 512×384 8bpp サーフェス、ダーティ矩形、フォント、波紋描画 |
| `src/stage.c` `src/stage.h` | `windepth.dat` パーサ |
| `src/game.c` `src/game.h` | ゲーム本体（自機・敵・爆雷・ボス・アイテム・得点） |
| `src/patterns.h` | DAR のパターン番号 |
| `src/sintab.h` | 原作 `0x00421b18` の sin テーブル（波紋用、実物をそのまま） |
| `src/smf.c` `src/smf.h` | 標準 MIDI ファイルのパーサ（トラックを 1 本にマージ） |
| `src/synth.c` `src/synth.h` | GM 風のソフトシンセ。加算合成の波形テーブル 3 段 + ADSR + ドラム |
| `src/main_win32.c` | Win32 フロントエンド |
| `src/main_wasm.c` | Emscripten フロントエンド |
| `index.html` | ブラウザ側（canvas + putImageData + Web Audio） |

## ビルド

**ネイティブ (mingw / w64devkit)**

```sh
sh tools/build.sh          # -> windepth.exe
./windepth.exe
```

**WASM (emsdk)**

```sh
EMSDK=/path/to/emsdk sh tools/build_wasm.sh   # -> windepth.js / windepth.wasm
```

出力はリポジトリ直下に置きます（GitHub Pages が `main:/` を配信する設定なので）。

ビルドは全部 `tools/lowpri.sh` 経由で低優先度で走ります。

## テスト

ウィンドウを開かずに検証できるようにしてあります。

```sh
sh tools/build_tests.sh

# 指定フレームを PNG に書き出す。--auto は雑なオートプレイ
./tests/frames.exe tests/out/f 100,240 
./tests/frames.exe tests/out/a 400,800 --start 130 --auto
./tests/frames.exe tests/out/c 260     --start 130 --stage 3  --auto   # チャレンジ面
./tests/frames.exe tests/out/b 500     --start 130 --stage 19 --auto   # ボス面

# DAR の全 329 パターンを PNG で吐く（ゲームと同じデコーダを使う）
mkdir -p tests/dar2 && ./tests/dumpdar.exe tests/dar2

# WASM 側も node で同じフレームを出せる（ネイティブと一致するはず）。
# BGM が鳴っているかもここで確認する
node tests/wasm_check.js 240 tests/out/wasm240.png

# index.html の中身を Web Audio のスタブ相手に node で動かす。
# --early はキー入力が wasm の準備より先に来る順序（これで一度無音になった）
node tests/page_audio_check.js
node tests/page_audio_check.js --early

# WASM 版の BGM を WAV に書き出して実際に聴く
./tests/midi2wav.exe data/Windepth.mid tests/out/bgm.wav  40
./tests/midi2wav.exe data/NAMEINN.MID  tests/out/name.wav 25
```

`tools/shot.ps1` はネイティブ版のウィンドウを実際に撮るスクリプトですが、
GDI の経路を確認する時だけ使うもので、普段の検証には不要です。

## データの取り出し

`data/` の中身は原作の実行ファイルのリソースから抜いたものです。

```sh
/c/Python313/python tools/dump_dar.py data/windepth.dar    # 書庫の一覧を表示
```

| `data/` | 由来 |
|---|---|
| `windepth.dar` | `Windepth.exe` のリソース `DAR/WIND_DAR` (278,268 B、329 パターン) |
| `*.wav` | リソース `WAVE/BURN` `START` `ENESHOT` `ITEM` `PLANE` `DROP` |
| `windepth.dat` | 配布物のステージ表（テキスト、無改変） |
| `Windepth.mid` `NAMEINN.MID` | 配布物の BGM。ネイティブ版は MCI、WASM 版は `src/synth.c` で合成 |

## 権利について

WinDepth はもともと **Bio_100%** が配布していたフリーソフトです
(http://bio.and.or.jp/ ・現在は消滅)。`orig/` にオリジナルの配布アーカイブ
`windp041.zip` をそのまま入れてあります。`data/` はその中の実行ファイルから
抜いた素材で、著作権は Bio_100% にあります。このリポジトリの C ソースは
解析して書き直したものです。

原作は Windows 11 でもそのまま起動します。移植の答え合わせに使えます。

## わかっていること・まだやっていないこと

* ネイティブ・WASM ともタイトル / 通常面 / チャレンジ面 / ボス面 / ゲームオーバー /
  ネーム入力まで一通り動きます。
* ハイスコアの保存（原作は `windepth.ini`）と、スコアをネットに送る機能は未実装。
  起動するたびに既定の 10 件から始まります。
* BGM はネイティブ版と WASM 版で音が違います。ネイティブは Windows のシンセ
  （原作と同じ経路）、WASM は `src/synth.c` の自前シンセなので、90 年前後の
  サウンドカードくらいの音になります。
* ジョイスティック入力は未実装（原作は `joyGetPosEx`）。

解析の詳細は [RESUME.md](RESUME.md) にあります。
