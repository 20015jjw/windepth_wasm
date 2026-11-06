# WinDepth — C port (native Win32 + WASM)

Bio_100% の Windows フリーソフト **WinDepth 0.41** (1994-1998) を、
実行ファイルを Ghidra で逆コンパイルして解析し、**C 言語で書き直した**ものです。

* **ネイティブ (Win32)** — `windepth.exe`、8bpp DIB + GDI、依存は gdi32/winmm のみ
* **WASM** — 同じコアを Emscripten でビルド。原作どおりのソフトウェア描画（クラシック）と、
  WebGL2 で描く 60Hz のモダン表示を切り替えられます（下記）

音は効果音（原作の WAV）と BGM（原作の MIDI）の両方が鳴ります。BGM はネイティブ版が
MCI 経由で Windows のシンセに投げ、WASM 版は**自前の MIDI シンセ**（`src/synth.c`）で
合成します。ブラウザに MIDI 出力が無く、再配布できるサウンドフォントも無いためです。

**BGM が鳴り出すのはゲーム開始（F2）から**で、Bio ロゴとタイトルは無音です。
これは原作どおりです（原作で BGM を開始するのは Start とネーム入力の 2 か所だけ）。
タイトルからも鳴らしたい場合は `src/game.c` の `BGM_FROM_TITLE` を 1 にします。

遊ぶ: https://yomei-o.github.io/windepth_wasm/

```
←/→ (H/L)     駆逐艦を左右に
Z / Space     左の爆雷投下
X / Enter     右の爆雷投下
F2 / 1        ゲーム開始（1 人）
2             2 人協力で開始（タイトル画面から）
Esc           タイトルへ
F3 / F4       効果音 / 音楽の ON-OFF
F8            見た目（モダン表示：くっきり → なめらか → ブラウン管）
F9            クラシック / モダン の切り替え

2P: A / D     駆逐艦を左右に
    T / Y     左 / 右の爆雷投下
```

## 2 人協力プレイ（原作にない追加）

タイトルで `2` を押すと 2 人協力で始まります。2P のキーは 1P と 1 つも重ならないので、
1 台のキーボードを分け合っても、キーボードを 2 台つないでも遊べます
（ブラウザはキーボードを区別しませんが、キーが別なので問題ありません）。

* 2P の駆逐艦は赤茶色。画面上部に各自の残り爆雷（`1P ####` / `2P ####`）が出ます。
* **残機は 2 人で共有の 6 機**。沈んだ艦だけが少し休んで、点滅（無敵）しながら戻ります。
  1 人プレイと違って画面はリセットされません。共有残機が 0 で沈むとその人は脱落、
  2 人とも脱落でゲームオーバー。得点も共有です。
* 通常面は同時に出る敵の上限が 1.5 倍（最大 33）、ボスの耐久は 25 → 38。
  ボスの攻撃パターンが変わる耐久の境目も同じ割合で動きます。
* チャレンジ面は `data/windepth2p.dat` の 2 人用の版を使います。片側から来る編隊には
  反対側から鏡写しの編隊が加わり（斜めの列は X に、横一列は 2 車線に）、遅い敵は少し速く。
  **全滅させると PERFECT ボーナス 10000 点**。ファイルの先頭に書式と制限を書いてあります。
* ハイスコア表は 1 人用とは別。タイトル画面では 2 人プレイの後から交互に表示します。
* `index.html?stage=15` のように付けると、どのゲームもその面から始まります（調整用）。

1 人プレイは原作のままです。変更の前後で `frames` の 1 人プレイの出力（PNG と状態遷移）が
バイト単位で一致することを確認しています。

## モダン表示（ブラウザ版のみ、原作にない追加）

ブラウザ版は既定でモダン表示です。F9 で原作どおりのクラシック（20Hz・512×384 の
8bpp サーフェス）に戻せます。選んだ表示はブラウザに記憶されます。

* **ロジックが 60Hz**。原作の 1 tick (50ms) を 3 つの細かい tick に分け、動く物は毎回
  速度の 1/3 ずつ動きます（端数は持ち越すので 3 回でちょうど原作の 1 歩）。タイマー・
  乱数・加速・アニメは原作の tick 境界でだけ進むので、速さも出現率も原作と同じです。
  変わるのは、入力が 3 倍細かく効くこと、当たり判定が 3 倍細かいこと、動いている敵の
  撃つ弾が「その瞬間の位置」から出ることだけ。入力も当たりもない場面（タイトル、
  チャレンジ面を放置）では、tick 境界の画面が原作と 1 ピクセルも違わないことを
  確認しています。
* **描画は GPU**。コアは描く代わりに描画命令を記録し（`video.h` の `VidCmd`、位置は
  1/256 ピクセル単位）、ページが全パターンを詰めたテクスチャ（`src/atlas.c`）から
  画面の解像度そのままで描きます。物がピクセルの間をなめらかに動き、60Hz より速い
  モニタでは直前の 2 tick の間を補間して描きます。
* **見た目は 3 種類**（F8）。くっきり（四角いピクセルのまま、縁だけ 1 画面ピクセル
  なめらか）、なめらか（全パターンを xBR 系のフィルタで 4 倍に拡大）、ブラウン管
  （走査線とアパーチャグリル）。
* WebGL2 が使えないブラウザでは、自動でクラシック（2D canvas）になります。

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
| `src/game.c` `src/game.h` | ゲーム本体（自機・敵・爆雷・ボス・アイテム・得点・2 人協力） |
| `src/patterns.h` | DAR のパターン番号 |
| `src/sintab.h` | 原作 `0x00421b18` の sin テーブル（波紋用、実物をそのまま） |
| `src/smf.c` `src/smf.h` | 標準 MIDI ファイルのパーサ（トラックを 1 本にマージ） |
| `src/synth.c` `src/synth.h` | GM 風のソフトシンセ。加算合成の波形テーブル 3 段 + ADSR + ドラム |
| `src/main_win32.c` | Win32 フロントエンド |
| `src/atlas.c` `src/atlas.h` | 全パターンを 1 枚の RGBA テクスチャに詰める。なめらか用の xBR 系 2 倍フィルタ |
| `src/main_wasm.c` | Emscripten フロントエンド |
| `index.html` | ブラウザ側（WebGL2 のモダン表示 / 2D canvas のクラシック、Web Audio） |

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
モダン表示を試すときは `file://` ではなく HTTP で開いてください
（例: `python3 -m http.server` で `http://localhost:8000/`）。

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
./tests/frames.exe tests/out/p 400     --start 130 --stage 3 --auto --players 2   # 2 人協力

# 2 人用チャレンジ面が敵の枠 33 に収まっているか（同時に生きている敵の最大数）
./tests/frames.exe tests/out/x 2000    --start 130 --stage 15 --players 2 --live

# モダン表示の 60Hz ロジック。tick 番号は原作の 50ms 単位のまま。
# 入力なしのチャレンジ面やタイトルは --rate 1 と同じ PNG になるはず
./tests/frames.exe tests/out/k 260,560  --start 130 --stage 12 --rate 3

# 描画命令の記録だけから毎フレーム描き直し、ラスタライザの結果と 1 ピクセルも
# 違わないことを確かめる（WebGL 側に渡しているものの検査）
./tests/frames.exe tests/out/r 6000    --start 130 --auto --replay --rate 3

# 状態遷移だけ見る（ロゴ→タイトル→プレイ→ゲームオーバー→ロゴ）
./tests/frames.exe tests/out/z 3000 --start 130 --auto --trace

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
| `windepth2p.dat` | 2 人協力用のチャレンジ面（この移植で追加。原作のものではない） |
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
  サウンドカードくらいの音になります。曲は 2 つだけで、全ステージ共通の
  `Windepth.mid` とネーム入力の `NAMEINN.MID` です（原作も面ごとには変わりません）。
* ジョイスティック入力は未実装（原作は `joyGetPosEx`）。

解析の詳細は [RESUME.md](RESUME.md) にあります。
