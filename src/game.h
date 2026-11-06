/* WinDepth's game object - the ~25KB struct the original keeps at a single
 * global and passes around as `this`.  Field names map onto the offsets the
 * decompilation shows; the comments give the original offset so the two can be
 * compared.
 *
 * Everything runs at a fixed 20Hz (the original arms a 50ms timer in
 * FUN_00401cd0) on a 512x384 8bpp surface.
 */
#ifndef GAME_H
#define GAME_H

#include "video.h"
#include "stage.h"

/* app mode - original +0x500 */
enum { MODE_TITLE = 0, MODE_PLAY = 1, MODE_NAME = 3, MODE_LOGO = 4 };
/* play state - original +0x451c */
enum { PS_NONE = 0, PS_PLAY = 1, PS_RESPAWN = 2, PS_GAMEOVER = 3, PS_CLEAR = 5 };

/* pad bits, as FUN_0040f110 packs them */
#define PAD_UP    0x0001
#define PAD_DOWN  0x0002
#define PAD_LEFT  0x0004
#define PAD_RIGHT 0x0008
#define PAD_BTN1  0x0010
#define PAD_BTN2  0x0020
#define PAD_ESC   0x8000

/* sound slots, in the order FUN_0040da00 registers them */
enum { SND_BURN = 0, SND_START = 1, SND_ENESHOT = 2, SND_ITEM = 3,
       SND_PLANE = 4, SND_DROP = 5 };

#define MAX_SHIPS   2      /* the original has one; the second is co-op */
#define MAX_ENEMY  33
#define PLAYER_BOMBS 4     /* each ship owns four slots, ship 0 first */
#define PLANE_BOMB0 (MAX_SHIPS * PLAYER_BOMBS)   /* the plane's start here */
#define MAX_BOMB   (PLANE_BOMB0 + 20)            /* the plane keeps its 20 */
#define MAX_SHOT   48
#define MAX_BURN   48
#define MAX_SPLASH 24
#define MAX_ITEM    4
#define MAX_POPUP  33
#define MAX_SCORES 10

/* Everything that moves also carries rx/ry, the port's sub-pixel remainder
 * when the logic runs faster than the original (see `k` below). */
typedef struct {           /* +0x4d34, stride 0x2c */
    int alive;             /* 0 = free; kind 3 toggles between 1 and 2 */
    int kind;              /* 0..4 from the stage weights, 9 = item fish */
    int x, y;
    int vx, vy;
    int rx, ry;
    int nvx, nvy;          /* challenge script's post-VChangeTime velocity */
    int face;              /* last non-zero vx, picks the L/R sprite */
    int frame;
    int timer;
} Enemy;

typedef struct { int frame, x, y, vx, vy, ry; } Bomb;          /* +0x5794, stride 0x14 */
typedef struct { int frame, kind, x, y, vx, vy, rx, ry; } Shot;/* +0x52e0, stride 0x18 */
typedef struct { int frame, x, y, chain, hits; } Burn; /* +0x5974, stride 0x14 */
typedef struct { int frame, x, y; } Splash;            /* +0x5d34, stride 0x0c */
typedef struct { int t, kind, x, y, vx, vy, rx, ry; } Item;    /* +0x5e54, stride 0x18 */
typedef struct { int t, x, y, ndig, value, chain; } Popup; /* +0x5eb4, stride 0x18 */

/* A destroyer.  Ship 0 is the original's, and its fields carry the original
 * offsets; ship 1 only exists in a two-player co-op run.
 *
 * `down` and `safe` are co-op only.  Solo, a sinking puts the whole game into
 * PS_RESPAWN and clears the board, as the original does; with two players that
 * would punish the partner, so a sunk ship sits out on its own instead and
 * comes back blinking and briefly untouchable. */
typedef struct {
    int in;               /* still in the run: sinking with no lives left in
                           * the pool puts a co-op player out */
    int x, y, vx;         /* +0x4d04 +0x4d08 +0x4d0c */
    int rx;
    int bombs_ready;      /* +0x4d14 */
    int trig1, trig2;     /* +0x6204 +0x6200 - button release latches */
    int down;             /* ticks since it sank, 0 = afloat */
    int safe;             /* ticks of invulnerability left after coming back */
} Ship;

typedef struct {                    /* +0x425860, stride 0x2c */
    unsigned score;
    int level;
    int maxhit;
    int fallnum;
    char name[10];
    char date[10];
    int sent;
    int chksum;
} ScoreEntry;

typedef struct {
    Video *v;
    const Dar *dar;
    Stage st;
    char datpath[512];
    char datpath2p[512];  /* windepth2p.dat: co-op versions of some stages */

    /* The logic rate.  The original ticks every 50ms; at k > 1 game_tick is
     * meant to be called k times as often.  Only the first call of each k is
     * a "major" tick, which does everything the original's tick does - timers,
     * random rolls, steering, animation; the others only move things, by 1/k
     * of a step each, and test collisions.  k = 1 is the original exactly. */
    int k, sub, major;
    int tickmode;         /* the mode this original tick started in */

    int mode;             /* +0x500  */
    int pstate;           /* +0x451c */
    int stageno;          /* +0x4520 */
    int stimer;           /* +0x4cfc - frames since the state changed */

    int use_mouse;        /* +0x450c */
    int sfx_on;           /* +0x4510 */
    int bgm_on;           /* +0x4514 */

    Ship ship[MAX_SHIPS];
    int nships;           /* 1, or 2 for co-op */
    int lives;            /* +0x4cec - in co-op, the pool both ships draw on */
    unsigned score;       /* +0x4cdc - the run's total, shared in co-op */
    unsigned stagescore;  /* +0x4ce4 - this stage only, shown on CLEAR */
    unsigned hiscore;     /* +0x4ce0 */
    int maxchain;         /* +0x4cf4 */
    int fallnum;          /* +0x4ce8 */

    int bomb_icon;        /* +0x4d18 - cycles 1..4 for the HUD icon */
    int fish_flag;        /* +0x4554 - 1 = spawn the fish, 2 = spawned */
    int chal_total;       /* co-op: enemies in this challenge stage's script */
    int chal_kills;       /*        and how many of them were sunk */
    unsigned perfect;     /*        the PERFECT bonus awarded, for the overlay */
    int live_enemies;     /* +0x4550 */

    int plane, planex, planey, planevx, planetimer;  /* +0x4d1c.. +0x4d30 */
    int planerx;

    int blink;            /* +0x61f8 - flips every tick, halves anim rates */
    int wavephase;        /* +0x61f0 */
    int logofade;         /* +0x61f4 */

    unsigned pad, padprev;/* +0x6214 +0x6210 - player 1, and the menus */
    unsigned pad2;        /* player 2, fed in by the host like `pad` */
    int mx, my, mbl, mbr; /* mouse, fed in by the host each tick */
    int mouse_in;

    /* name entry - original +0x61cc.. */
    int name_col, name_row, name_len, name_idx, name_done;
    int name_timer;       /* +0x4cf8 counts down to the title screen */
    int cur_entry;        /* +0x4cf0 - which score row is being edited */

    Enemy  en[MAX_ENEMY];
    Bomb   bomb[MAX_BOMB];
    Shot   shot[MAX_SHOT];
    Burn   burn[MAX_BURN];
    Splash spl[MAX_SPLASH];
    Item   item[MAX_ITEM];
    Popup  pop[MAX_POPUP];

    struct {              /* +0x5760.. */
        int state;        /* 0 = absent, 1..8 = attack patterns, 9 = dying, 10 = dead */
        int hp, maxhp;
        int boom;         /* pending hit sparks */
        int x, y;
        int vx, vy, dvy;
        int rx, ry;
        int lastvx;
        int interval, timer, phase, gunalt;
    } boss;

    ScoreEntry top[MAX_SCORES];
    ScoreEntry top2[MAX_SCORES];  /* co-op runs get a table of their own */
    int shown2p;          /* the attract screen's table: 0 = solo, 1 = co-op */
    int played2p;         /* a co-op run has happened, so show its table too */

    unsigned char tint[256];  /* palette remap that colours ship 1 */

    unsigned rnd;         /* the MSVC LCG the original uses */
} Game;

/* Host hooks.  The Win32 and WASM front ends provide these. */
void wd_sound(int slot, int loop);
void wd_music(int which, int on);     /* which: 0 = windepth.mid, 1 = nameinn.mid */

void game_init(Game *g, Video *v, const Dar *dar, const char *datpath);
void game_tick(Game *g);              /* advance one 50ms frame and redraw */
void game_start(Game *g, int players);/* menu "Start": begin a run; 2 = co-op */
void game_logo(Game *g);              /* the Bio_100% splash, where a finished
                                       * run returns to before the title */
void game_title(Game *g);             /* the attract screen */
void game_stage(Game *g, int no);    /* jump the current run to stage `no` */
void game_set_rate(Game *g, int k);  /* logic ticks per original 50ms tick */
int  game_rand(Game *g);

#endif
