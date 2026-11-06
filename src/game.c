/* WinDepth's game logic, reconstructed from the Ghidra decompilation of
 * Windepth.exe 0.41 (mainly FUN_00404860, the 12KB per-frame handler, plus the
 * state transitions around it).  Structure and constants follow the original;
 * the shape of the code does not, since the original is one flat function.
 */
#include "game.h"
#include "patterns.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- helpers */

/* The original's rand(): the MSVC LCG, inlined at FUN_00415a00. */
int game_rand(Game *g)
{
    g->rnd = g->rnd * 0x343fdu + 0x269ec3u;
    return (int)((g->rnd >> 16) & 0x7fff);
}

static int sgn(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

/* The idiom the original uses everywhere: 0 when the two are equal, otherwise
 * the sign of the difference. */
static int cmp(int a, int b) { return a == b ? 0 : (a > b ? 1 : -1); }

static int pat_w(const Game *g, int p) { return g->dar->pat[p].w; }
static int pat_h(const Game *g, int p) { return g->dar->pat[p].h; }

/* Enemy hit box (FUN_00402510 / FUN_00402560): the wide sprites are 64x32,
 * the brain and the fish 32x32. */
static int en_w(const Enemy *e)
{
    switch (e->kind) {
    case 0: case 1: case 2: case 4: return 0x40;
    case 3: case 9:                 return 0x20;
    default:                        return 0;
    }
}

static int en_h(const Enemy *e)
{
    return (e->kind >= 0 && (e->kind < 5 || e->kind == 9)) ? 0x20 : 0;
}

static void sfx(Game *g, int slot)
{
    if (g->mode == MODE_PLAY && g->sfx_on)
        wd_sound(slot, 0);
}

/* FUN_00402f50: add to both the run total (saturating) and the stage total. */
static void addscore(Game *g, unsigned n)
{
    unsigned s = g->score + n;
    if (s > g->score)
        g->score = s;
    g->stagescore += n;
}

static void dirty(Game *g, int x, int y, int w, int h)
{
    vid_dirty(g->v, x, y, w, h);
}

/* ------------------------------------------------------- object spawners */

/* FUN_00402de0 - a small splash burst, only inside the horizontal band the
 * original allows. */
static void splash(Game *g, int x, int y)
{
    int i;

    if (x <= -0x10 || x >= 0x200)
        return;
    for (i = 0; i < MAX_SPLASH; i++)
        if (!g->spl[i].frame) {
            g->spl[i].x = x;
            g->spl[i].y = y;
            g->spl[i].frame = 1;
            return;
        }
}

/* FUN_00402cb0 - an explosion, plus the floating score it awards.  `frame` is
 * 1 for one started this tick by the player and -1 for one started by a chain
 * reaction, so a chain cannot advance twice in the same frame. */
static void burn(Game *g, int x, int y, int frame, unsigned value,
                 int chain, int hits)
{
    int i, j;

    for (i = 0; i < MAX_BURN; i++) {
        if (g->burn[i].frame)
            continue;
        if (g->mode == MODE_PLAY && g->sfx_on)
            wd_sound(SND_BURN, 0);
        g->burn[i].x = x;
        g->burn[i].y = y;
        g->burn[i].frame = frame;
        g->burn[i].chain = chain;
        g->burn[i].hits = hits;
        if (!value)
            return;
        for (j = 0; j < MAX_POPUP; j++) {
            if (g->pop[j].t)
                continue;
            g->pop[j].y = y + 0x18;
            g->pop[j].t = 0x1e;
            if (chain > 1) {
                g->pop[j].x = x + 8;
                g->pop[j].value = value / (unsigned)chain;
                g->pop[j].ndig = 8;
            } else {
                g->pop[j].x = x + 0x10;
                g->pop[j].value = value;
                g->pop[j].ndig = 5;
            }
            g->pop[j].chain = chain;
            return;
        }
        return;
    }
}

/* FUN_00402e40 - the pickup the item fish leaves behind, drifting up. */
static void spawn_item(Game *g, int x, int y, int kind)
{
    int i;

    for (i = 0; i < MAX_ITEM; i++)
        if (!g->item[i].t) {
            g->item[i].x = x;
            g->item[i].y = y;
            g->item[i].vx = 0;
            g->item[i].vy = -2;
            g->item[i].t = 1;
            g->item[i].kind = kind;
            return;
        }
}

/* FUN_00402eb0 - the ship is destroyed. */
static void player_hit(Game *g)
{
    burn(g, g->px, g->py - 0x10, 1, 0, 1, 1);
    dirty(g, g->px - 8, g->py, pat_w(g, P_SHIP) + 0x10, pat_h(g, P_SHIP));
    g->px = 0xe0;
    g->pvx = 0;
    if (--g->lives < 0) {
        g->pstate = PS_GAMEOVER;
        g->lives = 0;
        g->stimer = 0;
    } else {
        g->stimer = 0;
        g->pstate = PS_RESPAWN;
    }
}

/* FUN_00402f80 - drop every live object.  Called on each state change. */
static void clear_objects(Game *g)
{
    memset(g->en, 0, sizeof g->en);
    memset(g->bomb, 0, sizeof g->bomb);
    memset(g->shot, 0, sizeof g->shot);
    memset(g->burn, 0, sizeof g->burn);
    memset(g->spl, 0, sizeof g->spl);
    memset(g->item, 0, sizeof g->item);
    memset(g->pop, 0, sizeof g->pop);
    g->plane = 0;
    g->boss.state = 0;
    g->live_enemies = 0;
    g->fish_flag = 0;
}

static int load_stage(Game *g)
{
    return stage_load(&g->st, g->datpath, g->stageno);
}

/* ------------------------------------------------------------ background */

/* FUN_00402790 - the four horizontal bands: sky, then three shades of sea. */
static void draw_background(Game *g)
{
    int x, y;

    for (x = 0; x < SCR_W; x += 0x40)
        vid_pat(g->v, x, 0, P_SKY);
    for (x = 0; x < SCR_W; x += 0x40)
        vid_pat(g->v, x, 0x40, P_SEA01);
    for (x = 0; x < SCR_W; x += 0x40)
        for (y = 0x80; y < 0x101; y += 0x40)
            vid_pat(g->v, x, y, P_SEA02);
    for (x = 0; x < SCR_W; x += 0x40)
        vid_pat(g->v, x, 0x140, P_SEA03);
}

/* FUN_00402810 - the tiled backdrop behind the Bio_100% splash. */
static void draw_logoback(Game *g)
{
    int x, y;

    for (x = 0; x < SCR_W; x += 0x20)
        for (y = 0; y < SCR_H; y += 0x20)
            vid_pat(g->v, x, y, P_LOGOBACK);
}

/* ------------------------------------------------------------------- HUD */

static void num(Game *g, int x, int y, unsigned v, const char *fmt, int bank)
{
    char buf[24];

    sprintf(buf, fmt, (unsigned long)v);
    vid_text(g->v, x, y, buf, bank);
}

/* FUN_004029e0 - "<value> X <chain>", for a chained kill. */
static void num_chain(Game *g, int x, int y, unsigned v, int chain)
{
    char buf[24];
    int n;

    sprintf(buf, "%lu", (unsigned long)v);
    n = vid_text(g->v, x, y, buf, 2);
    vid_pat(g->v, x + n * 8, y, P_X);
    dirty(g, x + n * 8, y, 8, 0x10);
    sprintf(buf, "%u", (unsigned)chain);
    vid_text(g->v, x + 8 + n * 8, y, buf, 3);
}

/* FUN_00402b00 - the top-ten table shown on the attract screen. */
static void draw_scores(Game *g)
{
    int i, y, alt = 0;

    vid_text(g->v, 0x20,  0x46, "RANK",   3);
    vid_text(g->v, 0x68,  0x46, "SCORE",  3);
    vid_text(g->v, 0xb0,  0x46, "STAGE",  3);
    vid_text(g->v, 0xf0,  0x46, "MAXHIT", 3);
    vid_text(g->v, 0x140, 0x46, "NAME",   3);
    vid_text(g->v, 0x1a0, 0x46, "DATE",   3);

    for (i = 0, y = 100; y < 0x119 && i < MAX_SCORES; i++, y += 0x14) {
        const ScoreEntry *e = &g->top[i];
        char buf[24];
        int bank;

        alt ^= 1;
        if (i == g->cur_entry)
            bank = (g->blink != 1) + 3;
        else
            bank = 2 - alt;

        sprintf(buf, "%02u", (unsigned)(i + 1));      vid_text(g->v, 0x28, y, buf, bank);
        sprintf(buf, "%08lu", (unsigned long)e->score); vid_text(g->v, 0x5c, y, buf, bank);
        sprintf(buf, "%02u", (unsigned)e->level);     vid_text(g->v, 0xbc, y, buf, bank);
        sprintf(buf, "%03u", (unsigned)e->maxhit);    vid_text(g->v, 0xfc, y, buf, bank);
        vid_text(g->v, 0x130, y, e->name, bank);
        vid_text(g->v, 400,   y, e->date, bank);
    }
}

/* ------------------------------------------------------ enemy management */

/* One enemy from the challenge script. */
static void spawn_scripted(Game *g, const ChalEnt *c)
{
    Enemy *e;
    int i;

    for (i = 0; i < MAX_ENEMY; i++)
        if (!g->en[i].alive)
            break;
    if (i >= MAX_ENEMY)
        i = MAX_ENEMY - 1;      /* the original overwrites the last slot */
    e = &g->en[i];
    g->st.nchal--;
    e->kind = c->kind;
    if (c->kind == 3) {
        e->x = c->xy << 5;
        e->y = 0x180;
    } else {
        e->x = c->vx < 0 ? 0x200 : -0x40;
        e->y = c->xy * 8 + 0x50;
    }
    e->vx = c->vx;
    e->vy = c->vy;
    e->nvx = c->nvx;
    e->nvy = c->nvy;
    e->timer = c->chtime;
    e->alive = 1;
    e->frame = 0;
    g->live_enemies++;
}

/* The random spawn Normal stages use. */
static void spawn_random(Game *g, Enemy *e)
{
    int r, k, s;

    g->live_enemies++;
    e->alive = 1;
    e->frame = 0;
    e->timer = 0;

    r = game_rand(g) % g->st.total;
    for (k = 4; k >= 0; k--)
        if (g->st.thr[k] <= r) {
            e->kind = k;
            break;
        }

    e->x = (game_rand(g) & 1) == 0 ? 0x200 : -0x40;
    e->vy = 0;
    s = cmp(0x100, e->x);

    switch (e->kind) {
    case 0:
        e->y = (game_rand(g) % 10) * 8 + 0x50;
        e->vx = (game_rand(g) % 4 + 3) * s;
        if (g->mode == MODE_TITLE && e->y > 0x80 && e->y < 0xe0) {
            g->live_enemies--;
            e->alive = 0;
        }
        break;
    case 1:
        e->y = (game_rand(g) % 0x14) * 8 + 0x80;
        e->vx = (game_rand(g) % 5 + 1) * s;
        if (g->mode == MODE_TITLE && e->y > 0x80 && e->y < 0xe0) {
            g->live_enemies--;
            e->alive = 0;
        }
        break;
    case 2:
        e->y = (game_rand(g) % 0xc) * 8 + 0x100;
        e->vx = (game_rand(g) % 3 + 1) * s;
        break;
    case 3:
        e->y = 0x180;
        e->vx = 0;
        e->vy = -1;
        e->x = game_rand(g) % 0x1e0;
        break;
    case 4:
        e->y = (game_rand(g) % 0x12) * 8 + 0xa0;
        e->vx = s * 4;
        break;
    default:
        break;
    }
}

/* The single item-carrying fish, released a third of the way through a stage. */
static void spawn_fish(Game *g, Enemy *e)
{
    g->fish_flag = 2;
    g->live_enemies++;
    e->alive = 1;
    e->frame = 0;
    e->kind = 9;
    e->x = (game_rand(g) & 1) == 0 ? 0x200 : -0x20;
    e->vy = 0;
    e->y = (game_rand(g) % 0x16) * 8 + 0x70;
    e->vx = cmp(0x100, e->x) * 2;
    e->timer = (game_rand(g) & 1) * 0x10 - 8 + e->y;
}

/* Award a kill: score, norma, explosion, and the fish's dropped item. */
static void kill_enemy(Game *g, Enemy *e, unsigned value, int chain, int hits,
                       int frame)
{
    int w = en_w(e), h = en_h(e);

    addscore(g, value);
    if (--g->st.norma == g->st.fish_at)
        g->fish_flag = 1;
    burn(g, e->x + w / 2 - 0x20, e->y + h / 2 - 0x20, frame, value, chain, hits);
    if (e->kind == 9 && g->st.kind == ST_NORMAL)
        spawn_item(g, e->x + w / 2 - 8, e->y + h / 2 - 8, 1);
    dirty(g, e->x - 8, e->y, pat_w(g, P_SUB_L) + 0x10, pat_h(g, P_SUB_L));
    e->alive = 0;
}

/* An enemy fires.  Which projectile depends on the enemy kind. */
static void enemy_fire(Game *g, Enemy *e)
{
    int i;

    for (i = 0; i < MAX_SHOT; i++) {
        Shot *s = &g->shot[i];

        if (s->frame)
            continue;
        s->frame = 1;
        s->x = e->x + 0xc;
        s->y = e->y;
        s->vx = 0;
        if (e->kind == 4) {
            sfx(g, SND_ENESHOT);
            s->kind = 2;
            s->vy = -12;
            e->timer++;
        } else if (e->kind == 2) {
            sfx(g, SND_ENESHOT);
            s->kind = 1;
            s->vy = -6;
        } else {
            s->kind = 0;
            s->vy = -2;
        }
        return;
    }
}

static void update_enemy(Game *g, int idx)
{
    Enemy *e = &g->en[idx];
    int x, y, nx, ny;

    /* --- spawning ------------------------------------------------------ */
    if (g->st.kind == ST_NORMAL && !e->alive && game_rand(g) % 300 == 1 &&
        g->st.norma > 0 && g->live_enemies < g->st.maxnum) {
        spawn_random(g, e);
    } else if (g->st.kind == ST_NORMAL && !e->alive && g->fish_flag == 1 &&
               g->st.norma > 0 && g->live_enemies < g->st.maxnum) {
        spawn_fish(g, e);
    }
    if (!e->alive)
        return;

    y = e->y;
    x = e->x;

    /* --- scripted velocity change --------------------------------------- */
    if (g->st.kind == ST_CHALLENGE && e->timer > 0 && --e->timer == 0) {
        e->vx = e->nvx;
        e->vy = e->nvy;
    }

    /* --- kind 3, the brain: idles, then homes in ------------------------- */
    if (e->kind == 3 && g->st.kind == ST_NORMAL) {
        if (++e->timer > 0x28) {
            e->alive = ((e->alive - 1) ^ 1) + 1;   /* 1 <-> 2 */
            e->timer = 0;
        }
        if (g->blink) {
            if (e->alive == 2) {
                int d = cmp(g->px + 0x10, x);
                if (y < 0x78)
                    e->vx += sgn(d * 3 - e->vx);
                else
                    e->vx += cmp(d, e->vx);
                e->vy += sgn(cmp(0x3c, y) * 3 - e->vy);
            } else {
                e->vx += sgn(-e->vx);
                e->vy += sgn(-e->vy);
            }
        }
    }

    /* --- the enemy runs the ship down ----------------------------------- */
    if (g->pstate == PS_PLAY &&
        g->px < en_w(e) - 0x10 + x && x < g->px + 0x30 &&
        g->py < en_h(e) - 4 + y && y < g->py + 0x1c)
        player_hit(g);

    /* --- kind 4, the swimmer: slows under the ship, sprints when clear --- */
    if (e->kind == 4 && g->blink && g->st.kind == ST_NORMAL) {
        int overhead = !(en_w(e) - 8 + x <= g->px || g->px + 0x38 <= x);
        int target = (e->timer < 2 && overhead) ? 2 : 6;
        e->vx += sgn(sgn(e->vx) * target - e->vx);
    }

    /* --- kind 9, the fish: bobs toward the depth it was given ----------- */
    if (e->kind == 9 && g->st.kind == ST_NORMAL)
        e->vy += cmp(e->timer, e->y);

    /* --- move, or leave the screen -------------------------------------- */
    nx = x + e->vx;
    ny = y + e->vy;
    if (nx < -0x40 || nx > 0x200 || ny < -0x20 || ny > 0x180) {
        int w = (e->kind == 9 || e->kind == 3) ? 0x20 : 0x40;
        dirty(g, x, y, w, 0x20);
        e->alive = 0;
        return;
    }
    e->y = ny;
    e->x = nx;

    /* --- shooting ------------------------------------------------------- */
    if (g->mode == MODE_PLAY && g->st.kind != ST_CHALLENGE &&
        e->kind > 0 && e->kind != 3 && e->kind < 9) {
        int fire;

        if (e->kind != 4)
            fire = game_rand(g) % 0x78 == 1;
        else
            fire = game_rand(g) % 0x1e == 1 && abs(e->vx) < 4 && e->timer < 3;
        if (fire)
            enemy_fire(g, e);
    }
}

/* ------------------------------------------------------------------ boss */

static void boss_fire(Game *g, int kind, int x, int y, int vx, int vy)
{
    int i;

    for (i = 0; i < MAX_SHOT; i++)
        if (!g->shot[i].frame) {
            g->shot[i].frame = 1;
            g->shot[i].kind = kind;
            g->shot[i].x = x;
            g->shot[i].y = y;
            g->shot[i].vx = vx;
            g->shot[i].vy = vy;
            sfx(g, SND_ENESHOT);
            return;
        }
}

static void update_boss(Game *g)
{
    int nx;

    if (g->st.kind != ST_BOSS)
        return;

    if (g->boss.state == 0) {
        g->boss.state = 1;
        g->boss.x = (game_rand(g) & 1) == 0 ? 0x200 : -0x80;
        g->boss.y = 0xd0;
        g->boss.vy = 0;
        g->boss.vx = cmp(0x100, g->boss.x) * 6;
        g->boss.dvy = -1;
        g->boss.timer = 0;
        g->boss.gunalt = 0;
        g->boss.interval = 0x23;
        g->boss.hp = 0x19;
        g->boss.boom = 0;
        g->boss.phase = 0;
    }

    if (abs(g->boss.vy) > 7)
        g->boss.dvy = -g->boss.dvy;
    nx = g->boss.vx + g->boss.x;
    g->boss.vy += g->boss.dvy;

    /* off the side: pick the next pass */
    if (nx < -0x9f || nx > 0x21f) {
        if (g->boss.hp < 0xb && game_rand(g) % 10 < 6) {
            g->boss.state = 3;
            g->boss.y = 0xc0;
            g->boss.vy = 0;
            g->boss.dvy = 0;
            g->boss.vx = sgn(g->boss.vx) * -10;
            g->boss.timer = 0;
            g->boss.interval = 500;
            g->boss.phase = 0;
        } else if (g->boss.hp < 0x15 && game_rand(g) % 10 < 6) {
            g->boss.state = 2;
            g->boss.y = 0x68;
            g->boss.vy = 0;
            g->boss.dvy = 0;
            g->boss.vx = sgn(g->boss.vx) * -12;
            g->boss.timer = 0;
            g->boss.interval = 8;
        } else {
            if (g->boss.state != 1) {
                g->boss.vy = 0;
                g->boss.dvy = -1;
                g->boss.timer = 0;
                g->boss.y = (game_rand(g) & 7) * 8 + 0xb0;
            }
            g->boss.state = 1;
            g->boss.interval = 0x19;
            g->boss.vx = sgn(g->boss.vx) * -6;
        }
    }

    if (g->boss.state == 3) {
        if (++g->boss.phase > 0xf) {
            g->boss.state = 4;
            g->boss.vy = 0;
            g->boss.vx = sgn(g->boss.vx) * 4;
            g->boss.dvy = 0;
            g->boss.timer = 0;
            g->boss.interval = 5;
            g->boss.phase = 0;
        }
    } else if (g->boss.state == 4 && ++g->boss.phase > 0x28) {
        g->boss.state = 5;
        g->boss.vx = sgn(g->boss.vx) * 16;
        g->boss.vy = 0;
        g->boss.dvy = 0;
        g->boss.timer = 0;
        g->boss.interval = 500;
        g->boss.phase = 0;
    }

    /* hit sparks along the hull */
    if (g->boss.boom > 0 && g->boss.state > 0 && g->boss.state < 9) {
        int sy = game_rand(g) % 0x30 + g->boss.y, sx;

        if (abs(g->boss.vx) < 5)
            sx = game_rand(g) % 0x70 + g->boss.x;
        else if (g->boss.vx < 0)
            sx = game_rand(g) % 0x30 + g->boss.x;
        else
            sx = game_rand(g) % 0x30 + 0x40 + g->boss.x;
        splash(g, sx, sy);
        g->boss.boom--;
    }

    /* dying */
    if (g->boss.state == 9) {
        if (++g->boss.timer >= g->boss.interval) {
            burn(g, g->boss.x,        g->boss.y - 0x18, 1, 0,     1, 1);
            burn(g, g->boss.x,        g->boss.y + 0x18, 1, 0,     1, 1);
            burn(g, g->boss.x + 0x20, g->boss.y,        1, 30000, 1, 1);
            burn(g, g->boss.x + 0x40, g->boss.y - 0x18, 1, 0,     1, 1);
            burn(g, g->boss.x + 0x40, g->boss.y + 0x18, 1, 0,     1, 1);
            addscore(g, 30000);
            g->boss.state = 10;
        }
        splash(g, game_rand(g) % 0x90 - 0x10 + g->boss.x,
                  (game_rand(g) & 0x3f) - 8 + g->boss.y);
        if (game_rand(g) % 5 == 0 && g->boss.timer < g->boss.interval) {
            int by = (game_rand(g) & 0x3f) - 0x20 + g->boss.y;
            burn(g, (game_rand(g) & 0x7f) - 0x20 + g->boss.x, by, 1, 0, 1, 1);
        }
    }

    if (g->mode != MODE_TITLE) {
        g->boss.y += g->boss.vy;
        g->boss.x += g->boss.vx;
    }

    /* fire on the interval */
    if (++g->boss.timer >= g->boss.interval) {
        int st = g->boss.state, vx = g->boss.vx, bx = g->boss.x;

        if (st > 0 && st < 9)
            g->boss.timer = 0;
        if ((vx < 0 && bx >= 0) || (vx > 0 && bx < 0x181)) {
            if (st == 1) {
                boss_fire(g, 1, vx < 0 ? bx + 0xb : bx + 0x53,
                          g->boss.y + 0x14, sgn(vx) * 6, -6);
                boss_fire(g, 1, vx < 0 ? bx + 0x1d : bx + 0x65,
                          g->boss.y + 0x14, sgn(vx) * 6, -6);
            } else if (st == 2) {
                boss_fire(g, 0, bx + 0x38, g->boss.y + 0xc, 0, -3);
            } else if (st == 4) {
                int sx = bx + 0xb + sgn(g->boss.gunalt) * 0x12;

                g->boss.gunalt ^= 1;
                if (vx >= 0)
                    sx += 0x36;
                boss_fire(g, 3, sx, g->boss.y + 0x14, sgn(vx) * 16, -16);
            }
        }
    }
}

/* ------------------------------------------------------------ explosions */

static void update_burns(Game *g)
{
    int i, j;

    for (i = 0; i < MAX_BURN; i++) {
        Burn *b = &g->burn[i];
        int bx, by;

        if (b->frame <= 0)
            continue;
        bx = b->x;
        by = b->y;
        if (g->blink)
            b->frame++;
        if (b->frame > 8) {
            dirty(g, bx, by, pat_w(g, P_BURN1 + 7), pat_h(g, P_BURN1 + 7));
            b->frame = 0;
        }
        if (b->frame <= 1 || b->frame >= 6)
            continue;

        for (j = 0; j < MAX_ENEMY; j++) {
            Enemy *e = &g->en[j];

            if (!e->alive)
                continue;
            if (e->x < bx + 0x38 && bx < e->x + en_w(e) - 8 &&
                e->y < by + 0x38 && by < e->y + en_h(e) - 8 &&
                e->x > -0x20 && e->x < 0x1e0 &&
                e->y > -0x20 && e->y < 0x19e) {
                unsigned value;

                if (b->chain < 10)
                    b->chain++;
                b->hits++;
                if (g->maxchain < b->hits)
                    g->maxchain = b->hits;
                value = (unsigned)(e->kind + 1) * (unsigned)b->chain * 10u;
                kill_enemy(g, e, value, b->chain, b->hits, -1);
            }
            /* The original tests the ship inside this loop, so it only runs
             * while at least one enemy is still alive.  Kept as-is. */
            if (g->pstate == PS_PLAY &&
                g->px < bx + 0x28 && bx < g->px + 0x28 &&
                g->py < by + 0x38 && by < g->py + 0x1c)
                player_hit(g);
        }
    }
    for (i = 0; i < MAX_BURN; i++)
        if (g->burn[i].frame == -1)
            g->burn[i].frame = 1;
}

/* ----------------------------------------------------------------- bombs */

static void update_bombs(Game *g)
{
    int i, j;

    /* animate, and count how many of the ship's four charges are free */
    g->bombs_ready = 0;
    for (i = 0; i < MAX_BOMB; i++) {
        if (!g->bomb[i].frame) {
            if (i < PLAYER_BOMBS)
                g->bombs_ready++;
        } else if (g->blink && ++g->bomb[i].frame > 4) {
            g->bomb[i].frame = 1;
        }
    }

    for (i = 0; i < MAX_BOMB; i++) {
        Bomb *b = &g->bomb[i];
        int x, y;

        if (!b->frame)
            continue;
        x = b->x;
        y = b->y;
        if (y + b->vy > 0x180) {
            dirty(g, x, y - 8, pat_w(g, P_BOMB1), pat_h(g, P_BOMB1) + 8);
            b->frame = 0;
        }

        for (j = 0; j < MAX_ENEMY; j++) {
            Enemy *e = &g->en[j];

            if (!e->alive)
                continue;
            if (e->x < x + 8 && x < e->x + en_w(e) - 8 &&
                e->y < y + 8 && y < e->y + en_h(e) - 8) {
                dirty(g, x, y - 8, pat_w(g, P_BOMB1), pat_h(g, P_BOMB1) + 8);
                b->frame = 0;
                kill_enemy(g, e, (unsigned)(e->kind + 1) * 10u, 1, 1, 1);
                break;
            }
        }

        if (g->boss.state > 0 && g->boss.state < 9 &&
            g->boss.x < x + 8 && x < g->boss.x + 0x78 &&
            g->boss.y + 8 < y + 8 && y < g->boss.y + 0x36) {
            if (--g->boss.hp > 0) {
                addscore(g, 500);
                burn(g, x - 0x18, y - 0x18, 1, 500, 1, 1);
                dirty(g, x, y - 8, pat_w(g, P_BOMB1), pat_h(g, P_BOMB1) + 8);
                b->frame = 0;
                g->boss.boom = 5;
            } else {
                g->boss.lastvx = g->boss.vx;
                g->boss.dvy = 0;
                g->boss.vy = 0;
                g->boss.vx = 0;
                g->boss.state = 9;
                g->boss.timer = 0;
                g->boss.interval = 0xb4;
                burn(g, g->boss.x + 0x20, g->boss.y, 1, 0, 1, 1);
                dirty(g, x, y - 8, pat_w(g, P_BOMB1), pat_h(g, P_BOMB1) + 8);
                b->frame = 0;
            }
        }
        b->y += b->vy;
    }
}

/* ----------------------------------------------------------------- shots */

static void update_shots(Game *g)
{
    int i;

    for (i = 0; i < MAX_SHOT; i++)
        if (g->shot[i].frame && ++g->shot[i].frame > 3)
            g->shot[i].frame = 1;

    for (i = 0; i < MAX_SHOT; i++) {
        Shot *s = &g->shot[i];
        int x, y, kind, cx, cy;

        if (!s->frame)
            continue;
        kind = s->kind;
        x = s->x;
        y = s->y;

        if ((kind == 0 && s->vy + y < 0x36) || (kind == 1 && s->vy + y < -0x10)) {
            dirty(g, x - 6, y, pat_w(g, P_CRACK1) + 0xc, pat_h(g, P_CRACK1) + 8);
            s->frame = 0;
            if (kind == 0)
                splash(g, x, y);
        } else if ((kind == 2 && s->vy + y < -0x1f) ||
                   (kind == 3 && (s->vy + y < -0x1f || s->vx + x < -0x1f ||
                                  s->vx + x > 0x1ff))) {
            int p = kind == 2 ? P_FIRE1 : P_FIRE2_L;

            dirty(g, x, y, pat_w(g, p), pat_h(g, p) + 0x10);
            s->frame = 0;
        }

        /* the missile leans toward the ship while it is still deep */
        if (kind == 1) {
            if (y < 0xb5 && (g->st.kind != ST_BOSS || y < 0x79))
                s->vx -= sgn(s->vx);
            else
                s->vx += cmp(g->px, x - 0x18);
            if (abs(s->vx) > 6)
                s->vx = sgn(s->vx) * 6;
        }

        cx = x;
        cy = y;
        if (kind == 3) {
            cx += 8;
            cy += 8;
        }
        if (g->pstate == PS_PLAY && g->px < cx && cx < g->px + 0x30 &&
            g->py < cy + 8 && cy < g->py + 0x18) {
            if (kind == 2)
                dirty(g, cx, cy, pat_w(g, P_FIRE1), pat_h(g, P_FIRE1) + 0x10);
            else if (kind == 3)
                dirty(g, s->x - 0x10, s->y, pat_w(g, P_FIRE2_L) + 0x20,
                      pat_h(g, P_FIRE2_L) + 0x10);
            else
                dirty(g, cx - 6, cy, pat_w(g, P_CRACK1) + 0xc,
                      pat_h(g, P_CRACK1) + 8);
            s->frame = 0;
            player_hit(g);
        }

        s->x += s->vx;
        s->y += s->vy;
    }
}

/* ----------------------------------------------------------------- items */

static void update_items(Game *g)
{
    int i;

    for (i = 0; i < MAX_ITEM; i++) {
        Item *it = &g->item[i];
        int x, y;

        if (!it->t)
            continue;
        y = it->y;
        x = it->x;
        if (y + it->vy < 0x36) {
            it->y = y + it->vy;
            it->vy = 0;                 /* parked just under the surface */
            if (++it->t > 100) {
                dirty(g, x, y, pat_w(g, P_ITEM1), pat_h(g, P_ITEM1) + 8);
                it->t = 0;
                continue;
            }
        }
        if (g->pstate == PS_PLAY && g->px < x + 8 && x < g->px + 0x38 &&
            g->py < y && y < g->py + 0x18) {
            sfx(g, SND_ITEM);
            dirty(g, x, y, pat_w(g, P_ITEM1), pat_h(g, P_ITEM1) + 8);
            it->t = 0;
            if (!g->plane) {
                sfx(g, SND_PLANE);
                g->plane = 1;
                g->planex = -0x40;
                g->planey = 10;
                g->planevx = 6;
                g->planetimer = 0;
            }
            continue;
        }
        it->x += it->vx;
        it->y += it->vy;
    }
}

/* ----------------------------------------------------------------- plane */

static void update_plane(Game *g)
{
    int nx, k;

    if (!g->plane)
        return;
    nx = g->planex + g->planevx;
    if (nx < -0x40 || nx > 0x200) {
        dirty(g, g->planex, g->planey, 0x40, 0x20);
        g->plane = 0;
        return;
    }
    g->planex = nx;
    if (++g->planetimer <= 6)
        return;
    g->planetimer = 0;
    for (k = PLAYER_BOMBS; k < MAX_BOMB; k++)
        if (!g->bomb[k].frame) {
            g->bomb[k].x = nx + 0x18;
            g->bomb[k].y = g->planey + 0x10;
            g->bomb[k].vy = 5;
            g->bomb[k].frame = 1;
            return;
        }
}

/* ---------------------------------------------------------------- player */

static void drop_charge(Game *g, int x)
{
    int k;

    for (k = 0; k < PLAYER_BOMBS; k++)
        if (!g->bomb[k].frame) {
            sfx(g, SND_DROP);
            g->bomb[k].x = x;
            g->bomb[k].y = g->py;
            g->bomb[k].vy = 3;
            g->bomb[k].frame = 1;
            return;
        }
}

static void update_player(Game *g)
{
    int b1, b2;

    if (g->mode != MODE_PLAY ||
        (g->pstate != PS_PLAY && g->pstate != PS_CLEAR))
        return;

    g->pvx = 0;
    if (!g->use_mouse) {
        if (g->pad & PAD_LEFT)
            g->pvx = -4;
        if (g->pad & PAD_RIGHT)
            g->pvx = 4;
        b1 = (g->pad & PAD_BTN1) != 0;
        b2 = (g->pad & PAD_BTN2) != 0;
    } else {
        if (g->mouse_in) {
            if (g->mx < g->px)
                g->pvx = -4;
            else if (g->mx > g->px + 0x40)
                g->pvx = 4;
        }
        b1 = g->mbl;
        b2 = g->mbr;
    }
    if (g->px + g->pvx < 0 || g->px + g->pvx > 0x1c0)
        g->pvx = 0;
    g->px += g->pvx;

    if (g->trig1 && b1) {
        drop_charge(g, g->px);
        g->trig1 = 0;
    }
    if (g->trig2 && b2) {
        drop_charge(g, g->px + 0x30);
        g->trig2 = 0;
    }
    if (!b1)
        g->trig1 = 1;
    if (!b2)
        g->trig2 = 1;
}

/* ------------------------------------------------------------- draw pass */

static void draw_enemy(Game *g, Enemy *e)
{
    int pat, sizepat, extra = 0;

    if (e->vx)
        e->face = e->vx;
    switch (e->kind) {
    case 0: pat = e->face < 0 ? P_NSUB_L : P_NSUB_R;  sizepat = P_SUB_L;  break;
    case 1: pat = e->face < 0 ? P_SUB_L  : P_SUB_R;   sizepat = P_SUB_L;  break;
    case 2: pat = e->face < 0 ? P_GOLD_L : P_GOLD_R;  sizepat = P_SUB_L;  break;
    case 3: pat = P_BRAIN1 + e->frame;                sizepat = P_BRAIN1;
            extra = 0x10; break;
    case 4: pat = (e->face < 0 ? P_SWIM_L1 : P_SWIM_R1) + e->frame;
            sizepat = P_SUB_L; break;
    case 9: pat = e->face < 0 ? P_FISH_L : P_FISH_R;  sizepat = P_FISH_L;
            extra = 0x10; break;
    default: return;
    }
    vid_pat(g->v, e->x, e->y, pat);
    if (e->kind == 9)
        dirty(g, e->x - 8, e->y - 8, pat_w(g, sizepat) + 0x10,
              pat_h(g, sizepat) + 0x10);
    else
        dirty(g, e->x - 8, e->y, pat_w(g, sizepat) + 0x10,
              pat_h(g, sizepat) + extra);
}

static void draw_boss(Game *g)
{
    int st = g->boss.state;

    if (st <= 0 || st >= 10)
        return;
    if (st < 9)
        g->boss.lastvx = g->boss.vx;
    if (g->boss.lastvx < 0) {
        if (g->boss.x > -0x40) {
            vid_pat(g->v, g->boss.x, g->boss.y, P_BOSS_L1);
            dirty(g, g->boss.x - 0x10, g->boss.y - 8,
                  pat_w(g, P_BOSS_L1) + 0x20, pat_h(g, P_BOSS_L1) + 0x10);
        }
        if (g->boss.x < 0x1c0) {
            vid_pat(g->v, g->boss.x + 0x40, g->boss.y, P_BOSS_L2);
            dirty(g, g->boss.x + 0x30, g->boss.y - 8,
                  pat_w(g, P_BOSS_L2) + 0x20, pat_h(g, P_BOSS_L2) + 0x10);
        }
    } else {
        if (g->boss.x > -0x40) {
            vid_pat(g->v, g->boss.x, g->boss.y, P_BOSS_R2);
            dirty(g, g->boss.x - 0x10, g->boss.y - 8,
                  pat_w(g, P_BOSS_R2) + 0x20, pat_h(g, P_BOSS_R2) + 0x10);
        }
        if (g->boss.x < 0x1c0) {
            vid_pat(g->v, g->boss.x + 0x40, g->boss.y, P_BOSS_R1);
            dirty(g, g->boss.x + 0x30, g->boss.y - 8,
                  pat_w(g, P_BOSS_R1) + 0x20, pat_h(g, P_BOSS_R1) + 0x10);
        }
    }
}

/* The bomb sprite for animation frames 1..4: 1,2,1,3 (table at 0x00421248). */
static const int bomb_frame[5] = { 0, 0, 1, 0, 2 };

static void draw_objects(Game *g)
{
    int i;

    /* ship */
    if (g->mode == MODE_TITLE ||
        (g->pstate != PS_RESPAWN && g->pstate != PS_GAMEOVER))
        vid_pat(g->v, g->px, g->py, P_SHIP);
    dirty(g, g->px - 8, g->py, pat_w(g, P_SHIP) + 0x10, pat_h(g, P_SHIP));

    /* the charges the ship still has, as icons beside the HUD */
    for (i = 0; i < g->bombs_ready; i++)
        vid_pat(g->v, 0xe0 + i * 0x10, 0x16,
                P_BOMB1 + bomb_frame[g->bomb_icon]);
    dirty(g, 0xe0, 0x16, pat_w(g, P_BOMB1) * 4, pat_h(g, P_BOMB1));

    if (g->plane) {
        vid_pat(g->v, g->planex, g->planey, P_PLANE);
        dirty(g, g->planex - 8, g->planey,
              pat_w(g, P_PLANE) + 0x10, pat_h(g, P_PLANE));
    }

    for (i = 0; i < MAX_ENEMY; i++)
        if (g->en[i].alive)
            draw_enemy(g, &g->en[i]);

    draw_boss(g);

    for (i = 0; i < MAX_BOMB; i++) {
        Bomb *b = &g->bomb[i];
        int base = i < PLAYER_BOMBS ? P_BOMB1 : P_PBOMB1;

        if (!b->frame)
            continue;
        vid_pat(g->v, b->x, b->y, base + bomb_frame[b->frame]);
        dirty(g, b->x, b->y - 8, pat_w(g, P_BOMB1), pat_h(g, P_BOMB1) + 8);
    }

    for (i = 0; i < MAX_SHOT; i++) {
        Shot *s = &g->shot[i];
        int dx = s->x, dw, dh;

        if (!s->frame)
            continue;
        if (s->kind == 0) {
            vid_pat(g->v, s->x, s->y, P_CRACK1 + s->frame - 1);
            dw = pat_w(g, P_CRACK1);
            dh = pat_h(g, P_CRACK1) + 8;
        } else if (s->kind == 1) {
            int a = abs(s->vx), p;

            if (!s->vx)
                p = P_MISSLE0;
            else
                p = ((a > 2) + (a > 4)) * 2 + (s->vx < 0 ? P_MISSLE0 + 1
                                                         : P_MISSLE0 + 2);
            vid_pat(g->v, s->x, s->y, p);
            dx = s->x - 6;
            dw = pat_w(g, P_CRACK1) + 0xc;
            dh = pat_h(g, P_CRACK1) + 8;
        } else if (s->kind == 2) {
            vid_pat(g->v, s->x, s->y, P_FIRE1);
            dw = pat_w(g, P_FIRE1);
            dh = pat_h(g, P_FIRE1) + 0x10;
        } else {
            vid_pat(g->v, s->x, s->y, s->vx < 0 ? P_FIRE2_L : P_FIRE2_R);
            dx = s->x - 0x10;
            dw = pat_w(g, P_FIRE2_L) + 0x20;
            dh = pat_h(g, P_FIRE2_L) + 0x10;
        }
        dirty(g, dx, s->y, dw, dh);
    }

    for (i = 0; i < MAX_ITEM; i++) {
        Item *it = &g->item[i];

        if (!it->t)
            continue;
        if (it->t < 0x3c || g->blink)
            vid_pat(g->v, it->x, it->y, P_ITEM1);
        dirty(g, it->x, it->y, pat_w(g, P_ITEM1), pat_h(g, P_ITEM1) + 8);
    }

    for (i = 0; i < MAX_POPUP; i++) {
        Popup *p = &g->pop[i];

        if (p->t <= 0)
            continue;
        if (p->chain < 2)
            num(g, p->x, p->y, p->value, "%lu", 2);
        else
            num_chain(g, p->x, p->y, p->value, p->chain);
    }

    for (i = 0; i < MAX_BURN; i++) {
        Burn *b = &g->burn[i];

        if (b->frame <= 0)
            continue;
        vid_pat(g->v, b->x, b->y, P_BURN1 + b->frame - 1);
        dirty(g, b->x, b->y, pat_w(g, P_BURN1), pat_h(g, P_BURN1));
    }

    for (i = 0; i < MAX_SPLASH; i++) {
        Splash *s = &g->spl[i];

        if (s->frame <= 0)
            continue;
        vid_pat(g->v, s->x, s->y, P_SBURN1 + s->frame - 1);
        dirty(g, s->x, s->y, pat_w(g, P_SBURN1), pat_h(g, P_SBURN1));
    }
}

static void draw_hud(Game *g)
{
    vid_pat(g->v, 8, 2, P_SCORE);
    dirty(g, 8, 2, 0x28, 0x10);
    num(g, 0x36, 2, g->score, "%08lu", 2);

    if (g->hiscore < g->score)
        g->hiscore = g->score;
    vid_pat(g->v, 0xde, 2, P_HISCORE);
    dirty(g, 0xde, 2, 0x38, 0x10);
    num(g, 0x11c, 2, g->hiscore, "%08lu", 2);

    vid_pat(g->v, 0x1cc, 2, P_LEFT);
    dirty(g, 0x1cc, 2, 0x20, 0x10);
    num(g, 500, 2, (unsigned)g->lives, "%lu", 2);
}

static void draw_overlays(Game *g)
{
    if (g->mode != MODE_PLAY)
        return;

    if (g->pstate == PS_PLAY) {
        if (g->stimer < 0x32) {
            if (g->st.kind == ST_NORMAL || g->st.kind == ST_BOSS) {
                vid_pat(g->v, 0xe4, 0xb4, P_STAGE);
                num(g, 0x114, 0xb4, (unsigned)g->stageno, "%lu", 2);
            } else if (g->st.kind == ST_CHALLENGE) {
                vid_pat(g->v, 0xb4, 0xb4, P_CHALLENG);
                vid_pat(g->v, 0x114, 0xb4, P_STAGE);
                num(g, 0x144, 0xb4, (unsigned)g->stageno, "%lu", 2);
            }
            vid_pat(g->v, 0xec, 0xdc, P_READY);
        }
        if (g->stimer == 0x32)
            dirty(g, g->st.kind == ST_CHALLENGE ? 0x144 : 0x114, 0xb4, 0x28, 0x10);
        if (g->stimer < 0x33) {
            if (g->st.kind == ST_CHALLENGE)
                dirty(g, 0xb4, 0xb4, 0x58, 0x10);
            dirty(g, g->st.kind == ST_CHALLENGE ? 0x114 : 0xe4, 0xb4, 0x28, 0x10);
            dirty(g, 0xec, 0xdc, 0x28, 0x10);
        }
    }
    if (g->pstate == PS_CLEAR) {
        vid_pat(g->v, 0xe8, 200, P_CLEAR);
        dirty(g, 0xe8, 200, 0x30, 0x10);
        if (g->st.kind == ST_CHALLENGE) {
            vid_pat(g->v, 0xd4, 0xf0, P_TOTAL);
            dirty(g, 0xd4, 0xf0, 0x28, 0x10);
            num(g, 0x104, 0xf0, g->stagescore, "%lu", 2);
        }
    }
    if (g->pstate == PS_GAMEOVER) {
        vid_pat(g->v, 0xdc, 200, P_GAMEOVER);
        dirty(g, 0xdc, 200, 0x48, 0x10);
    }
}

/* --------------------------------------------------------- state changes */

/* When the BGM starts.
 *
 * The original starts it only when a game starts.  FUN_00403030 - open the
 * sequencer and play - has exactly two callers: FUN_00404780, i.e. "Start", for
 * windepth.mid, and FUN_00404040, the name entry, for nameinn.mid.  Neither the
 * Bio_100% splash (FUN_00403e30) nor the title screen (FUN_00403f90) touches
 * it, and the splash tick plays no effect either, so both are silent.  It also
 * refuses to restart a tune that is already running, and the stop
 * (FUN_00403080) happens on game over, on the name entry timing out, and when
 * the window is deactivated - never per stage.  So one pass of windepth.mid
 * starts with the run and loops until it ends.
 *
 * Set this to 1 to play the tune over the attract screens as well.  It is off
 * because starting the music with the run is what makes it line up with what is
 * on screen; with it on, pressing Start drops you into the middle of a tune that
 * has been looping since launch. */
#define BGM_FROM_TITLE 0

/* FUN_00403d80 - the Bio_100% splash, which is also where a finished game
 * returns to.  It is not just a one-off intro. */
void game_logo(Game *g)
{
    g->mode = MODE_LOGO;
    g->pstate = PS_NONE;
    g->logofade = 0x28;
    g->stimer = 0;
    g->stageno = 0;
    clear_objects(g);
    g->lives = 0;
    g->px = 0xe0;
    load_stage(g);
    vid_dirty_all(g->v);
    wd_music(0, BGM_FROM_TITLE ? g->bgm_on : 0);
}

void game_title(Game *g)
{
    g->mode = MODE_TITLE;
    g->pstate = PS_NONE;
    g->stageno = 0;
    g->cur_entry = -1;
    clear_objects(g);
    g->hiscore = g->top[0].score;
    load_stage(g);
    vid_dirty_all(g->v);
    g->lives = 0;
    g->blink = 0;
    g->stimer = 0;
    wd_music(0, BGM_FROM_TITLE ? g->bgm_on : 0);
}

void game_start(Game *g)
{
    g->maxchain = 0;
    g->stagescore = 0;
    g->score = 0;
    g->fallnum = 1;
    g->hiscore = g->top[0].score;
    g->lives = 4;
    g->mode = MODE_PLAY;
    g->pstate = PS_PLAY;
    g->stimer = 0;
    g->stageno = 1;
    g->cur_entry = -1;
    g->px = 0xe0;
    clear_objects(g);
    load_stage(g);
    vid_dirty_all(g->v);
    if (g->sfx_on)
        wd_sound(SND_START, 0);
    wd_music(0, g->bgm_on);
}

/* FUN_00404040 - the run is over and the score made the table. */
static void enter_name(Game *g)
{
    int i = 0, k;

    wd_music(1, g->bgm_on);
    g->mode = MODE_NAME;
    g->pstate = PS_PLAY;
    clear_objects(g);
    g->name_done = 0;
    g->name_timer = 0;
    g->name_col = 0x1a;
    g->name_row = 2;
    g->name_len = 0;
    g->name_idx = 0;

    while (i < MAX_SCORES && g->score <= g->top[i].score)
        i++;
    if (i >= MAX_SCORES) {
        g->cur_entry = -1;
        return;
    }
    for (k = MAX_SCORES - 1; k > i; k--)
        g->top[k] = g->top[k - 1];
    memset(&g->top[i], 0, sizeof g->top[i]);
    g->top[i].score = g->score;
    g->top[i].level = g->stageno;
    g->top[i].maxhit = g->maxchain;
    g->top[i].fallnum = g->fallnum;
    strcpy(g->top[i].date, "00/00/00");
    g->cur_entry = i;
    g->name_idx = (i == 0) ? 1 : 0;
}

/* ------------------------------------------------------------- the frame */

static void tick_play_state(Game *g)
{
    /* stage-clear conditions */
    if (g->st.kind == ST_NORMAL && g->st.norma < 1 && g->pstate == PS_PLAY &&
        g->live_enemies < 1) {
        g->pstate = PS_CLEAR;
        g->stimer = 0;
    }
    if (g->st.kind == ST_BOSS && g->boss.state == 10 && g->pstate == PS_PLAY) {
        g->pstate = PS_CLEAR;
        g->stimer = 0;
    }
    if (g->st.kind == ST_CHALLENGE && g->st.nchal < 1 && g->pstate == PS_PLAY &&
        g->live_enemies < 1) {
        g->pstate = PS_CLEAR;
        g->stimer = 0;
    }

    if (g->pstate == PS_RESPAWN) {
        if (g->stimer > 0x14) {
            g->pstate = PS_PLAY;
            clear_objects(g);
            vid_dirty_all(g->v);
            g->stimer = 0;
        }
    } else if (g->pstate == PS_GAMEOVER) {
        if (g->stimer > 0x50) {
            wd_music(0, 0);
            if (g->score <= g->top[MAX_SCORES - 1].score)
                game_logo(g);
            else
                enter_name(g);
        }
    } else if (g->pstate == PS_CLEAR && g->stimer > 0x28) {
        g->pstate = PS_PLAY;
        g->stagescore = 0;
        if (++g->stageno > 0x14) {
            addscore(g, 1);
            g->stageno = 1;
        }
        clear_objects(g);
        load_stage(g);
        vid_dirty_all(g->v);
        g->stimer = 0;
    }
}

/* Draw the whole frame - the original does this inline at the end of the tick,
 * sharing the object arrays it has just updated. */
static void draw_frame(Game *g)
{
    draw_background(g);
    draw_objects(g);
    draw_hud(g);
    draw_overlays(g);

    if (g->mode == MODE_TITLE) {
        if (g->stimer < 1) {
            draw_scores(g);
        } else {
            vid_pat_wave(g->v, 0x18, 0xa0, P_FIREWALL, 0x40, 4, g->wavephase);
            vid_pat(g->v, 0x14, 0xa0, P_WDLOGO);
            dirty(g, 0x14, 0xa0, pat_w(g, P_WDLOGO), pat_h(g, P_WDLOGO));
        }
    }
    g->wavephase += 0x10;
}

/* FUN_00403e30 - the Bio_100% splash, a rippling logo that settles. */
static void tick_logo(Game *g)
{
    int w = pat_w(g, P_BIOLOGO), h = pat_h(g, P_BIOLOGO);
    int x = (SCR_W - w) / 2, y = 0x8c;

    g->blink ^= 1;
    if (g->stimer > 0x78) {
        game_title(g);
        return;
    }
    g->wavephase += 8;
    if (g->blink && g->logofade > 0)
        g->logofade--;
    draw_logoback(g);
    vid_pat_wave(g->v, x, y, P_BIOLOGO, -0x40, g->logofade, g->wavephase);
    dirty(g, x - g->logofade, y, g->logofade * 2 + w, h);
}

/* FUN_004041e0 - the high-score name entry screen. */
static void tick_name(Game *g)
{
    static const char *rows[2] = {
        " !\"#$%&'()*+,-./0123456789:;<=>?",
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    };
    ScoreEntry *e = (g->cur_entry >= 0) ? &g->top[g->cur_entry] : 0;
    unsigned newpad = g->pad & ~g->padprev;
    int i;

    g->blink ^= 1;
    if (g->name_done) {
        g->name_timer -= 0x32;
        if (g->name_timer < 0) {
            wd_music(0, 0);
            game_logo(g);
            return;
        }
    }

    if (!g->name_done && e) {
        if (newpad & PAD_LEFT)  g->name_col--;
        if (newpad & PAD_RIGHT) g->name_col++;
        if (newpad & PAD_UP)    g->name_row--;
        if (newpad & PAD_DOWN)  g->name_row++;
        if (g->name_col < 0)    g->name_col = 0x1f;
        if (g->name_col > 0x1f) g->name_col = 0;
        if (g->name_row < 0)    g->name_row = 2;
        if (g->name_row > 2)    g->name_row = 0;
        if (newpad & (PAD_BTN1 | PAD_BTN2)) {
            int c = 0;

            if (g->name_row < 2)
                c = rows[g->name_row][g->name_col & 0x1f];
            else if (g->name_col == 0x16)
                c = -3;                     /* DEL */
            else if (g->name_col == 0x1a)
                c = -2;                     /* DUP */
            else if (g->name_col == 0x1e)
                c = -1;                     /* END */

            if (c > 0) {
                if (g->name_len < 8) {
                    e->name[g->name_len++] = (char)c;
                    e->name[g->name_len] = 0;
                }
                if (g->name_len >= 8) {
                    g->name_col = 0x1e;
                    g->name_row = 2;
                }
            } else if (c == -3) {
                if (g->name_len > 0)
                    e->name[--g->name_len] = 0;
            } else if (c == -2) {
                /* DUP reaches the row being edited too; copying that onto
                 * itself is a no-op, and an overlapping strncpy is undefined */
                if (&g->top[g->name_idx] != e)
                    strncpy(e->name, g->top[g->name_idx].name, sizeof e->name - 1);
                e->name[sizeof e->name - 1] = 0;
                g->name_len = (int)strlen(e->name);
                if (++g->name_idx >= MAX_SCORES)
                    g->name_idx = 0;
            } else if (c == -1) {
                g->name_done = 1;
                g->name_timer = 3000;
            }
        }
    }

    draw_background(g);
    draw_hud(g);
    draw_scores(g);
    vid_text_wide(g->v, 0x40, 0x134, rows[0], 4);
    vid_text_wide(g->v, 0x40, 0x14c, rows[1], 4);
    vid_text(g->v, 0x140, 0x164, "DEL", 3);
    vid_text(g->v, 0x170, 0x164, "DUP", 3);
    vid_text(g->v, 0x1a0, 0x164, "END", 3);
    dirty(g, 0x40, 0x138, 0x18c, 0x10);
    dirty(g, 0x40, 0x150, 0x18c, 0x10);
    dirty(g, 0x40, 0x168, 0x18c, 0x10);

    if (!g->name_done) {
        int cx = g->name_col * 0xc + 0x40;
        int cy = g->name_row * 0x18 + 0x134;

        if (g->blink) {
            vid_pat(g->v, cx, cy + 3, P_FONT1 + ('_' - ' '));
            vid_pat(g->v, cx, cy + 4, P_FONT1 + ('_' - ' '));
        }
        dirty(g, cx, cy + 3, 8, 0x10);
        if (e) {
            if (g->blink)
                vid_text(g->v, g->name_len * 8 + 0x130,
                         g->cur_entry * 0x14 + 0x66, "_", 4);
            dirty(g, 0x130, g->cur_entry * 0x14 + 0x66, 0x58, 0x10);
        }
    }
    for (i = 0; i < MAX_SPLASH; i++)
        if (g->spl[i].frame > 0) {
            vid_pat(g->v, g->spl[i].x, g->spl[i].y,
                    P_SBURN1 + g->spl[i].frame - 1);
            dirty(g, g->spl[i].x, g->spl[i].y,
                  pat_w(g, P_SBURN1), pat_h(g, P_SBURN1));
        }
}

void game_tick(Game *g)
{
    int i;

    g->stimer++;

    if (g->mode == MODE_LOGO) {
        tick_logo(g);
        return;
    }
    if (g->mode == MODE_NAME) {
        tick_name(g);
        g->padprev = g->pad;
        return;
    }

    g->blink ^= 1;

    if (g->mode == MODE_PLAY) {
        tick_play_state(g);
        if (g->mode != MODE_PLAY) {      /* game over rolled us elsewhere */
            g->padprev = g->pad;
            return;
        }
    } else if (g->mode == MODE_TITLE) {
        if (g->stimer > 0x78) {
            g->stimer = -0x78;
            vid_dirty_all(g->v);
        }
        if (g->stimer == 1)
            vid_dirty_all(g->v);
    }

    g->padprev = g->pad;

    update_player(g);
    update_plane(g);

    /* live enemy count, and the two animated kinds */
    g->live_enemies = 0;
    for (i = 0; i < MAX_ENEMY; i++) {
        Enemy *e = &g->en[i];

        if (!e->alive)
            continue;
        g->live_enemies++;
        if (e->kind == 3 && g->blink && ++e->frame > 1)
            e->frame = 0;
        if (e->kind == 4 && g->blink && ++e->frame > 2)
            e->frame = 0;
    }

    /* scripted spawns fire on an exact frame number */
    if (g->st.kind == ST_CHALLENGE)
        for (i = 0; i < MAX_CHAL; i++)
            if (g->st.chal[i].time > 0 && g->stimer == g->st.chal[i].time) {
                spawn_scripted(g, &g->st.chal[i]);
                g->st.chal[i].time = 0;
            }

    if (g->st.kind == ST_NORMAL || g->st.kind == ST_CHALLENGE)
        for (i = 0; i < MAX_ENEMY; i++)
            update_enemy(g, i);

    update_boss(g);
    update_burns(g);

    for (i = 0; i < MAX_SPLASH; i++)
        if (g->spl[i].frame > 0) {
            if (g->blink)
                g->spl[i].frame++;
            if (g->spl[i].frame > 4) {
                dirty(g, g->spl[i].x, g->spl[i].y,
                      pat_w(g, P_SBURN1 + 3), pat_h(g, P_SBURN1 + 3));
                g->spl[i].frame = 0;
            }
        }

    if (g->blink && ++g->bomb_icon > 4)
        g->bomb_icon = 1;

    update_bombs(g);
    update_shots(g);
    update_items(g);

    for (i = 0; i < MAX_POPUP; i++)
        if (g->pop[i].t && --g->pop[i].t < 1)
            dirty(g, g->pop[i].x, g->pop[i].y, g->pop[i].ndig << 3, 0x10);

    draw_frame(g);
}

void game_init(Game *g, Video *v, const Dar *dar, const char *datpath)
{
    int i;

    memset(g, 0, sizeof *g);
    g->v = v;
    g->dar = dar;
    strncpy(g->datpath, datpath, sizeof g->datpath - 1);
    g->rnd = 1;
    g->sfx_on = 1;
    g->bgm_on = 1;
    g->use_mouse = 0;
    g->py = 0x26;
    g->px = 0xe0;
    g->bomb_icon = 1;
    g->trig1 = g->trig2 = 1;
    g->cur_entry = -1;
    g->logofade = 0x28;

    /* The default table the original writes when windepth.ini has no scores
     * (FUN_004037e0). */
    for (i = 0; i < MAX_SCORES; i++) {
        g->top[i].score = 3000;
        g->top[i].level = 8;
        g->top[i].maxhit = 10;
        g->top[i].fallnum = 1;
        g->top[i].sent = 1;
        strcpy(g->top[i].name, "BIO_100%");
        strcpy(g->top[i].date, "97/09/28");
    }
    g->hiscore = g->top[0].score;

    /* The original's app constructor ends by calling FUN_00403d80, so start
     * where it starts: on the splash. */
    game_logo(g);
}
