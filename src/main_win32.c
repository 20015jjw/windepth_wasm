/* Native Win32 front end.
 *
 * The original went through the Bio_100% "WinGL" library, which picks between
 * DirectDraw, WinG and a plain DIB at run time; here there is only the DIB
 * path, which is what WinGL falls back to on a modern Windows anyway.  The
 * surface is 8bpp with the DAR's palette as the colour table, exactly as the
 * original set it up, and Windows does the conversion in SetDIBitsToDevice.
 *
 * Timing matches the original's 50ms timer (20Hz).
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#include "dar.h"
#include "video.h"
#include "game.h"

#define WD_TIMER_ID 1
#define WD_FRAME_MS 50

static Dar   g_dar;
static Video g_vid;
static Game  g_game;
static HWND  g_hwnd;
static char  g_datadir[MAX_PATH];

static struct {
    unsigned char *buf;
    long size;
} g_wav[6];

static BITMAPINFO *g_bmi;

/* The key table at 0x00421c28: three bindings for each direction, two for each
 * button.  Terminated by a zero mask. */
static const struct { unsigned mask; int vk; } g_keys[] = {
    { PAD_UP,    'K' }, { PAD_UP,    VK_UP },    { PAD_UP,    VK_NUMPAD8 },
    { PAD_DOWN,  'J' }, { PAD_DOWN,  VK_DOWN },  { PAD_DOWN,  VK_NUMPAD2 },
    { PAD_LEFT,  'H' }, { PAD_LEFT,  VK_LEFT },  { PAD_LEFT,  VK_NUMPAD4 },
    { PAD_RIGHT, 'L' }, { PAD_RIGHT, VK_RIGHT }, { PAD_RIGHT, VK_NUMPAD6 },
    { PAD_BTN1,  'Z' }, { PAD_BTN1,  VK_SPACE },
    { PAD_BTN2,  'X' }, { PAD_BTN2,  VK_RETURN },
    { 0, 0 }
};

/* ------------------------------------------------------------------ sound */

/* sndPlaySound wants a file or a WAVE image in memory; the original hands
 * WaveMix the resource image, so keep the files in memory and use SND_MEMORY. */
void wd_sound(int slot, int loop)
{
    (void)loop;
    if (slot < 0 || slot >= 6 || !g_wav[slot].buf)
        return;
    PlaySoundA((LPCSTR)g_wav[slot].buf, NULL,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void wd_music(int which, int on)
{
    static int playing = -1;
    char cmd[MAX_PATH + 64];

    if (playing >= 0) {
        mciSendStringA("close wdmidi", NULL, 0, NULL);
        playing = -1;
    }
    if (!on)
        return;
    sprintf(cmd, "open \"%s\\%s\" alias wdmidi", g_datadir,
            which == 1 ? "NAMEINN.MID" : "Windepth.mid");
    if (mciSendStringA(cmd, NULL, 0, NULL) != 0)
        return;
    mciSendStringA("play wdmidi repeat", NULL, 0, NULL);
    playing = which;
}

static void load_wavs(void)
{
    static const char *names[6] = {
        "burn.wav", "start.wav", "eneshot.wav",
        "item.wav", "plane.wav", "drop.wav"
    };
    int i;

    for (i = 0; i < 6; i++) {
        char path[MAX_PATH];
        FILE *f;

        sprintf(path, "%s\\%s", g_datadir, names[i]);
        f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        g_wav[i].size = ftell(f);
        fseek(f, 0, SEEK_SET);
        g_wav[i].buf = (unsigned char *)malloc((size_t)g_wav[i].size);
        if (g_wav[i].buf &&
            fread(g_wav[i].buf, 1, (size_t)g_wav[i].size, f) != (size_t)g_wav[i].size) {
            free(g_wav[i].buf);
            g_wav[i].buf = NULL;
        }
        fclose(f);
    }
}

/* ----------------------------------------------------------------- video */

static void build_bmi(void)
{
    int i;

    g_bmi = (BITMAPINFO *)calloc(1, sizeof(BITMAPINFOHEADER) +
                                    256 * sizeof(RGBQUAD));
    g_bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi->bmiHeader.biWidth = SCR_W;
    g_bmi->bmiHeader.biHeight = -SCR_H;      /* top-down */
    g_bmi->bmiHeader.biPlanes = 1;
    g_bmi->bmiHeader.biBitCount = 8;
    g_bmi->bmiHeader.biCompression = BI_RGB;
    g_bmi->bmiHeader.biClrUsed = 256;
    for (i = 0; i < 256; i++) {
        g_bmi->bmiColors[i].rgbRed   = g_dar.pal[i][0];
        g_bmi->bmiColors[i].rgbGreen = g_dar.pal[i][1];
        g_bmi->bmiColors[i].rgbBlue  = g_dar.pal[i][2];
    }
}

/* FUN_00402660 - push the surface, either the accumulated dirty rects or the
 * whole thing when something asked for a full redraw. */
static void present(HDC hdc)
{
    int i;

    if (g_vid.full) {
        StretchDIBits(hdc, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H,
                      g_vid.px, g_bmi, DIB_RGB_COLORS, SRCCOPY);
        g_vid.full = 0;
        g_vid.ndirty = 0;
        return;
    }
    for (i = 0; i < g_vid.ndirty; i++) {
        Rect r = g_vid.dirty[i];

        if (r.x < 0) { r.w += r.x; r.x = 0; }
        if (r.y < 0) { r.h += r.y; r.y = 0; }
        if (r.x + r.w > SCR_W) r.w = SCR_W - r.x;
        if (r.y + r.h > SCR_H) r.h = SCR_H - r.y;
        if (r.w <= 0 || r.h <= 0)
            continue;
        /* StretchDIBits measures the source rectangle from the bottom of the
         * bitmap even for a top-down DIB, so a partial blit has to flip the
         * source y itself.  Getting this wrong shows up as rectangles of
         * content lifted from the mirrored row - easy to miss on a full-screen
         * blit, obvious the moment only dirty rects are pushed. */
        StretchDIBits(hdc, r.x, r.y, r.w, r.h,
                      r.x, SCR_H - r.y - r.h, r.w, r.h,
                      g_vid.px, g_bmi, DIB_RGB_COLORS, SRCCOPY);
    }
    g_vid.ndirty = 0;
}

/* ----------------------------------------------------------------- input */

static void read_input(void)
{
    unsigned pad = 0;
    POINT pt;
    RECT rc;
    int i;

    for (i = 0; g_keys[i].mask; i++)
        if (GetAsyncKeyState(g_keys[i].vk) & 0x8000)
            pad |= g_keys[i].mask;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        pad |= PAD_ESC;
    g_game.pad = pad;

    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    GetClientRect(g_hwnd, &rc);
    g_game.mouse_in = PtInRect(&rc, pt) != 0;
    g_game.mx = pt.x;
    g_game.my = pt.y;
    g_game.mbl = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_game.mbr = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

/* ------------------------------------------------------------ window proc */

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        SetTimer(h, WD_TIMER_ID, WD_FRAME_MS, NULL);
        return 0;

    case WM_TIMER: {
        HDC hdc = GetDC(h);

        read_input();
        game_tick(&g_game);
        present(hdc);
        ReleaseDC(h, hdc);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);

        StretchDIBits(hdc, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H,
                      g_vid.px, g_bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp) {
        case VK_F2:                 /* the original's Start accelerator */
            game_start(&g_game);
            return 0;
        case VK_F3:
            g_game.sfx_on = !g_game.sfx_on;
            return 0;
        case VK_F4:
            g_game.bgm_on = !g_game.bgm_on;
            wd_music(0, g_game.bgm_on);
            return 0;
        case VK_ESCAPE:
            if (g_game.mode == MODE_PLAY)
                game_title(&g_game);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h, WD_TIMER_ID);
        wd_music(0, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ------------------------------------------------------------------- main */

static void find_datadir(void)
{
    char *p;

    GetModuleFileNameA(NULL, g_datadir, sizeof g_datadir);
    p = strrchr(g_datadir, '\\');
    if (p)
        *p = 0;
    strcat(g_datadir, "\\data");
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG msg;
    RECT rc;
    char path[MAX_PATH];

    (void)prev; (void)cmd;

    find_datadir();
    sprintf(path, "%s\\windepth.dar", g_datadir);
    if (dar_load(&g_dar, path) != 0) {
        char m[MAX_PATH + 64];
        sprintf(m, "cannot read %s", path);
        MessageBoxA(NULL, m, "WinDepth", MB_ICONERROR);
        return 1;
    }
    load_wavs();
    build_bmi();
    vid_init(&g_vid, &g_dar);

    sprintf(path, "%s\\windepth.dat", g_datadir);
    game_init(&g_game, &g_vid, &g_dar, path);

    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "WinDepth";
    if (!RegisterClassA(&wc))
        return 1;

    rc.left = 0; rc.top = 0; rc.right = SCR_W; rc.bottom = SCR_H;
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
    g_hwnd = CreateWindowExA(0, "WinDepth", "WinDepth 0.41",
                             (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME) &
                             ~WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, inst, NULL);
    if (!g_hwnd)
        return 1;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    dar_free(&g_dar);
    return (int)msg.wParam;
}
