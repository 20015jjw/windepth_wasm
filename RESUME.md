# RESUME — WinDepth 0.41 を C に戻して native -> WASM

## 対象

Bio_100% "WinDepth" 0.41 (`Windepth.exe`, 713,216 bytes, 1999-06-13)。
配布アーカイブ `windp041.zip` を `orig/` に展開して使う。

## バイナリの素性（実測）

| | |
|---|---|
| 形式 | PE32 / x86 32bit、4 セクション、ImageBase 0x400000 |
| コード | `.text` 0x1d120 = 119KB、**関数 347**（Ghidra が 346 復元、失敗 1） |
| 素性 | **ほぼ純 C**（C++ マングル名は 3 個だけ）、MSVC + 自製ライブラリ "WinGL" |
| 依存 | KERNEL32/WINMM/USER32/GDI32/ADVAPI32/SHELL32。**DirectX は動的ロードのみ** |
| 描画 | 512×384 の 8bpp サーフェス。WinGL が DirectDraw / WinG / 素の DIB を実行時に選ぶ |
| 音 | WAV は WaveMix32 か `sndPlaySound`、MIDI は `SMFDRV32.DLL` を LoadLibrary |
| 入力 | `GetAsyncKeyState` + `joyGetPosEx` + マウス |
| リソース | `.rsrc` 566KB。`DAR/WIND_DAR` に絵が全部、`WAVE/*` に音が 6 個 |

**フレームレートは 50ms = 20fps**（`FUN_00401cd0(this, 0x32, 200, 0)` が
`SetTimer` する間隔。第 2 引数 200 は取りこぼし時の追いつき上限）。

**画面は 512×384**（`0x200 × 0x180`。`FUN_004025e0(this,0,0,0x200,0x180)` = 全面ダーティ）。

## DAR フォーマット（`src/dar.c` に実装済み）

`FUN_0040f850`（マジック確認）と `FUN_0040f590`（レコード走査）から復元。

```
0x000  char   magic[5]   "DAR:8"
0x005  uint8  version    <= 1 でなければ拒否。WinDepth のものは 1
0x006  uint16 count      パターン数 = 329
0x008  uint16 x2         4（未使用）
0x00a  uint16 x3         0（未使用）
0x00c  RGBQUAD pal[256]  B,G,R,0 = Windows の DIB カラーテーブルそのまま
0x40c  count 個のレコード:
         uint16 reclen     このフィールドの直後からピクセルまでのバイト数
         uint16 w
         uint16 h          格納行数
         uint16 pitch      1 行あたりのバイト数
         int16  trans      透明パレット番号。-1 = 不透明
         [reclen>13]  uint16 ox, uint16 oy, uint16 h2   （全部 0 / h と同値）
         [reclen>15]  uint8 nfield, char name[nfield-1]（NUL 埋め）
         pixels: h * pitch バイト
```

`FUN_0040f590` の該当箇所:

* `piVar13[1] = (int)puVar16 + uVar14;` → ピクセルは `reclen` の直後
* `puVar15 = puVar16 + h * pitch + reclen;` → **1 レコードのピクセル量 = h * pitch**
* `piVar13[0xb] = (int)puVar16 + 0xf;` → 名前は固定フィールド 14 バイト + 長さ 1 バイトの後

### 行はランレングス、しかも複数ラン（ここで 2 回間違えた）

1 行の中身は:

```
uint16 gap;       /* 透明として飛ばす画素数 */
uint16 len;       /* 続く不透明画素数。0 で行終端 */
uint8  px[len];   /* 次のヘッダが 4 バイト境界に来るようパディング */
```

を **繰り返す**。`gap + len` の累計が `w` に達した時点でも終端（だから
不透明な背景タイルには終端ヘッダが無い）。全 329 パターン・6543 行すべてで
累計がちょうど `w` になる。これが正解の判定条件になる。

**嵌りどころ 1: 1 行 1 ラン だと思い込む。**
`SUB_L` のような素直なスプライトは実際に 1 行 1 ランで、それらしく絵になるので
そこで止めてしまう。残りバイトはパッカーのゴミだと判断してしまった。
`BIOLOGO1`（w=366 なのに pitch=160）で必ず破綻する。**`pitch` は行の最大長で、
どの行の長さでもない**。だから `w` と無関係に見える。

**嵌りどころ 2: 行は下から上に格納されている。**
原作の描画は DIB を負のストライドで舐める（`FUN_00411580` の
`param_2[10] = -iVar5`）。上から読むと**文字の入ったスプライトが全部上下反転**する。
潜水艦や爆発はほぼ上下対称なので気づきにくい。`SCORE`（パターン 31）を 1 枚
書き出せば一目でわかる。

### パターン番号（`src/patterns.h`）

```
 0 YAMABOKU(自機)  1 SUB_L   2 SUB_R    3 SKY    4-6 SEA01..03
 7-9 CRACK1..3    10-12 BOMB1..3   13-20 BURN1..8   21 GOLD_L 22 GOLD_R
23 WDLOGO4        24 MISSLE0  25-30 MISSLE1L..3R
31 SCORE 32 HISCORE 33 LEFT 34 READY 35 GAMEOVER 36 STAGE 37 CHALLENG
38 NSUB_L 39 NSUB_R  40 CLEAR 41 TOTAL
42-44 SWIM_L1..3  45-47 SWIM_R1..3   48-49 BRAIN1..2   50 FISH_L 51 FISH_R
52 ITEM1  53 BIOLOGO1  54 LOGOBACK  55 FIRE1  56 FIREWALL  57 PLANE
58-60 PBOMB1..3   61-64 S_BURN1..4
65 BOSS_L1 66 BOSS_L2 67 BOSS_R1 68 BOSS_R2   69 JET_R1  70 FIRE2_L 71 FIRE2_R
72 X   73 WDFONTH1  137 WDFONTH2  201 WDFONTH3  265 WDFONTH4
```

フォントは 64 字 × 4 バンク。原作は `文字 + 0x29 / 0x69 / 0xa9 / 0xe9` で引く。
`' ' + 0x29 = 73 = WDFONTH1` なので、要は「バンク先頭 + (文字 - ' ')」。
バンク 1..4 の先頭は 73 / 137 / 201 / 265。字送りは 8px 版
(`FUN_004028f0`) と 12px 版 (`FUN_00402840`) の 2 つがある。

### ボスは左右 2 枚組

`BOSS_L1` が前(砲塔側)、`BOSS_L2` が後(噴射口側)。左向きなら L1 を `x`、
L2 を `x+0x40`。右向きは `BOSS_R2` を `x`、`BOSS_R1` を `x+0x40`（**番号が入れ替わる**）。

## 波紋 (`FUN_0040ea10`) — タイトルの炎と Bio ロゴ

行ごとに横方向へずらして描くだけ。

```
dx[row] = (sintab[(phase + row * (0x200 / |wave|)) & 0xff] * amp) >> 7
```

* `sintab` は `0x00421b18` の 256 バイト符号付き（振幅 ±127）。`src/sintab.h` に実物を入れた
* `wave` が負のとき **1 行ごとに amp の符号が反転する**（ロゴの揺らぎ）
* タイトルの炎: `FUN_00402740(this, 0x18, 0xa0, 0x38=FIREWALL, 0x40, 4, 位相)`
  位相は毎フレーム `+0x10`
* Bio ロゴ: `pat 0x35=BIOLOGO1, wave=-0x40, amp = 0x28 から 0 へ減衰`、位相は `+8`

## 描画レイヤ（`src/video.c`）

| 原典 | 役割 |
|---|---|
| `FUN_004026f0(this,x,y,pat)` | パターン描画。`this+0x6208/0x620c` のオフセットを足す（常に 0） |
| `FUN_004025e0(this,x,y,w,h)` | ダーティ矩形を積む（最大 0x400 個） |
| `FUN_00402660(this,hdc)` | ダーティ矩形だけ、またはフラグが立っていれば全面を転送 |
| `FUN_00402790(this)` | 背景。y=0 に SKY、0x40 に SEA01、0x80/0xc0/0x100 に SEA02、0x140 に SEA03 |
| `FUN_00402810(this)` | LOGOBACK を 32px 刻みで全面に敷く |

**`FUN_004025e0` は y にも `+0x6208`（= x のオフセット）を足している**。
どちらも常に 0 なので影響は無いが、原作の癖なので残した。

背景は毎フレーム全面描き直すのに、ウィンドウには**ダーティ矩形しか転送しない**。
背景が動かないので成立している。移植でも同じにしてある。

**嵌りどころ 3: `StretchDIBits` は top-down DIB でも転送元 y を下端起点で数える。**
全面転送だと気づかないが、ダーティ矩形だけ転送した瞬間に
「上下反転した位置の絵が四角く貼られる」形で出る。
`SCR_H - y - h` を渡す（`src/main_win32.c`）。

## ゲーム状態（原作 `this` は約 0x6220 バイト、`src/game.h` に対応）

| 原典 | 意味 |
|---|---|
| `+0x500` | アプリモード: 4=Bio ロゴ, 0=タイトル/デモ, 1=プレイ, 3=ネーム入力 |
| `+0x451c` | プレイ状態: 1=プレイ, 2=やり直し, 3=ゲームオーバー, 5=面クリア |
| `+0x4cfc` | 状態タイマ（毎フレーム +1。デコンパイル側には加算が無く、生アセンブラのタイマ側で加算される） |
| `+0x4520` | ステージ番号 1..0x14。超えたら 1 に戻して `FUN_00402f50(this,1)` |
| `+0x4524` | ステージ種別 1=Normal 2=Challenge 3=Boss |
| `+0x4528 / +0x4530 / +0x452c` | ノルマ / 同時出現数上限 / Item フラグ |
| `+0x4534` `+0x4538..+0x4548` | 敵種別抽選の母数と閾値 |
| `+0x4550` `+0x454c` `+0x4554` | 生存敵数 / チャレンジ残り / 魚フラグ |
| `+0x4558` | `ノルマ/3*2`。ここまで減ったら魚（アイテム持ち）が 1 匹出る |
| `+0x4d04 +0x4d08 +0x4d0c` | 自機 x (初期 0xe0) / y (0x26) / vx |
| `+0x4cec` | 残機（開始時 4） |
| `+0x4cdc +0x4ce4 +0x4ce0 +0x4cf4 +0x4ce8` | 総得点 / 面得点 / ハイスコア / 最大連鎖 / FallNum |
| `+0x4d14 +0x4d18` | 残爆雷数（先頭 4 スロットの空き）/ HUD アイコンのアニメ |
| `+0x4d1c..+0x4d30` | 飛行機: 有効 / x / y / vx / 投下タイマ |
| `+0x61f8` | 毎フレーム反転。アニメを半分の速度にするのに使う |
| `+0x61f0` | 波紋の位相 |
| `+0x6204 +0x6200` | ボタンの離しラッチ |
| `+0x5760..+0x5790` | ボス: state / hp(0x19) / 火花数 / x / y / vx / vy / dvy / lastvx / 発射間隔 / タイマ / 位相 / 砲門切替 |

### オブジェクト配列（`FUN_00402f80` が全部クリアする）

| 原典 | 個数 | ストライド | 何 |
|---|---|---|---|
| `+0x455c` | 60 | 0x20 | チャレンジ台本（8 int = dat の 8 項目そのまま） |
| `+0x4d34` | 33 | 0x2c | 敵 {alive,kind,x,y,vx,vy,nvx,nvy,face,frame,timer} |
| `+0x52e0` | 48 | 0x18 | 敵弾 {frame,kind,x,y,vx,vy} |
| `+0x5794` | 24 | 0x14 | 爆雷 {frame,x,y,-,vy}。**先頭 4 が自機、残りが飛行機** |
| `+0x5974` | 48 | 0x14 | 爆発 {frame,x,y,chain,hits} |
| `+0x5d34` | 24 | 0x0c | 小さい水しぶき {frame,x,y} |
| `+0x5e54` | 4 | 0x18 | アイテム {t,kind,x,y,vx,vy} |
| `+0x5eb4` | 33 | 0x18 | 得点表示 {t,x,y,桁数,値,連鎖} |

敵の `+0x20` は「最後に 0 でなかった vx」= 向き。描画がこれで L/R を選ぶ。

## 乱数は MSVC の rand()

```c
seed = seed * 0x343fd + 0x269ec3;
return (seed >> 16) & 0x7fff;
```

## windepth.dat

配布物のテキスト。`;` `#` `/` 始まりは注釈。

```
Begin <n> / Normal|Challenge|Boss / <数値行...> / End <n>
Normal:    norma, maxnum, item, k0,k1,k2,k3,k4
Challenge: time, kind, xy, vx, vy, chtime, nvx, nvy   （1 行 1 匹、最大 60）
Boss:      a, b, c
```

敵種別の抽選（`FUN_00404860`）:

```
r = rand() % total;                    /* total = k0+..+k4 */
for (k = 4; k >= 0; k--)
    if (thr[k] <= r) { kind = k; break; }
```

`thr[k]` は `k` より前の重みの累計。ただし **`kcount[k] == 0` の種別は
`thr[k] = total`** にする（`r < total` なので絶対に選ばれない）。
重みが 0 でない一番小さい `k` の `thr` は必ず 0 なので、必ずどれかに当たる。

**嵌りどころ 4: `Begin %d` の走査を sscanf の戻り値 != 1 で打ち切ってはいけない。**
ファイル内の並びは `Begin 19 / Boss / End 19` が `Begin 3` より**前**にあるので、
`Boss` の行で `sscanf(s,"Begin %d")` が 0 を返し、そこで止めるとチャレンジ面が
一切読めなくなる（kind が 0 のまま）。原作は戻り値を **-1 と比較**している
（つまり EOF だけで打ち切る）。

## 得点

| | |
|---|---|
| 爆雷の直撃 | `(kind + 1) * 10` |
| 爆発の連鎖 | `(kind + 1) * chain * 10`。`chain` は 1 発の爆発につき 10 まで伸びる |
| ボスへの命中 | 500 |
| ボス撃破 | 30000 |
| 表示 | `chain < 2` なら数値だけ、そうでなければ `値 X 連鎖`（`X` はパターン 72） |

`+0x4cf4`（MAXHIT、ハイスコア表に出る）は 1 発の爆発が巻き込んだ数の最大値。

## キー割り当て（`0x00421c28` の表）

| ビット | キー |
|---|---|
| 0x0001 上 | K / ↑ / テンキー 8 |
| 0x0002 下 | J / ↓ / テンキー 2 |
| 0x0004 左 | H / ← / テンキー 4 |
| 0x0008 右 | L / → / テンキー 6 |
| 0x0010 ボタン 1 | Z / Space（左の爆雷） |
| 0x0020 ボタン 2 | X / Enter（右の爆雷） |
| 0x8000 | Esc |

自機の速度は ±4、可動域は 0..0x1c0。左の爆雷は `x`、右は `x+0x30` から落ちる。

## 音の番号（`FUN_0040da00` の登録順 = `FUN_0040e740` の第 1 引数）

`0 BURN` / `1 START` / `2 ENESHOT` / `3 ITEM` / `4 PLANE` / `5 DROP`

## BGM

原作は `.mid` を `SMFDRV32.DLL`（LoadLibrary で動的に読む MIDI ドライバ）に渡すだけ。
`FUN_00403030` が「BGM が有効かつまだ鳴っていなければ読み込んで再生」する形なので、
**同じ曲を要求されても鳴り直さない**。移植も同じにしてある（`wd_music`）。

| | |
|---|---|
| `Windepth.mid` | format 0 / division 96 / 4395 note-on / 186 s。ch0 sax(64), ch1 clarinet(71), ch2 strings(48), ch3 slap bass(36), ch4 guitar(27), ch5 piano(1), ch6 orchestra hit(55), ch7 muted guitar(28), ch9 ドラム 1660 発 |
| `NAMEINN.MID` | format 0 / division 96 / 744 note-on / 48 s。ch0-2 すべて celesta(8) |

* **ネイティブ**は MCI に投げる（Windows のシンセ = 原作と同じ経路）。
* **WASM** は `src/smf.c` + `src/synth.c` で自前に合成する。ブラウザに MIDI 出力は無く、
  再配布できるサウンドフォントも無い。加算合成で作った波形テーブルを 3 段の
  帯域制限付きで持ち、ADSR と 1 次 LPF、ドラムだけノイズ + サイン。GM の
  プログラム番号から音色を引く。ページ側は `wd_audio()` で 4096 フレームずつ
  作って `AudioBufferSourceNode` を隙間なく予約していくだけ。
  `ScriptProcessorNode` は入力 0 本の形が環境依存なので使わず、
  `AudioWorklet` は wasm をワーカへ持ち込む手間が要るので使わない。

**嵌りどころ 5: MCI の MIDI デバイスは `play <alias> repeat` を受け付けない。**
`MCIERR_UNRECOGNIZED_KEYWORD` (259) を返して**何も鳴らずに黙る**。
`play <alias> notify` にして `MM_MCINOTIFY` で先頭に戻して再生し直す。

**嵌りどころ 6: ブラウザ側で「音の出口」を作る条件を間違えると、
効果音だけ鳴って BGM が一生鳴らない。**
最初の版は「ユーザー操作の瞬間に wasm の準備ができていれば pump を作る」に
なっていて、しかも AudioContext は先に作ってしまっていたので、以降の呼び出しは
すべて先頭で return していた。効果音は AudioContext だけで鳴るし、F4 の
ON/OFF 表示も出るので、一見それらしく動いてしまう。
`ensureContext()`（操作側）と `ensurePump()`（wasm 準備側）に分け、
どちらからでも足りない方だけ作るようにした。
`tests/page_audio_check.js` が Web Audio のスタブ相手に index.html を
node で動かして、両方の順序で音が出ることを見る。

**嵌りどころ 7: SMF のテンポイベントでデータ 3 バイトを読み飛ばすのを忘れる。**
テンポだけ特別扱いして早期に抜けると、その 3 バイトが次のデルタタイム／
ステータスとして読まれてストリームが崩壊する。`Windepth.mid` は
「note-on の総数は合っているのに tick_end が 35808 でなく 50164」、
`NAMEINN.MID` は「イベントが 1 個しか採れない」という形で出た。
メタ / SysEx の後は running status も切る（仕様どおり）。

## 残した原作の癖

* 爆発 vs 自機の当たり判定が**敵の走査ループの内側**にある。つまり
  「生きている敵が 1 匹以上いる」ときだけ自機が爆発に巻き込まれる。
* 爆発を新規に置くとき `frame` に -1 を入れ、そのフレームの最後に 1 に直す。
  同じフレームで連鎖が二重に進まないようにするため。
* アイテムは水面（y<0x36）に着いたら `vy=0` にして 100 フレーム数えて消える。
* チャレンジ面はノルマを書かないので、原作では前の面の値が残る。移植では 0 から
  減るので負になるが、クリア判定は台本の残数なので影響しない。

## 手順（この順で）

1. ~~**DAR の解析とパレット**~~ 済 — `tests/dumpdar.exe` で 329 枚出る
2. ~~**背景・フォント・波紋**~~ 済
3. ~~**ゲームロジック** (`FUN_00404860`)~~ 済 — 通常/チャレンジ/ボス、アイテム、飛行機、連鎖
4. ~~**ネイティブ版**~~ 済 — `src/main_win32.c`、8bpp DIB + `StretchDIBits`
5. ~~**WASM 版**~~ 済 — `src/main_wasm.c` + `index.html`。
   パレット展開して RGBA を `putImageData`、**WebGL 不使用**
6. ~~**BGM**~~ 済 — ネイティブは MCI、WASM は `src/smf.c` + `src/synth.c`
7. **未実装** — `windepth.ini` へのハイスコア保存、スコア送信、ジョイスティック

## 環境

* Ghidra 12.1.3 + JDK 21 を `C:\prog\ghidra` に。再実行:
  ```sh
  JAVA_HOME=C:/prog/ghidra/jdk-21.0.12.1+1 \
  /c/prog/ghidra/ghidra_12.1.3_PUBLIC/support/analyzeHeadless.bat \
    C:/prog/claude2/windepth/ghidra_proj wd \
    -import C:/prog/claude2/windepth/orig/Windepth.exe \
    -scriptPath C:/prog/claude2/windepth/tools/ghidra_scripts \
    -postScript DecompileAll.java C:/prog/claude2/windepth/decomp
  ```
  プロジェクトディレクトリは**事前に mkdir が要る**（無いと即 abort）。
  出力 `decomp/`（`all.c` 20,870 行 / `functions/*.c` / `index.csv`）は git 管理外。
* mingw は `C:\prog\w64devkit`、emsdk は `C:\prog\emsdk\emsdk`。
* emsdk の `emcc.bat` は無く **`emcc.exe`** を使う。
* ビルドは必ず `tools/lowpri.sh` 経由（`cmd start /LOW` で生成時に優先度クラスを
  設定するので、コンパイラドライバの子プロセスも全部継承する）。
* GitHub Pages は `main:/` を配信する設定なので、`windepth.js` / `windepth.wasm` /
  `index.html` / `data/` はリポジトリ直下に置く。

## 罠（環境側）

* **`tests/wasm_check.js` で `global.Module` を先に立てても無効。**
  emscripten の出力冒頭は `var Module = typeof Module != "undefined" ? Module : {}`
  で、CommonJS のモジュールスコープではこの `Module` が巻き上げられた**ローカル**に
  なるため、グローバルは拾われない。node 側は `require()` の戻り（emscripten が
  `module.exports = Module` してくれる）を使い、エクスポートが生えるまで待つ。
  ブラウザは両方グローバルスコープなのでそのまま動く。
* w64devkit に zlib は無い。テストの PNG 書き出しは stored deflate を自前で作った
  (`tests/png.c`)。
* fluidsynth / timidity / ffmpeg は入っていない。`gm.dls` は Windows にあるが
  再配布できないので、BGM は自前合成一本にした。
* **`cp data/Windepth.mid data/windepth.mid` は Windows では自爆する。**
  大文字小文字を区別しないので同じファイルへのコピーになり、続く
  `rm data/windepth.mid` が原本を消す（git から戻した）。
  emscripten の `--embed-file` には元の名前のまま渡す。
* シンセの検証は `tests/midi2wav.exe` で WAV に書き出し、
  numpy でエンベロープの自己相関（拍が合っているか）と FFT のピーク
  （鳴っている MIDI ノートと一致するか）を見た。8/16 秒の時点で
  MIDI が 43,55,67 / 30,42,50,54,66,69,74 のとき、レンダ側のピークも
  55,67,79 / 54,66,74 になる。
