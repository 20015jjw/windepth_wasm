#include "synth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WAVE_LEN  1024
#define WAVE_MASK (WAVE_LEN - 1)
#define NMIP      3           /* band-limited variants per timbre */

enum {
    W_SINE, W_TRI, W_SAW, W_SQUARE, W_REED, W_PLUCK, W_ORGAN, W_COUNT
};

static float g_wave[W_COUNT][NMIP][WAVE_LEN];
static int   g_tables_ready;

/* Harmonic amplitude for timbre `w` at harmonic n (1 = fundamental). */
static float harmonic(int w, int n)
{
    switch (w) {
    case W_SINE:   return n == 1 ? 1.0f : 0.0f;
    case W_TRI:    return (n & 1) ? (((n / 2) & 1) ? -1.0f : 1.0f) / (float)(n * n)
                                  : 0.0f;
    case W_SAW:    return 1.0f / (float)n;
    case W_SQUARE: return (n & 1) ? 1.0f / (float)n : 0.0f;
    /* A reed's spectrum keeps the odd harmonics strong but is not as hollow as
     * a square; a bit of even content is what makes it read as a sax. */
    case W_REED:   return (n & 1) ? 1.0f / (float)n : 0.45f / (float)n;
    /* Plucked and struck strings start bright, so roll off slower than 1/n and
     * let the envelope do the work. */
    case W_PLUCK:  return 1.0f / powf((float)n, 0.75f);
    case W_ORGAN:  return (n == 1) ? 1.0f : (n == 2) ? 0.5f :
                          (n == 4) ? 0.32f : (n == 8) ? 0.18f : 0.0f;
    default:       return 0.0f;
    }
}

static void build_tables(void)
{
    static const int maxh[NMIP] = { 40, 14, 5 };
    int w, m, n, i;

    for (w = 0; w < W_COUNT; w++)
        for (m = 0; m < NMIP; m++) {
            float peak = 0.0f;

            memset(g_wave[w][m], 0, sizeof g_wave[w][m]);
            for (n = 1; n <= maxh[m]; n++) {
                float a = harmonic(w, n);

                if (a == 0.0f)
                    continue;
                for (i = 0; i < WAVE_LEN; i++)
                    g_wave[w][m][i] += a *
                        sinf(6.28318530718f * (float)n * (float)i / WAVE_LEN);
            }
            for (i = 0; i < WAVE_LEN; i++)
                if (fabsf(g_wave[w][m][i]) > peak)
                    peak = fabsf(g_wave[w][m][i]);
            if (peak > 0.0f)
                for (i = 0; i < WAVE_LEN; i++)
                    g_wave[w][m][i] /= peak;
        }
    g_tables_ready = 1;
}

/* ------------------------------------------------------------- GM mapping */

typedef struct {
    int   wave;
    float atk_ms, dec_ms, sus, rel_ms;
    float cutoff;      /* one-pole coefficient, 1 = open */
    float gain;
} Timbre;

/* Only the families matter; the programs WinDepth actually uses are called out
 * because they carry the tune: 64 soprano sax, 71 clarinet, 48 strings,
 * 36 slap bass, 27/28 electric guitar, 1 piano, 55 orchestra hit, 8 celesta. */
static Timbre timbre_of(int prog)
{
    Timbre t;

    t.wave = W_SAW;
    t.atk_ms = 5; t.dec_ms = 400; t.sus = 0.6f; t.rel_ms = 150;
    t.cutoff = 0.55f; t.gain = 0.5f;

    switch (prog) {
    case 8: case 9: case 10: case 11:            /* celesta / glockenspiel */
        t.wave = W_PLUCK; t.atk_ms = 1; t.dec_ms = 420; t.sus = 0.0f;
        t.rel_ms = 200; t.cutoff = 0.85f; t.gain = 0.55f;
        return t;
    case 36: case 37:                            /* slap bass */
        t.wave = W_PLUCK; t.atk_ms = 1; t.dec_ms = 260; t.sus = 0.12f;
        t.rel_ms = 90; t.cutoff = 0.7f; t.gain = 0.85f;
        return t;
    case 27:                                     /* electric guitar, clean */
        t.wave = W_PLUCK; t.atk_ms = 2; t.dec_ms = 700; t.sus = 0.25f;
        t.rel_ms = 160; t.cutoff = 0.6f; t.gain = 0.5f;
        return t;
    case 28:                                     /* electric guitar, muted */
        t.wave = W_PLUCK; t.atk_ms = 1; t.dec_ms = 130; t.sus = 0.0f;
        t.rel_ms = 60; t.cutoff = 0.5f; t.gain = 0.6f;
        return t;
    case 48: case 49: case 50: case 51:          /* string ensemble */
        t.wave = W_SAW; t.atk_ms = 90; t.dec_ms = 900; t.sus = 0.8f;
        t.rel_ms = 320; t.cutoff = 0.35f; t.gain = 0.34f;
        return t;
    case 55:                                     /* orchestra hit */
        t.wave = W_SAW; t.atk_ms = 2; t.dec_ms = 220; t.sus = 0.0f;
        t.rel_ms = 120; t.cutoff = 0.6f; t.gain = 0.7f;
        return t;
    case 64: case 65: case 66: case 67:          /* saxes */
        t.wave = W_REED; t.atk_ms = 22; t.dec_ms = 700; t.sus = 0.78f;
        t.rel_ms = 120; t.cutoff = 0.5f; t.gain = 0.55f;
        return t;
    case 71:                                     /* clarinet */
        t.wave = W_SQUARE; t.atk_ms = 18; t.dec_ms = 800; t.sus = 0.8f;
        t.rel_ms = 110; t.cutoff = 0.42f; t.gain = 0.45f;
        return t;
    default:
        break;
    }

    switch (prog / 8) {
    case 0:                                      /* piano */
        t.wave = W_PLUCK; t.atk_ms = 1; t.dec_ms = 900; t.sus = 0.22f;
        t.rel_ms = 200; t.cutoff = 0.65f; t.gain = 0.55f;
        break;
    case 1: case 13:                             /* chromatic perc / ethnic */
        t.wave = W_PLUCK; t.atk_ms = 1; t.dec_ms = 400; t.sus = 0.0f;
        t.rel_ms = 160; t.cutoff = 0.8f; t.gain = 0.5f;
        break;
    case 2:                                      /* organ */
        t.wave = W_ORGAN; t.atk_ms = 8; t.dec_ms = 1200; t.sus = 0.95f;
        t.rel_ms = 80; t.cutoff = 0.6f; t.gain = 0.42f;
        break;
    case 3:                                      /* guitar */
        t.wave = W_PLUCK; t.atk_ms = 2; t.dec_ms = 600; t.sus = 0.2f;
        t.rel_ms = 150; t.cutoff = 0.6f; t.gain = 0.5f;
        break;
    case 4:                                      /* bass */
        t.wave = W_TRI; t.atk_ms = 4; t.dec_ms = 500; t.sus = 0.55f;
        t.rel_ms = 100; t.cutoff = 0.5f; t.gain = 0.9f;
        break;
    case 5: case 6:                              /* strings / ensemble */
        t.wave = W_SAW; t.atk_ms = 70; t.dec_ms = 900; t.sus = 0.78f;
        t.rel_ms = 280; t.cutoff = 0.38f; t.gain = 0.36f;
        break;
    case 7:                                      /* brass */
        t.wave = W_SAW; t.atk_ms = 30; t.dec_ms = 600; t.sus = 0.75f;
        t.rel_ms = 140; t.cutoff = 0.5f; t.gain = 0.5f;
        break;
    case 8:                                      /* reed */
        t.wave = W_REED; t.atk_ms = 25; t.dec_ms = 700; t.sus = 0.78f;
        t.rel_ms = 130; t.cutoff = 0.5f; t.gain = 0.52f;
        break;
    case 9:                                      /* pipe */
        t.wave = W_SINE; t.atk_ms = 35; t.dec_ms = 800; t.sus = 0.8f;
        t.rel_ms = 150; t.cutoff = 0.9f; t.gain = 0.55f;
        break;
    case 10:                                     /* synth lead */
        t.wave = W_SQUARE; t.atk_ms = 6; t.dec_ms = 600; t.sus = 0.7f;
        t.rel_ms = 120; t.cutoff = 0.55f; t.gain = 0.45f;
        break;
    case 11: case 12:                            /* pad / effects */
        t.wave = W_SAW; t.atk_ms = 180; t.dec_ms = 1400; t.sus = 0.8f;
        t.rel_ms = 500; t.cutoff = 0.3f; t.gain = 0.3f;
        break;
    case 14:                                     /* percussive */
        t.wave = W_PLUCK; t.atk_ms = 1; t.dec_ms = 220; t.sus = 0.0f;
        t.rel_ms = 90; t.cutoff = 0.7f; t.gain = 0.5f;
        break;
    default:
        break;
    }
    return t;
}

/* ------------------------------------------------------------------ drums */

enum { D_KICK, D_SNARE, D_HAT, D_OPENHAT, D_TOM, D_CRASH, D_RIDE, D_CLAP,
       D_RIM, D_SHAKER, D_COWBELL };

typedef struct {
    int   kind;
    float freq, sweep, noise, tone, dec_ms, cutoff, gain;
} Drum;

/* GM percussion key map, only the pieces this soundtrack plays plus sensible
 * defaults.  `sweep` is the per-second pitch decay of the tone part. */
static Drum drum_of(int note)
{
    Drum d;

    d.kind = D_SNARE;
    d.freq = 220; d.sweep = 0.0f; d.noise = 0.8f; d.tone = 0.4f;
    d.dec_ms = 120; d.cutoff = 0.7f; d.gain = 0.8f;

    switch (note) {
    case 35: case 36:                          /* bass drum */
        d.kind = D_KICK; d.freq = 72; d.sweep = 30.0f; d.noise = 0.05f;
        d.tone = 1.0f; d.dec_ms = 170; d.cutoff = 0.25f; d.gain = 1.35f;
        break;
    case 38: case 40:                          /* snare */
        d.kind = D_SNARE; d.freq = 190; d.sweep = 4.0f; d.noise = 0.9f;
        d.tone = 0.45f; d.dec_ms = 135; d.cutoff = 0.72f; d.gain = 0.95f;
        break;
    case 37:                                   /* rim shot */
        d.kind = D_RIM; d.freq = 420; d.noise = 0.8f; d.tone = 0.5f;
        d.dec_ms = 40; d.cutoff = 0.85f; d.gain = 0.7f;
        break;
    case 39:                                   /* hand clap */
        d.kind = D_CLAP; d.freq = 0; d.noise = 1.0f; d.tone = 0.0f;
        d.dec_ms = 110; d.cutoff = 0.8f; d.gain = 0.8f;
        break;
    case 42: case 44:                          /* closed / pedal hat */
        d.kind = D_HAT; d.freq = 0; d.noise = 1.0f; d.tone = 0.0f;
        d.dec_ms = note == 42 ? 45 : 60; d.cutoff = 0.97f; d.gain = 0.5f;
        break;
    case 46:                                   /* open hat */
        d.kind = D_OPENHAT; d.freq = 0; d.noise = 1.0f; d.tone = 0.0f;
        d.dec_ms = 300; d.cutoff = 0.95f; d.gain = 0.45f;
        break;
    case 41: case 43: case 45: case 47: case 48: case 50: {
        static const float tf[6] = { 95, 115, 140, 175, 210, 255 };
        int i = note == 41 ? 0 : note == 43 ? 1 : note == 45 ? 2 :
                note == 47 ? 3 : note == 48 ? 4 : 5;

        d.kind = D_TOM; d.freq = tf[i]; d.sweep = 12.0f; d.noise = 0.12f;
        d.tone = 1.0f; d.dec_ms = 240; d.cutoff = 0.4f; d.gain = 0.9f;
        break;
    }
    case 49: case 57: case 52: case 55:        /* crash / china / splash */
        d.kind = D_CRASH; d.freq = 0; d.noise = 1.0f; d.tone = 0.0f;
        d.dec_ms = 1100; d.cutoff = 0.9f; d.gain = 0.4f;
        break;
    case 51: case 53: case 59:                 /* ride */
        d.kind = D_RIDE; d.freq = 0; d.noise = 1.0f; d.tone = 0.1f;
        d.dec_ms = 420; d.cutoff = 0.93f; d.gain = 0.35f;
        break;
    case 54: case 69: case 70:                 /* tambourine / shaker */
        d.kind = D_SHAKER; d.freq = 0; d.noise = 1.0f; d.tone = 0.0f;
        d.dec_ms = 90; d.cutoff = 0.96f; d.gain = 0.4f;
        break;
    case 56:                                   /* cowbell */
        d.kind = D_COWBELL; d.freq = 800; d.noise = 0.1f; d.tone = 1.0f;
        d.dec_ms = 200; d.cutoff = 0.85f; d.gain = 0.5f;
        break;
    default:
        d.dec_ms = 80; d.gain = 0.5f;
        break;
    }
    return d;
}

/* ---------------------------------------------------------------- helpers */

static unsigned rnd(unsigned *s)
{
    *s = *s * 1103515245u + 12345u;
    return *s;
}

static float noise(unsigned *s)
{
    return (float)((int)(rnd(s) >> 9) - 0x400000) * (1.0f / 0x400000);
}

/* Per-sample multiplier that falls to about -60dB over `ms`. */
static float decay_rate(float ms, int rate)
{
    float n = ms * 0.001f * (float)rate;

    if (n < 1.0f)
        n = 1.0f;
    return powf(0.001f, 1.0f / n);
}

static void chan_reset(Music *m)
{
    int c;

    for (c = 0; c < 16; c++) {
        m->ch[c].prog = 0;
        m->ch[c].vol = 100;
        m->ch[c].expr = 127;
        m->ch[c].pan = 64;
        m->ch[c].bend = 0;
        m->ch[c].sustain = 0;
    }
}

void mus_init(Music *m, int rate)
{
    memset(m, 0, sizeof *m);
    if (!g_tables_ready)
        build_tables();
    m->rate = rate > 0 ? rate : 44100;
    m->gain = 0.55f;
    m->rng = 0x2545f491u;
    m->usec = 500000;
    chan_reset(m);
}

int mus_load(Music *m, const unsigned char *data, long size)
{
    Smf s;

    if (smf_open(&s, data, size) != 0)
        return -1;
    mus_stop(m);
    smf_close(&m->smf);
    m->smf = s;
    m->loaded = 1;
    m->cursor = 0;
    m->usec = s.usec0;
    m->spt = (double)m->rate * (double)m->usec / 1e6 / (double)s.division;
    m->until = 0;
    chan_reset(m);
    return 0;
}

int mus_load_file(Music *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;
    int r;

    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    r = mus_load(m, buf, size);
    free(buf);
    return r;
}

void mus_play(Music *m, int loop)
{
    if (!m->loaded)
        return;
    memset(m->v, 0, sizeof m->v);
    chan_reset(m);
    m->cursor = 0;
    m->usec = m->smf.usec0;
    m->spt = (double)m->rate * (double)m->usec / 1e6 / (double)m->smf.division;
    m->until = 0;
    m->loop = loop;
    m->playing = 1;
}

void mus_stop(Music *m)
{
    m->playing = 0;
    memset(m->v, 0, sizeof m->v);
}

void mus_free(Music *m)
{
    smf_close(&m->smf);
    memset(m, 0, sizeof *m);
}

void mus_set_gain(Music *m, float g) { m->gain = g; }

/* ----------------------------------------------------------- note on/off */

static float note_freq(int note, int bend)
{
    /* bend is the raw 14-bit value biased to 0, +-8192 = +-2 semitones. */
    return 440.0f * powf(2.0f, ((float)note - 69.0f +
                                (float)bend * (2.0f / 8192.0f)) / 12.0f);
}

static SynVoice *alloc_voice(Music *m)
{
    int i, worst = 0;
    float lowest = 1e9f;

    for (i = 0; i < SYN_VOICES; i++)
        if (!m->v[i].used)
            return &m->v[i];
    /* steal the quietest, preferring one already in release */
    for (i = 0; i < SYN_VOICES; i++) {
        float score = m->v[i].env * m->v[i].amp;

        if (m->v[i].stage == 3)
            score *= 0.25f;
        if (score < lowest) {
            lowest = score;
            worst = i;
        }
    }
    return &m->v[worst];
}

static void set_pan(SynVoice *v, int pan)
{
    float p = (float)pan / 127.0f;

    /* constant-power pan */
    v->panl = cosf(p * 1.57079632679f);
    v->panr = sinf(p * 1.57079632679f);
}

static void note_on(Music *m, int ch, int note, int vel)
{
    SynVoice *v = alloc_voice(m);
    SynChan *c = &m->ch[ch];
    float amp = (float)vel / 127.0f * (float)c->vol / 127.0f *
                (float)c->expr / 127.0f;

    memset(v, 0, sizeof *v);
    v->used = 1;
    v->ch = ch;
    v->note = note;
    v->rng = m->rng ^ (unsigned)(note * 2654435761u);
    rnd(&m->rng);
    set_pan(v, c->pan);

    if (ch == 9) {
        Drum d = drum_of(note);

        v->drum = 1;
        v->dfreq = d.freq;
        v->dsweep = d.sweep / (float)m->rate;
        v->dnoise = d.noise;
        v->dtone = d.tone;
        v->dec = decay_rate(d.dec_ms, m->rate);
        v->lpc = d.cutoff;
        v->stage = 1;                    /* drums go straight to decay */
        v->env = 1.0f;
        v->amp = amp * d.gain;
        /* the hats and cymbals get a highpass so they sit above the mix */
        v->hpprev = (d.kind == D_HAT || d.kind == D_OPENHAT ||
                     d.kind == D_SHAKER || d.kind == D_CRASH ||
                     d.kind == D_RIDE) ? 1.0f : 0.0f;
        return;
    }

    {
        Timbre t = timbre_of(c->prog);
        float freq = note_freq(note, c->bend);
        int mip;

        /* Pick the band-limited variant: the more of the spectrum would fold
         * back above Nyquist, the fewer harmonics the table should carry. */
        if (freq * 40.0f < (float)m->rate * 0.5f)
            mip = 0;
        else if (freq * 14.0f < (float)m->rate * 0.5f)
            mip = 1;
        else
            mip = 2;
        v->wave = g_wave[t.wave][mip];
        v->step = (unsigned)(freq * (double)WAVE_LEN / m->rate * 65536.0);
        v->atk = 1.0f / (t.atk_ms * 0.001f * (float)m->rate + 1.0f);
        v->dec = decay_rate(t.dec_ms, m->rate);
        v->sus = t.sus;
        v->rel = decay_rate(t.rel_ms, m->rate);
        v->lpc = t.cutoff;
        v->amp = amp * t.gain;
        v->stage = 0;
        v->env = 0.0f;
    }
}

static void note_off(Music *m, int ch, int note)
{
    int i;

    if (ch == 9)
        return;                          /* let the drums ring out */
    for (i = 0; i < SYN_VOICES; i++)
        if (m->v[i].used && m->v[i].ch == ch && m->v[i].note == note &&
            m->v[i].stage != 3) {
            if (m->ch[ch].sustain)
                continue;
            m->v[i].stage = 3;
        }
}

static void all_off(Music *m, int ch, int hard)
{
    int i;

    for (i = 0; i < SYN_VOICES; i++)
        if (m->v[i].used && m->v[i].ch == ch) {
            if (hard)
                m->v[i].used = 0;
            else
                m->v[i].stage = 3;
        }
}

static void retune(Music *m, int ch)
{
    int i;

    for (i = 0; i < SYN_VOICES; i++)
        if (m->v[i].used && m->v[i].ch == ch && !m->v[i].drum) {
            float freq = note_freq(m->v[i].note, m->ch[ch].bend);

            m->v[i].step = (unsigned)(freq * (double)WAVE_LEN / m->rate * 65536.0);
        }
}

static void apply(Music *m, const SmfEvent *e)
{
    int ch = e->status & 0x0f;

    if (e->status == 0xff) {
        m->usec = e->usec ? e->usec : 500000;
        m->spt = (double)m->rate * (double)m->usec / 1e6 /
                 (double)m->smf.division;
        return;
    }
    switch (e->status & 0xf0) {
    case 0x90:
        if (e->d2)
            note_on(m, ch, e->d1, e->d2);
        else
            note_off(m, ch, e->d1);
        break;
    case 0x80:
        note_off(m, ch, e->d1);
        break;
    case 0xc0:
        m->ch[ch].prog = e->d1 & 0x7f;
        break;
    case 0xe0:
        m->ch[ch].bend = ((int)e->d2 << 7 | e->d1) - 8192;
        retune(m, ch);
        break;
    case 0xb0:
        switch (e->d1) {
        case 7:  m->ch[ch].vol = e->d2; break;
        case 10: m->ch[ch].pan = e->d2; break;
        case 11: m->ch[ch].expr = e->d2; break;
        case 64:
            m->ch[ch].sustain = e->d2 >= 64;
            if (!m->ch[ch].sustain)
                break;
            break;
        case 120: all_off(m, ch, 1); break;
        case 121: chan_reset(m); break;
        case 123: all_off(m, ch, 0); break;
        default: break;
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- render */

static void render_block(Music *m, float *l, float *r, int n)
{
    int i, k;

    for (k = 0; k < SYN_VOICES; k++) {
        SynVoice *v = &m->v[k];

        if (!v->used)
            continue;

        for (i = 0; i < n; i++) {
            float s, e = v->env;

            /* envelope */
            switch (v->stage) {
            case 0:
                e += v->atk;
                if (e >= 1.0f) { e = 1.0f; v->stage = 1; }
                break;
            case 1:
                e *= v->dec;
                if (e <= v->sus) { e = v->sus; v->stage = 2; }
                break;
            case 2:
                break;
            default:
                e *= v->rel;
                break;
            }
            v->env = e;
            if (e < 0.0002f && v->stage != 0) {
                v->used = 0;
                break;
            }

            if (v->drum) {
                float t = 0.0f;

                if (v->dtone > 0.0f && v->dfreq > 1.0f) {
                    v->dphase += (double)v->dfreq / m->rate;
                    if (v->dphase >= 1.0)
                        v->dphase -= 1.0;
                    t = sinf(6.28318530718f * (float)v->dphase) * v->dtone;
                    v->dfreq -= v->dfreq * v->dsweep;
                }
                s = t + noise(&v->rng) * v->dnoise;
            } else {
                unsigned idx;

                v->phase += v->step;
                idx = (v->phase >> 16) & WAVE_MASK;
                s = v->wave[idx];
            }

            /* one-pole lowpass, then optionally a matching highpass */
            v->lp += (s - v->lp) * v->lpc;
            s = v->lp;
            if (v->hpprev != 0.0f) {
                v->hp += (s - v->hp) * 0.35f;
                s = s - v->hp;
            }

            s *= e * v->amp;
            l[i] += s * v->panl;
            r[i] += s * v->panr;
        }
    }
}

void mus_render(Music *m, float *left, float *right, int frames)
{
    int done = 0;

    memset(left, 0, (size_t)frames * sizeof *left);
    memset(right, 0, (size_t)frames * sizeof *right);
    if (!m->playing || !m->loaded)
        return;

    while (done < frames) {
        int n = frames - done;

        /* how many samples until the next event */
        if (m->cursor < m->smf.nev) {
            if (m->until <= 0.0) {
                unsigned tick = m->smf.ev[m->cursor].tick;

                /* fire everything scheduled at this tick */
                while (m->cursor < m->smf.nev &&
                       m->smf.ev[m->cursor].tick == tick) {
                    apply(m, &m->smf.ev[m->cursor]);
                    m->cursor++;
                }
                if (m->cursor < m->smf.nev)
                    m->until = (double)(m->smf.ev[m->cursor].tick - tick) * m->spt;
                else
                    m->until = (double)(m->smf.tick_end - tick) * m->spt +
                               0.5 * m->rate;   /* let the tail ring out */
                continue;
            }
            if ((double)n > m->until)
                n = (int)m->until + 1;
        } else if (m->until <= 0.0) {
            if (m->loop) {
                m->cursor = 0;
                m->usec = m->smf.usec0;
                m->spt = (double)m->rate * (double)m->usec / 1e6 /
                         (double)m->smf.division;
                chan_reset(m);
                continue;
            }
            m->playing = 0;
            memset(m->v, 0, sizeof m->v);
            return;
        } else if ((double)n > m->until) {
            n = (int)m->until + 1;
        }
        if (n > frames - done)
            n = frames - done;
        if (n < 1)
            n = 1;

        render_block(m, left + done, right + done, n);
        m->until -= n;
        done += n;
    }

    /* master gain and a soft knee so a dense bar cannot clip hard */
    for (done = 0; done < frames; done++) {
        float a = left[done] * m->gain, b = right[done] * m->gain;

        left[done]  = a / (1.0f + fabsf(a) * 0.35f);
        right[done] = b / (1.0f + fabsf(b) * 0.35f);
    }
}
