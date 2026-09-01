/* Render one of WinDepth's .mid files with the port's synth and write a WAV,
 * so the browser build's music can be checked (and listened to) without a
 * browser.
 *
 *   midi2wav <in.mid> <out.wav> [seconds] [rate]
 *
 * Also prints what the sequencer found and how loud the result is - a silent
 * or clipped render shows up here rather than in someone's speakers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/smf.h"
#include "../src/synth.h"

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static void put32(FILE *f, unsigned v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

int main(int argc, char **argv)
{
    const char *in = argc > 1 ? argv[1] : "data/Windepth.mid";
    const char *out = argc > 2 ? argv[2] : "tests/out/music.wav";
    double seconds = argc > 3 ? atof(argv[3]) : 30.0;
    int rate = argc > 4 ? atoi(argv[4]) : 44100;
    Music mus;
    FILE *f;
    float *l, *r;
    long total, done = 0;
    const int BLK = 1024;
    double sum = 0.0;
    float peak = 0.0f;
    long clipped = 0;
    int notes = 0, i;

    mus_init(&mus, rate);
    if (mus_load_file(&mus, in) != 0) {
        fprintf(stderr, "cannot read %s\n", in);
        return 1;
    }
    for (i = 0; i < mus.smf.nev; i++)
        if ((mus.smf.ev[i].status & 0xf0) == 0x90 && mus.smf.ev[i].d2)
            notes++;
    printf("%s: %d events, %d note-ons, division %d, tick_end %u, "
           "%.0f us/qn (%.1f BPM), %.1f s\n",
           in, mus.smf.nev, notes, mus.smf.division, mus.smf.tick_end,
           (double)mus.smf.usec0, 6e7 / mus.smf.usec0,
           (double)mus.smf.tick_end * mus.smf.usec0 / 1e6 / mus.smf.division);

    total = (long)(seconds * rate);
    l = (float *)malloc((size_t)BLK * sizeof *l);
    r = (float *)malloc((size_t)BLK * sizeof *r);
    f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    fwrite("RIFF", 1, 4, f); put32(f, (unsigned)(36 + total * 4));
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16);
    put16(f, 1); put16(f, 2); put32(f, (unsigned)rate);
    put32(f, (unsigned)(rate * 4)); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (unsigned)(total * 4));

    mus_play(&mus, 1);
    while (done < total) {
        int n = (int)(total - done < BLK ? total - done : BLK);

        mus_render(&mus, l, r, n);
        for (i = 0; i < n; i++) {
            float a = l[i], b = r[i];
            int x, y;

            if (fabsf(a) > peak) peak = fabsf(a);
            if (fabsf(b) > peak) peak = fabsf(b);
            sum += (double)a * a + (double)b * b;
            if (fabsf(a) >= 0.999f || fabsf(b) >= 0.999f) clipped++;
            x = (int)(a * 32767.0f);
            y = (int)(b * 32767.0f);
            if (x > 32767)  x = 32767;
            if (x < -32768) x = -32768;
            if (y > 32767)  y = 32767;
            if (y < -32768) y = -32768;
            put16(f, (unsigned)(x & 0xffff));
            put16(f, (unsigned)(y & 0xffff));
        }
        done += n;
    }
    fclose(f);
    printf("wrote %s: %.1f s @ %d Hz, peak %.3f, rms %.4f (%.1f dBFS), "
           "clipped %ld samples\n", out, seconds, rate, peak,
           sqrt(sum / (double)(total * 2)),
           20.0 * log10(sqrt(sum / (double)(total * 2)) + 1e-12), clipped);
    mus_free(&mus);
    free(l);
    free(r);
    return 0;
}
