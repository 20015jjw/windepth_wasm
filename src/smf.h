/* Standard MIDI File reader.
 *
 * WinDepth ships two SMFs and hands them to SMFDRV32.DLL, i.e. to whatever
 * synthesiser Windows has.  A browser has no equivalent, so the port parses the
 * files itself and synthesises them (src/synth.c).  This half only turns a file
 * into one time-ordered event list; it knows nothing about sound.
 *
 * Both of WinDepth's files are format 0 with division 96, but format 1 is
 * handled too - the tracks are merged by tick.
 */
#ifndef SMF_H
#define SMF_H

typedef struct {
    unsigned tick;
    unsigned char status;   /* channel message with the channel in the low bits,
                             * or 0xff for the tempo change below */
    unsigned char d1, d2;
    unsigned usec;          /* status == 0xff: new microseconds per quarter */
} SmfEvent;

typedef struct {
    SmfEvent *ev;
    int nev;
    int division;            /* ticks per quarter note */
    unsigned tick_end;
    unsigned usec0;          /* tempo before any tempo event, default 500000 */
} Smf;

/* Parse an SMF image already in memory.  Returns 0 on success. */
int  smf_open(Smf *s, const unsigned char *data, long size);
void smf_close(Smf *s);

#endif
