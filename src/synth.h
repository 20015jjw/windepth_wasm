/* A small General-MIDI-ish software synthesiser, so the browser build can play
 * WinDepth's two .mid files.
 *
 * The original hands the files to SMFDRV32.DLL and lets Windows synthesise
 * them; a browser has no MIDI output and no soundfont we may redistribute, so
 * the port generates the audio itself.  It is a wavetable synth, not a sampler:
 * additive tables built at init, three band-limited versions of each so high
 * notes alias less, one-pole filtering, an ADSR per voice, and a handful of
 * hand-made drum voices for channel 10.  It sounds like a good sound card from
 * about 1990 rather than like a GS wavetable, which suits a 1994 game.
 *
 * Everything is float and rate-agnostic, so it drops straight into whatever
 * sample rate the host's AudioContext runs at.
 */
#ifndef SYNTH_H
#define SYNTH_H

#include "smf.h"

#define SYN_VOICES 48

typedef struct {
    int used;
    int ch, note;
    unsigned phase, step;     /* 16.16 index into the wavetable */
    const float *wave;        /* NULL for the drum voices */
    int drum;
    float amp;                /* velocity * channel volume * timbre gain */
    float env;
    int stage;                /* 0 attack, 1 decay, 2 sustain, 3 release */
    float atk, dec, sus, rel;
    float lp, lpc;            /* one-pole lowpass */
    float hp, hpprev;         /* one-pole highpass, for the hats */
    float panl, panr;
    float dfreq, dsweep, dnoise, dtone;   /* drum oscillator */
    double dphase;
    unsigned rng;
} SynVoice;

typedef struct {
    int prog, vol, expr, pan, bend, sustain;
} SynChan;

typedef struct {
    Smf smf;
    int loaded, playing, loop;
    int rate;
    int cursor;               /* next event */
    double spt;               /* samples per tick at the current tempo */
    double until;             /* samples left before the next event */
    unsigned usec;
    SynChan ch[16];
    SynVoice v[SYN_VOICES];
    float gain;
    unsigned rng;
} Music;

void mus_init(Music *m, int rate);
int  mus_load(Music *m, const unsigned char *smf, long size);
int  mus_load_file(Music *m, const char *path);
void mus_play(Music *m, int loop);
void mus_stop(Music *m);
void mus_free(Music *m);
void mus_set_gain(Music *m, float g);

/* Write `frames` stereo samples, overwriting the buffers.  Silence when
 * nothing is playing. */
void mus_render(Music *m, float *left, float *right, int frames);

#endif
