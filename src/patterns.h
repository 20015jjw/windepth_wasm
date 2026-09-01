/* Pattern numbers in WIND_DAR.  The order is fixed by the archive, and the
 * original addresses them by number throughout, so they are spelled out here
 * rather than looked up by name. */
#ifndef PATTERNS_H
#define PATTERNS_H

#define P_SHIP      0    /* YAMABOKU - the player's destroyer */
#define P_SUB_L     1
#define P_SUB_R     2
#define P_SKY       3
#define P_SEA01     4
#define P_SEA02     5
#define P_SEA03     6
#define P_CRACK1    7    /* .. 9,  the slow bubble shot */
#define P_BOMB1    10    /* .. 12, the depth charge */
#define P_BURN1    13    /* .. 20, the 8-frame explosion */
#define P_GOLD_L   21
#define P_GOLD_R   22
#define P_WDLOGO   23
#define P_MISSLE0  24    /* .. 30, straight then L/R at three lean angles */
#define P_SCORE    31
#define P_HISCORE  32
#define P_LEFT     33
#define P_READY    34
#define P_GAMEOVER 35
#define P_STAGE    36
#define P_CHALLENG 37
#define P_NSUB_L   38
#define P_NSUB_R   39
#define P_CLEAR    40
#define P_TOTAL    41
#define P_SWIM_L1  42    /* .. 44 */
#define P_SWIM_R1  45    /* .. 47 */
#define P_BRAIN1   48    /* .. 49 */
#define P_FISH_L   50
#define P_FISH_R   51
#define P_ITEM1    52
#define P_BIOLOGO  53
#define P_LOGOBACK 54
#define P_FIRE1    55
#define P_FIREWALL 56
#define P_PLANE    57
#define P_PBOMB1   58    /* .. 60, the plane's depth charge */
#define P_SBURN1   61    /* .. 64, the small splash burst */
#define P_BOSS_L1  65
#define P_BOSS_L2  66
#define P_BOSS_R1  67
#define P_BOSS_R2  68
#define P_JET_R1   69
#define P_FIRE2_L  70
#define P_FIRE2_R  71
#define P_X        72
#define P_FONT1    73    /* WDFONTH1, 64 glyphs starting at ' ' */

#endif
