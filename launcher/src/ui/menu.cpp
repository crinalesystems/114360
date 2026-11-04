/* Launcher menu. A DOS-style text screen: black background, grey and white text, inverse bars for the title,
   the selection and the key help, in an X11 bitmap font (font.h) drawn at up to twice its size. microui lays
   the screen out and records it as draw commands; every rectangle and every horizontal run of lit glyph pixels
   becomes one quad, drawn by a small shader pair compiled at start-up.

   Text at a fractional scale is anti-aliased rather than rounded to whole screen pixels (rounding made some
   strokes one pixel and others two, so lines looked half drawn). Each glyph run is drawn at its exact position
   and the pixel shader returns how much of the screen pixel the run covers. The coverages of one string are
   added up in the back buffer's alpha, so neighbouring runs that share an edge sum to full without a seam, and
   the text colour is then blended through that alpha (render_text).

   Controls (any pad): up/down move, left/right change a value, A selects or edits, START plays. Text fields
   (server name, address, PlayOnline ID and password, chat) are edited with the system keyboard
   (XShowKeyboardUI). The PlayOnline chat page (polirc.c) has its own items, B goes back, LB/RB scroll its log.
   A preset with a PlayOnline ID draws on DOS blue instead of black and signs in before it starts. */
#include <xtl.h>
#include <d3dx9.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "microui.h"
}
#include "font.h"
#include "menu.h"

extern "C" ULONG __cdecl DbgPrint(const char *format, ...);

#ifndef VKBD_DEFAULT
#define VKBD_DEFAULT 0x00000000
#endif

#define SCREEN_W 1280
#define SCREEN_H 720
/* Text is drawn at a fractional scale: twice the font's pixels where that fits, smaller where the menu's
   MENU_ROWS rows or the widest line (MENU_COLS, the chat page's key help) would not fit the title-safe area
   (set_font). The safe area is 5% on an HD output and 10% on SD (480p and 4:3, where TVs overscan more);
   an SD output still gets the whole 1280x720 frame, scaled down by the console. */
#define MAX_SCALE 2.0f
#define MENU_ROWS 27
#define MENU_COLS 86
static float text_scale = MAX_SCALE;
static int margin_x = 64, margin_y = 36;
#define MARGIN_X margin_x
#define MARGIN_Y margin_y
#define LABEL_COLS 16     /* label column: the longest label ("PlayOnline ID", 13) plus its indent and a gap */
#define BATCH_QUADS 512

/* The DOS palette's black, light grey and white. */
static const mu_Color BLACK = {0, 0, 0, 255};
static const mu_Color GREY = {170, 170, 170, 255};
static const mu_Color WHITE = {255, 255, 255, 255};

/* p: clip position; q: the same point in screen pixels; box: the rectangle being drawn, in screen pixels. The
   pixel shader's q is its sample point, whatever the pixel-centre convention, so the pixel is [q-0.5, q+0.5]. */
static const char vertex_source[] =
    "struct V { float4 p : POSITION; float2 q : TEXCOORD0; float4 box : TEXCOORD1; };"
    "V main(float2 p : POSITION, float2 q : TEXCOORD0, float4 box : TEXCOORD1)"
    "{ V v; v.p = float4(p, 0, 1); v.q = q; v.box = box; return v; }";
static const char pixel_source[] =
    "float4 color : register(c0);"
    "float4 main(float2 q : TEXCOORD0, float4 box : TEXCOORD1) : COLOR"
    "{ float2 c = saturate(min(q + 0.5, box.zw) - max(q - 0.5, box.xy));"
    "  return float4(color.rgb, color.a * c.x * c.y); }";

/* ---- Drawing ---------------------------------------------------------------------------------------- */

typedef struct {
    float x, y;
    float px, py;
    float l, t, r, b;
} VERTEX;

/* Render states of a batch: plain colour (rectangles), and the three passes of one string. */
enum { DRAW_FILL, DRAW_ALPHA_CLEAR, DRAW_COVERAGE, DRAW_TEXT };

static IDirect3DDevice9 *dev;
static VERTEX batch[BATCH_QUADS * 6];
static int batch_quads;

static void flush(void)
{
    if (batch_quads != 0) {
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, batch_quads * 2, batch, sizeof(VERTEX));
        batch_quads = 0;
    }
}

/* Draw what is queued, then set the render states and colour (alpha 0..1) for the next batch. */
static void set_draw(int mode, mu_Color c, float alpha)
{
    static const DWORD write_mask[] = {
        D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE,
        D3DCOLORWRITEENABLE_ALPHA,
        D3DCOLORWRITEENABLE_ALPHA,
        D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE,
    };
    float f[4];
    flush();
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, write_mask[mode]);
    if (mode == DRAW_COVERAGE) {
        /* Alpha += coverage: runs sharing an edge add up to exactly full. */
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    } else if (mode == DRAW_TEXT) {
        /* Colour = text * alpha + what is underneath * (1 - alpha). */
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVDESTALPHA);
    } else {
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
    f[0] = c.r / 255.0f;
    f[1] = c.g / 255.0f;
    f[2] = c.b / 255.0f;
    f[3] = alpha;
    dev->SetPixelShaderConstantF(0, f, 1);
}

/* Queue the rectangle [x0, x1) x [y0, y1) in screen pixels. With `aa` its quad grows by a pixel on every side,
   so the pixels it partly covers are drawn too and get their coverage from the shader. */
static void quad(float x0, float y0, float x1, float y1, int aa)
{
    float g = aa ? 1.0f : 0.0f;
    float qx[2], qy[2];
    static const int corner[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
    VERTEX *v;
    int i;
    if (batch_quads == BATCH_QUADS) {
        flush();
    }
    qx[0] = x0 - g;
    qx[1] = x1 + g;
    qy[0] = y0 - g;
    qy[1] = y1 + g;
    v = batch + batch_quads++ * 6;
    for (i = 0; i < 6; i++) {
        v[i].px = qx[corner[i][0]];
        v[i].py = qy[corner[i][1]];
        v[i].x = v[i].px * (2.0f / SCREEN_W) - 1.0f;
        v[i].y = 1.0f - v[i].py * (2.0f / SCREEN_H);
        v[i].l = x0;
        v[i].t = y0;
        v[i].r = x1;
        v[i].b = y1;
    }
}

/* Width in whole screen pixels of `n` font pixels, for microui's layout. */
static int edge(int n)
{
    return (int)(n * text_scale + 0.5f);
}

/* Queue every horizontal run of lit pixels of `s`, at its exact (fractional) screen position. */
static void glyph_runs(const FONT *font, int x, int y, const char *s)
{
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)s[i];
        const unsigned short *glyph;
        int row, base = i * font->w;
        if (ch < FONT_FIRST || ch > FONT_LAST) {
            ch = '?';
        }
        glyph = font->bits + (ch - FONT_FIRST) * font->h;
        for (row = 0; row < font->h; row++) {
            unsigned bits = glyph[row];
            int col = 0;
            while (col < font->w) {
                int start;
                if ((bits & (0x8000u >> col)) == 0) {
                    col++;
                    continue;
                }
                start = col;
                while (col < font->w && (bits & (0x8000u >> col)) != 0) {
                    col++;
                }
                quad(x + (base + start) * text_scale, y + row * text_scale, x + (base + col) * text_scale,
                     y + (row + 1) * text_scale, 1);
            }
        }
    }
}

/* One string in three passes: clear the alpha under it, add up its coverage there, blend its colour through. */
static void render_text(const FONT *font, int x, int y, const char *s, mu_Color color)
{
    float x1 = x + (int)strlen(s) * font->w * text_scale + 1.0f, y1 = y + font->h * text_scale + 1.0f;
    set_draw(DRAW_ALPHA_CLEAR, color, 0.0f);
    quad(x - 1.0f, y - 1.0f, x1, y1, 0);
    set_draw(DRAW_COVERAGE, color, 1.0f);
    glyph_runs(font, x, y, s);
    set_draw(DRAW_TEXT, color, 1.0f);
    quad(x - 1.0f, y - 1.0f, x1, y1, 0);
    flush();
}

static void render(mu_Context *ctx)
{
    mu_Command *cmd = NULL;
    int filling = 0;
    D3DCOLOR fill_color = 0;
    while (mu_next_command(ctx, &cmd)) {
        if (cmd->type == MU_COMMAND_RECT) {
            mu_Color c = cmd->rect.color;
            D3DCOLOR packed = D3DCOLOR_XRGB(c.r, c.g, c.b);
            if (!filling || packed != fill_color) {
                set_draw(DRAW_FILL, c, 1.0f);
                filling = 1;
                fill_color = packed;
            }
            quad((float)cmd->rect.rect.x, (float)cmd->rect.rect.y, (float)(cmd->rect.rect.x + cmd->rect.rect.w),
                 (float)(cmd->rect.rect.y + cmd->rect.rect.h), 0);
        } else if (cmd->type == MU_COMMAND_TEXT) {
            render_text((const FONT *)cmd->text.font, cmd->text.pos.x, cmd->text.pos.y, cmd->text.str,
                        cmd->text.color);
            filling = 0;
        }
        /* MU_COMMAND_CLIP: everything is drawn inside the one full-screen container; no icons are used. */
    }
    flush();
}

static int text_width(mu_Font font, const char *s, int len)
{
    return edge((len < 0 ? (int)strlen(s) : len) * ((const FONT *)font)->w);
}

static int text_height(mu_Font font)
{
    return edge(((const FONT *)font)->h);
}

static const FONT *font_named(const char *name)
{
    int i;
    for (i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (_stricmp(fonts[i].name, name) == 0) {
            return &fonts[i];
        }
    }
    return &fonts[0];
}

/* ---- Pad input -------------------------------------------------------------------------------------- */

#define REPEAT_DELAY 400
#define REPEAT_RATE 120
#define STICK_DEAD 16000

/* Buttons newly pressed on any pad since the last call, with the d-pad (and the left stick, as a d-pad)
   repeating while held. *port receives the pad that pressed. */
static WORD read_pads(DWORD *port)
{
    static WORD held[XUSER_MAX_COUNT];
    static DWORD repeat_at[XUSER_MAX_COUNT];
    const WORD dpad = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT
                      | XINPUT_GAMEPAD_DPAD_RIGHT;
    WORD pressed = 0;
    DWORD p, now = GetTickCount();
    for (p = 0; p < XUSER_MAX_COUNT; p++) {
        XINPUT_STATE state;
        WORD buttons, fresh;
        if (XInputGetState(p, &state) != ERROR_SUCCESS) {
            held[p] = 0;
            continue;
        }
        buttons = state.Gamepad.wButtons;
        if (state.Gamepad.sThumbLY > STICK_DEAD) buttons |= XINPUT_GAMEPAD_DPAD_UP;
        if (state.Gamepad.sThumbLY < -STICK_DEAD) buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (state.Gamepad.sThumbLX < -STICK_DEAD) buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (state.Gamepad.sThumbLX > STICK_DEAD) buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        fresh = buttons & ~held[p];
        if (fresh & dpad) {
            repeat_at[p] = now + REPEAT_DELAY;
        } else if ((buttons & dpad) != 0 && (int)(now - repeat_at[p]) >= 0) {
            fresh |= buttons & dpad;
            repeat_at[p] = now + REPEAT_RATE;
        }
        held[p] = buttons;
        if (fresh != 0) {
            pressed |= fresh;
            *port = p;
        }
    }
    return pressed;
}

/* ---- System keyboard -------------------------------------------------------------------------------- */

enum { EDIT_NONE, EDIT_NAME, EDIT_ADDRESS, EDIT_NEW, EDIT_DUMP, EDIT_POLID, EDIT_POLPASS, EDIT_SAY };

/* Settings fit MENU_TEXT; a chat line (a message, /register with its code) gets more. */
#define CHAT_TEXT 160

static XOVERLAPPED keyboard_wait;
static WCHAR keyboard_text[CHAT_TEXT];
static int editing = EDIT_NONE;

static void widen(WCHAR *out, int size, const char *in)
{
    int i;
    for (i = 0; i < size - 1 && in[i] != '\0'; i++) {
        out[i] = (unsigned char)in[i];
    }
    out[i] = 0;
}

static void open_keyboard(DWORD port, int what, const char *current, const WCHAR *title, const WCHAR *hint)
{
    static WCHAR initial[MENU_TEXT];
    DWORD r;
    widen(initial, MENU_TEXT, current);
    memset(&keyboard_wait, 0, sizeof(keyboard_wait));
    memset(keyboard_text, 0, sizeof(keyboard_text));
    r = XShowKeyboardUI(port, VKBD_DEFAULT, initial, title, hint, keyboard_text, what == EDIT_SAY ? CHAT_TEXT : MENU_TEXT,
                        &keyboard_wait);
    if (r == ERROR_IO_PENDING || r == ERROR_SUCCESS) {
        editing = what;
    } else {
        DbgPrint("114360: menu: XShowKeyboardUI failed %08X\n", r);
    }
}

/* Copy the keyboard's result as ASCII (other characters become '?', blanks at either end dropped). Returns
   0 when the player cancelled or left the text empty. */
static int keyboard_result(char *out, int size)
{
    int i, n = 0, start = 0;
    if (XGetOverlappedResult(&keyboard_wait, NULL, FALSE) != ERROR_SUCCESS) {
        return 0;
    }
    while (keyboard_text[start] == L' ') {
        start++;
    }
    for (i = start; keyboard_text[i] != 0 && n < size - 1; i++) {
        out[n++] = keyboard_text[i] < 0x7F && keyboard_text[i] >= 0x20 ? (char)keyboard_text[i] : '?';
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
    return n > 0;
}

/* ---- The menu --------------------------------------------------------------------------------------- */

enum { ITEM_SERVER, ITEM_ADDRESS, ITEM_REVISION, ITEM_PLATFORM, ITEM_POL, ITEM_LANGUAGE, ITEM_FONT, ITEM_DUMP,
       ITEM_POLID, ITEM_POLPASS, ITEM_START, ITEM_CHAT, ITEM_NEW, ITEM_DELETE, ITEM_CREDITS, ITEM_EXIT, ITEM_COUNT };

/* The PlayOnline chat page (draw_chat): shown instead of the menu, scrolled back by chat_scroll rows. */
static int chat_page, chat_scroll;

/* The credits page (draw_credits): what the launcher is built from and under which terms, scrolled by
   credits_scroll rows. The full license texts ship in the installer's licenses folder. */
static int credits_page, credits_scroll;
static const char *const credits[] = {
    "114360 launcher: FINAL FANTASY XI for Xbox 360 on today's servers.",
    "Licensed under the GNU Affero General Public License, version 3 (AGPL-3.0).",
    "",
    "PlayOnline sign-in crypto (polcrypt.c) is ported from Project Crystal",
    "(POLCommon SqCrypto.cs and Blowfish.cs), AGPL-3.0.",
    "PlayOnline protocol details follow Project Crystal and OpenLobby",
    "(PrettyOpenLobby/OpenLobby), both AGPL-3.0.",
    "",
    "microui - Copyright (c) 2024 rxi. MIT License:",
    "Permission is hereby granted, free of charge, to any person obtaining a copy",
    "of this software and associated documentation files (the \"Software\"), to deal",
    "in the Software without restriction, including without limitation the rights",
    "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell",
    "copies of the Software, and to permit persons to whom the Software is",
    "furnished to do so, subject to the following conditions:",
    "The above copyright notice and this permission notice shall be included in all",
    "copies or substantial portions of the Software.",
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR",
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,",
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE",
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER",
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,",
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE",
    "SOFTWARE.",
    "",
    "Fonts 6x13, 7x13, 9x15: X.Org misc-fixed. \"Public domain font. Share and enjoy.\"",
    "",
    "Font 8x16: X.Org Sony fixed. Copyright 1989 by Sony Corp.",
    "Permission to use, copy, modify, and distribute this software and",
    "its documentation for any purpose and without fee is hereby granted,",
    "provided that the above copyright notices appear in all copies and",
    "that both those copyright notices and this permission notice appear",
    "in supporting documentation, and that the name of Sony Corp.",
    "not be used in advertising or publicity pertaining to distribution",
    "of the software without specific, written prior permission.  Sony",
    "Corp. makes no representations about the suitability of this",
    "software for any purpose.  It is provided \"as is\" without express or",
    "implied warranty.",
    "SONY DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS",
    "SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND",
    "FITNESS, IN NO EVENT SHALL SONY BE LIABLE FOR ANY",
    "SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER",
    "RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF",
    "CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN",
    "CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.",
    "",
    "FINAL FANTASY XI and PlayOnline are trademarks of Square Enix. Their game files",
    "are not part of this launcher and are not distributed with it.",
};
#define CREDITS_LINES (int)(sizeof(credits) / sizeof(credits[0]))

/* What has been typed on the chat page with a keyboard (Xenia keyboard_mode = 2, or 4 in the 114360 build, or a
   USB keyboard on a console): sent as a line by Enter, like a terminal. */
#define CHAT_LINE 160
static char chat_line[CHAT_LINE];
static int chat_len;

static const char *const languages[] = {"", "japanese", "english", "2", "3"};
#define LANGUAGE_COUNT (sizeof(languages) / sizeof(languages[0]))

/* The platform tags the official clients put in the world login (0x00A): the game's own (Xbox 360), the PS2
   client's, the PC client's. A server picks its packet layout by it (Project Crystal translates for "PS2"). */
static const char *const platforms[] = {"", "PS2", "WIN"};
#define PLATFORM_COUNT (int)(sizeof(platforms) / sizeof(platforms[0]))

static const char *platform_label(const char *tag)
{
    if (_stricmp(tag, "PS2") == 0) {
        return "PS2 (2010 packets on Project Crystal)";
    }
    if (_stricmp(tag, "WIN") == 0) {
        return "PC (WIN)";
    }
    return "Xbox 360 (the game's own)";
}

/* The PlayOnline row: the version the launcher signs in as (polirc.h pol_versions); "" = the same version as the
   game's PolCore (`game`, shown on the PolCore row). */
static const char *pol_label(const char *version, const char *game, char *buf, int size)
{
    const POL_VERSION *v = pol_version_find(version);
    if (v == NULL) {
        _snprintf(buf, size, "%s (as the PolCore)", game[0] ? game : "-");
    } else {
        _snprintf(buf, size, "X2U %s (%s)", v->version, v->servers);
    }
    buf[size - 1] = '\0';
    return buf;
}

static mu_Context ui;

/* Use `font` for the menu, at the largest scale up to MAX_SCALE that fits MENU_ROWS x MENU_COLS in the safe
   area. */
static void set_font(const FONT *font)
{
    float by_h = (float)(SCREEN_H - 2 * margin_y) / (MENU_ROWS * font->h);
    float by_w = (float)(SCREEN_W - 2 * margin_x) / (MENU_COLS * font->w);
    text_scale = MAX_SCALE;
    if (by_h < text_scale) {
        text_scale = by_h;
    }
    if (by_w < text_scale) {
        text_scale = by_w;
    }
    ui.style->font = (mu_Font)font;
}

static int cell_w(void)
{
    return ui.text_width(ui.style->font, "M", 1);
}

static int cell_h(void)
{
    return ui.text_height(ui.style->font);
}

static void text_at(mu_Rect r, const char *s, mu_Color color)
{
    mu_draw_text(&ui, ui.style->font, s, -1, mu_vec2(r.x, r.y), color);
}

/* A full-width inverse bar holding `s`, at the next layout cell or at `fixed` when given. */
static void bar(const char *s, const mu_Rect *fixed)
{
    int width = -1;
    mu_Rect r;
    mu_layout_row(&ui, 1, &width, cell_h());
    if (fixed != NULL) {
        mu_layout_set_next(&ui, *fixed, 0);
    }
    r = mu_layout_next(&ui);
    mu_draw_rect(&ui, r, GREY);
    text_at(mu_rect(r.x + cell_w(), r.y, r.w, r.h), s, BLACK);
}

static void gap(void)
{
    int width = -1;
    mu_layout_row(&ui, 1, &width, cell_h() / 2);
    mu_layout_next(&ui);
}

/* A "Label   value" line. The selected value sits on an inverse bar; `choice` marks values changed with
   left/right, shown between < > while selected. */
static void field(const char *label, const char *value, int selected, int choice, mu_Color color)
{
    int widths[2];
    char shown[MENU_TEXT + 8];
    mu_Rect r;
    widths[0] = LABEL_COLS * cell_w();
    widths[1] = -1;
    mu_layout_row(&ui, 2, widths, cell_h());
    r = mu_layout_next(&ui);
    text_at(mu_rect(r.x + cell_w(), r.y, r.w, r.h), label, GREY);
    r = mu_layout_next(&ui);
    _snprintf(shown, sizeof(shown), selected && choice ? "< %s >" : " %s ", value);
    shown[sizeof(shown) - 1] = '\0';
    if (selected) {
        mu_draw_rect(&ui, mu_rect(r.x, r.y, ui.text_width(ui.style->font, shown, -1), r.h), GREY);
        text_at(r, shown, BLACK);
    } else {
        text_at(r, shown, color);
    }
}

/* An action line ("Start game"). */
static void action(const char *s, int selected, int enabled)
{
    int width = -1;
    char shown[MENU_TEXT];
    mu_Rect r;
    mu_layout_row(&ui, 1, &width, cell_h());
    r = mu_layout_next(&ui);
    _snprintf(shown, sizeof(shown), " %s ", s);
    shown[sizeof(shown) - 1] = '\0';
    r.x += cell_w();
    if (selected) {
        mu_draw_rect(&ui, mu_rect(r.x, r.y, ui.text_width(ui.style->font, shown, -1), r.h), GREY);
        text_at(r, shown, BLACK);
    } else {
        text_at(r, shown, enabled ? WHITE : GREY);
    }
}

static int find_revision(const MENU *m, const char *name)
{
    int i;
    for (i = 0; i < m->revision_count; i++) {
        if (_stricmp(m->revisions[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_language(const char *name)
{
    int i;
    for (i = 0; i < LANGUAGE_COUNT; i++) {
        if (_stricmp(languages[i], name) == 0) {
            return i;
        }
    }
    return 0;
}

static int find_font(const char *name)
{
    return (int)(font_named(name) - fonts);
}

/* Left/right (step -1/+1) on a value. */
static void change(MENU *m, int item, int step)
{
    MENU_PRESET *p = &m->presets[m->current];
    int n;
    switch (item) {
    case ITEM_SERVER:
        m->current = (m->current + m->preset_count + step) % m->preset_count;
        m->changed = 1;
        break;
    case ITEM_REVISION:
        if (m->revision_count > 0) {
            n = find_revision(m, p->revision);
            n = n < 0 ? 0 : (n + m->revision_count + step) % m->revision_count;
            strcpy(p->revision, m->revisions[n]);
            m->changed = 1;
        }
        break;
    case ITEM_PLATFORM:
        for (n = 0; n < PLATFORM_COUNT && _stricmp(platforms[n], p->platform) != 0; n++) {
        }
        n = n == PLATFORM_COUNT ? 0 : (n + PLATFORM_COUNT + step) % PLATFORM_COUNT;
        strcpy(p->platform, platforms[n]);
        m->changed = 1;
        break;
    case ITEM_POL:
        /* "" (the game's own), then each known version. */
        for (n = 0; n < POL_VERSION_COUNT && strcmp(pol_versions[n].version, p->pol_version) != 0; n++) {
        }
        n = n == POL_VERSION_COUNT ? 0 : n + 1;
        n = (n + POL_VERSION_COUNT + 1 + step) % (POL_VERSION_COUNT + 1);
        strcpy(p->pol_version, n == 0 ? "" : pol_versions[n - 1].version);
        m->changed = 1;
        break;
    case ITEM_LANGUAGE:
        n = (find_language(m->language) + LANGUAGE_COUNT + step) % LANGUAGE_COUNT;
        strcpy(m->language, languages[n]);
        m->changed = 1;
        break;
    case ITEM_FONT:
        n = (find_font(m->font) + (int)(sizeof(fonts) / sizeof(fonts[0])) + step) % (int)(sizeof(fonts) / sizeof(fonts[0]));
        strcpy(m->font, fonts[n].name);
        set_font(&fonts[n]);
        m->changed = 1;
        break;
    }
}

/* ---- Chat commands ---------------------------------------------------------------------------------- */

/* A manual page per command, shown in the chat log by /help (like man): NAME, SYNOPSIS, DESCRIPTION. */
struct CHAT_MAN {
    const char *name;
    const char *synopsis;
    const char *summary;
    const char *description[5];
};

static const CHAT_MAN chat_pages[] = {
    {"help", "/help [command]", "show this list, or the manual page of a command",
     {"With no command, lists every command with a line about it.",
      "With a command, shows its page: NAME, SYNOPSIS, DESCRIPTION.",
      "/man is the same command. LB/RB scroll the log.", NULL}},
    {"login", "/login [ID [password]]", "sign in to PlayOnline",
     {"Signs in to the server of the current preset (its Address).",
      "Without arguments it uses the preset's PlayOnline ID and Password.",
      "With them it signs in with those and stores them in the preset.",
      "A new sign-in ends the previous session; the log is kept.", NULL}},
    {"logout", "/logout", "sign out of PlayOnline",
     {"Ends the session. A preset with a PlayOnline ID signs in again",
      "when the game is started.", NULL}},
    {"join", "/join #channel", "join a chat channel",
     {"Joins the channel and makes it the one plain text goes to.",
      "The channel is kept in launcher.ini (chat_channel).", NULL}},
    {"part", "/part [#channel]", "leave a chat channel",
     {"Leaves the channel (default: the current one).", NULL}},
    {"tell", "/tell ID text", "send a private message",
     {"Sends text to one PlayOnline ID. Tells you receive show as",
      "\"ID >> text\"; tells you send as \"ID << text\".", NULL}},
    {"say", "/say text", "talk in the current channel",
     {"The same as typing text without a command.", NULL}},
    {"profile", "/profile", "show the account",
     {"Shows the signed-in PlayOnline ID, its server and channel, and asks",
      "the profile server for the account's content IDs (characters).", NULL}},
    {"clear", "/clear", "empty the chat log", {NULL}},
    {"errors", "/errors", "show what went wrong",
     {"The install check of every revision (GOOD100, or codes such as E303",
      "with what they mean and the first missing files; install-check.txt",
      "has each result, install-check-files.txt every missing file), then",
      "this run's and the last run's crashes, missing data files, sign-ins.", NULL}},
    {"patches", "/patches [id] [on|off]", "client patches for this preset's game",
     {"Fixes to the game's program files (not the data packs), each for the",
      "client build it was made for, applied as the game reads the file.",
      "With no id, lists them: on/off, applies or not to this preset's client.",
      "With an id, shows what it changes; on/off turns it on or off for the",
      "selected preset (saved in launcher.ini, key patches)."}},
    {"register", "/register name password [code or e-mail]", "make a new PlayOnline ID",
     {"Fills in the server's PlayOnline registration pages, as the Viewer does.",
      "OpenLobby: name is the handle (up to 15), then its registration code.",
      "Project Crystal: name is the user name, then an e-mail address; it",
      "mails a code for /verify. Passwords: 8-15 letters and digits only.",
      "The new ID and password go into the preset. /reg is the same."}},
    {"verify", "/verify CODE", "finish a registration with the mailed code",
     {"Project Crystal mails a 6-digit code after /register; /verify sends it",
      "and the server makes the account.", NULL}},
    {"friends", "/friends", "show the friend list",
     {"Lists your PlayOnline friends with their IDs and whether they are",
      "online; \"waiting\" marks a request they have not accepted yet.",
      "Friend requests you receive, and friends coming online or going",
      "offline, show up in the log as they happen.", NULL}},
    {"friend", "/friend add|accept|remove ID [name]", "change the friend list",
     {"add: asks ID to be your friend (they see the request in their log or Viewer).",
      "accept: accepts the request ID sent you; both lists then hold the link.",
      "remove: takes ID off your list. name is how the entry is shown (default ID).",
      "Signing in gives an account a PlayOnline handle if it has none,",
      "which others need before they can add it."}},
};
#define CHAT_PAGE_COUNT (int)(sizeof(chat_pages) / sizeof(chat_pages[0]))

static int chat_signin_pending;     /* /login: the menu loop signs in (it draws the "Signing in" frame) */

static void chat_help(MENU *m, const char *name)
{
    int i, k;
    if (name[0] == '/' || name[0] == '\\') {
        name++;
    }
    if (name[0] == '\0') {
        pol_log(m->pol, "COMMANDS  (/help command for its page)");
        for (i = 0; i < CHAT_PAGE_COUNT; i++) {
            pol_log(m->pol, "    /%-8s %s", chat_pages[i].name, chat_pages[i].summary);
        }
        pol_log(m->pol, "    Text without a command goes to the current channel. \\ works as /.");
        return;
    }
    for (i = 0; i < CHAT_PAGE_COUNT; i++) {
        if (_stricmp(chat_pages[i].name, name) == 0 || (_stricmp(name, "man") == 0 && i == 0)) {
            pol_log(m->pol, "NAME");
            pol_log(m->pol, "    %s - %s", chat_pages[i].name, chat_pages[i].summary);
            pol_log(m->pol, "SYNOPSIS");
            pol_log(m->pol, "    %s", chat_pages[i].synopsis);
            if (chat_pages[i].description[0] != NULL) {
                pol_log(m->pol, "DESCRIPTION");
                for (k = 0; k < 5 && chat_pages[i].description[k] != NULL; k++) {
                    pol_log(m->pol, "    %s", chat_pages[i].description[k]);
                }
            }
            return;
        }
    }
    pol_log(m->pol, "No manual entry for %s", name);
}

static void chat_friends(MENU *m)
{
    static POL_FRIEND friends[100];
    int n, i;
    if (m->pol->state != POL_ONLINE) {
        pol_log(m->pol, "friends: not signed in (/login)");
        return;
    }
    n = pol_friends(m->pol, friends, 100);
    if (n < 0) {
        pol_log(m->pol, "friends: the profile server did not answer");
    } else if (n == 0) {
        pol_log(m->pol, "friends: none yet (/friend add ID)");
    }
    for (i = 0; i < n; i++) {
        pol_log(m->pol, "  %s  %-15s %s%s", friends[i].pol_id, friends[i].name,
                friends[i].temporary ? "waiting" : friends[i].online ? "online" : "offline",
                friends[i].blacklist ? ", blocked" : "");
    }
    for (i = 0; i < m->pol->request_count; i++) {
        pol_log(m->pol, "  %s asks to be your friend (/friend accept %s)", m->pol->requests[i].pol_id,
                m->pol->requests[i].pol_id);
    }
}

/* "/friend add|accept|remove ID [name]" */
static void chat_friend(MENU *m, char *arg)
{
    char *id = strchr(arg, ' '), *name;
    *id++ = '\0';
    while (*id == ' ') {
        id++;
    }
    name = strchr(id, ' ');
    if (name != NULL) {
        *name++ = '\0';
    }
    _strupr(id);
    if (m->pol->state != POL_ONLINE) {
        pol_log(m->pol, "friend: not signed in (/login)");
    } else if (_stricmp(arg, "add") == 0) {
        pol_friend_add(m->pol, id, name);
    } else if (_stricmp(arg, "accept") == 0) {
        pol_friend_accept(m->pol, id, name);
    } else if (_stricmp(arg, "remove") == 0 || _stricmp(arg, "del") == 0) {
        pol_friend_remove(m->pol, id);
    } else {
        pol_log(m->pol, "friend: add, accept or remove (/help friend)");
    }
}

static void chat_profile(MENU *m)
{
    static POL_CHARACTER chars[16];
    const MENU_PRESET *p = &m->presets[m->current];
    int n, i;
    if (m->pol->state != POL_ONLINE) {
        pol_log(m->pol, "profile: not signed in (/login)");
        return;
    }
    pol_log(m->pol, "PlayOnline ID %s on %s, channel %s", m->pol->pol_id, p->address,
            m->channel[0] ? m->channel : "(none)");
    n = pol_characters(m->pol, chars, 16);
    if (n < 0) {
        pol_log(m->pol, "profile: the profile server did not answer the character list");
        return;
    }
    pol_log(m->pol, "%d content ID%s", n, n == 1 ? "" : "s");
    for (i = 0; i < n; i++) {
        pol_log(m->pol, "    %08X  %s  %s", chars[i].content_id,
                chars[i].content_class == 1 ? "FFXI" : "other",
                chars[i].sub_id == 0 ? "(free)" : chars[i].name);
    }
}

/* /register name password [code or e-mail]: a new PlayOnline ID on the preset's server, saved into the preset.
   A server that mails a verification code (Project Crystal) is finished with /verify CODE. */
static char register_password[32];  /* kept for /verify, to save into the preset */

static void registered(MENU *m, const char *pol_id, const char *password, const char *note)
{
    MENU_PRESET *p = &m->presets[m->current];
    if (pol_id[0]) {
        _snprintf(p->pol_id, sizeof(p->pol_id), "%s", pol_id);
        p->pol_id[sizeof(p->pol_id) - 1] = '\0';
        _snprintf(p->pol_password, sizeof(p->pol_password), "%s", password);
        p->pol_password[sizeof(p->pol_password) - 1] = '\0';
        m->changed = 1;
        pol_log(m->pol, "register: your PlayOnline ID is %s (saved in [%s]); /login signs in", pol_id, p->name);
    } else {
        pol_log(m->pol, "register: %s", note);
    }
}

static void chat_register(MENU *m, char *arg)
{
    MENU_PRESET *p = &m->presets[m->current];
    char *name = arg, *password, *extra, pol_id[16], error[160];
    int r;
    if ((password = strchr(name, ' ')) == NULL) {
        pol_log(m->pol, "register: /register name password [code or e-mail]  (/help register)");
        return;
    }
    *password++ = '\0';
    if ((extra = strchr(password, ' ')) != NULL) {
        *extra++ = '\0';
    } else {
        extra = password + strlen(password);
    }
    pol_log(m->pol, "register: asking %s for a new PlayOnline ID for %s...", p->address, name);
    r = m->register_id(p, name, password, extra, pol_id, error, sizeof(error));
    if (r == 0) {
        pol_log(m->pol, "register: %s", error);
    } else if (r == 2) {
        _snprintf(register_password, sizeof(register_password), "%s", password);
        register_password[sizeof(register_password) - 1] = '\0';
        pol_log(m->pol, "register: %s; /verify CODE finishes it", error);
    } else {
        registered(m, pol_id, password, error);
    }
}

static void chat_verify(MENU *m, const char *code)
{
    char pol_id[16], error[160];
    if (!code[0]) {
        pol_log(m->pol, "verify: /verify CODE (the code the server mailed)");
    } else if (!m->register_code(code, pol_id, error, sizeof(error))) {
        pol_log(m->pol, "verify: %s", error);
    } else {
        registered(m, pol_id, register_password, error);
    }
}

/* A line typed on the chat page: a /command, or text for the current channel. A backslash works as the slash:
   the on-screen keyboard of older dashboards has \ but no /. */
static void chat_command(MENU *m, const char *text)
{
    MENU_PRESET *p = &m->presets[m->current];
    char cmd[16], arg[CHAT_TEXT];
    const char *rest;
    int n = 0;
    if (text[0] != '/' && text[0] != '\\') {
        if (m->pol->state != POL_ONLINE) {
            pol_log(m->pol, "not signed in: /login first (/help for commands)");
        } else if (!m->channel[0]) {
            pol_log(m->pol, "no channel: /join #name first");
        } else {
            pol_say(m->pol, m->channel, text);
        }
        return;
    }
    for (rest = text + 1; *rest && *rest != ' ' && n < (int)sizeof(cmd) - 1; rest++) {
        cmd[n++] = *rest;
    }
    cmd[n] = '\0';
    while (*rest == ' ') {
        rest++;
    }
    _snprintf(arg, sizeof(arg), "%s", rest);
    arg[sizeof(arg) - 1] = '\0';
    if (_stricmp(cmd, "help") == 0 || _stricmp(cmd, "man") == 0) {
        chat_help(m, arg);
    } else if (_stricmp(cmd, "login") == 0) {
        if (arg[0]) {
            char *pw = strchr(arg, ' ');
            if (pw != NULL) {
                *pw++ = '\0';
                _snprintf(p->pol_password, sizeof(p->pol_password), "%s", pw);
                p->pol_password[sizeof(p->pol_password) - 1] = '\0';
            }
            _snprintf(p->pol_id, sizeof(p->pol_id), "%s", arg);
            p->pol_id[sizeof(p->pol_id) - 1] = '\0';
            _strupr(p->pol_id);
            m->changed = 1;
        }
        if (!p->pol_id[0]) {
            pol_log(m->pol, "login: the preset has no PlayOnline ID (/login ID password)");
        } else {
            chat_signin_pending = 1;
        }
    } else if (_stricmp(cmd, "logout") == 0) {
        pol_signout(m->pol);
        pol_log(m->pol, "PlayOnline: signed out");
    } else if (_stricmp(cmd, "join") == 0 && arg[0]) {
        _snprintf(m->channel, sizeof(m->channel), "%s%s", arg[0] == '#' ? "" : "#", arg);
        m->channel[sizeof(m->channel) - 1] = '\0';
        m->changed = 1;
        if (!pol_join(m->pol, m->channel)) {
            pol_log(m->pol, "join: %s kept; joined once signed in", m->channel);
        }
    } else if (_stricmp(cmd, "part") == 0) {
        if (pol_part(m->pol, arg[0] ? arg : m->channel) && (!arg[0] || _stricmp(arg, m->channel) == 0)) {
            m->channel[0] = '\0';
            m->changed = 1;
        }
    } else if (_stricmp(cmd, "tell") == 0 && strchr(arg, ' ') != NULL) {
        char *msg = strchr(arg, ' ');
        *msg++ = '\0';
        _strupr(arg);
        if (!pol_say(m->pol, arg, msg)) {
            pol_log(m->pol, "tell: not signed in (/login)");
        }
    } else if (_stricmp(cmd, "say") == 0 && arg[0]) {
        chat_command(m, arg[0] == '/' || arg[0] == '\\' ? arg + 1 : arg);
    } else if (_stricmp(cmd, "profile") == 0) {
        chat_profile(m);
    } else if (_stricmp(cmd, "register") == 0 || _stricmp(cmd, "reg") == 0) {
        chat_register(m, arg);
    } else if (_stricmp(cmd, "verify") == 0) {
        chat_verify(m, arg);
    } else if (_stricmp(cmd, "friends") == 0) {
        chat_friends(m);
    } else if (_stricmp(cmd, "friend") == 0 && strchr(arg, ' ') != NULL) {
        chat_friend(m, arg);
    } else if (_stricmp(cmd, "clear") == 0) {
        m->pol->log_count = 0;
    } else if (_stricmp(cmd, "errors") == 0 && m->errors != NULL) {
        m->errors();
    } else if (_stricmp(cmd, "patches") == 0 && m->patches != NULL) {
        m->patches(arg);
    } else {
        pol_log(m->pol, "%s: command not found or missing arguments (/help %s)", text, cmd);
    }
}

/* Keystrokes from a keyboard, as pad buttons: arrows = d-pad, Enter = A, Escape/Backspace = B, F2 = X,
   PageUp/PageDown = LB/RB, F5 = START. On the chat page, printable keys type into chat_line instead, Backspace
   deletes, Enter sends the line (chat_command) and Escape clears it. Xenia hands the game real keystrokes only
   in keyboard_mode = 2 or 4 (passthrough); with keyboard_mode = 1 the keyboard drives an emulated pad, which
   read_pads already sees. */
static WORD read_keys(MENU *m)
{
    XINPUT_KEYSTROKE k;
    WORD pressed = 0;
    while (XInputGetKeystroke(XUSER_INDEX_ANY, XINPUT_FLAG_KEYBOARD, &k) == ERROR_SUCCESS) {
        if (!(k.Flags & (XINPUT_KEYSTROKE_KEYDOWN | XINPUT_KEYSTROKE_REPEAT))) {
            continue;
        }
        if (chat_page) {
            if (k.VirtualKey == VK_RETURN) {
                if (chat_len > 0) {
                    chat_command(m, chat_line);
                    chat_line[chat_len = 0] = '\0';
                    chat_scroll = 0;
                }
                continue;
            }
            if (k.VirtualKey == VK_BACK) {
                if (chat_len > 0) {
                    chat_line[--chat_len] = '\0';
                }
                continue;
            }
            if (k.VirtualKey == VK_ESCAPE && chat_len > 0) {
                chat_line[chat_len = 0] = '\0';
                continue;
            }
            if ((k.Flags & XINPUT_KEYSTROKE_VALIDUNICODE) && k.Unicode >= 0x20 && k.Unicode < 0x7F) {
                if (chat_len < CHAT_LINE - 1) {
                    chat_line[chat_len++] = (char)k.Unicode;
                    chat_line[chat_len] = '\0';
                }
                continue;
            }
        }
        switch (k.VirtualKey) {
        case VK_UP: pressed |= XINPUT_GAMEPAD_DPAD_UP; break;
        case VK_DOWN: pressed |= XINPUT_GAMEPAD_DPAD_DOWN; break;
        case VK_LEFT: pressed |= XINPUT_GAMEPAD_DPAD_LEFT; break;
        case VK_RIGHT: pressed |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
        case VK_RETURN: pressed |= XINPUT_GAMEPAD_A; break;
        case VK_ESCAPE:
        case VK_BACK: pressed |= XINPUT_GAMEPAD_B; break;
        case VK_F2: pressed |= XINPUT_GAMEPAD_X; break;
        case VK_PRIOR: pressed |= XINPUT_GAMEPAD_LEFT_SHOULDER; break;
        case VK_NEXT: pressed |= XINPUT_GAMEPAD_RIGHT_SHOULDER; break;
        case VK_F5: pressed |= XINPUT_GAMEPAD_START; break;
        }
    }
    return pressed;
}

/* Apply a finished keyboard edit. Returns the item to select afterwards. */
static int finish_edit(MENU *m, int item)
{
    char text[CHAT_TEXT];
    MENU_PRESET *p = &m->presets[m->current];
    int what = editing;
    editing = EDIT_NONE;
    if (!keyboard_result(text, what == EDIT_SAY ? CHAT_TEXT : MENU_TEXT)) {
        return item;
    }
    if (what == EDIT_ADDRESS) {
        strcpy(p->address, text);
    } else if (what == EDIT_DUMP) {
        strcpy(m->packet_dump, _stricmp(text, "off") == 0 ? "" : text);
    } else if (what == EDIT_POLID) {
        _snprintf(p->pol_id, sizeof(p->pol_id), "%s", _stricmp(text, "none") == 0 ? "" : text);
        p->pol_id[sizeof(p->pol_id) - 1] = '\0';
        _strupr(p->pol_id);
    } else if (what == EDIT_POLPASS) {
        _snprintf(p->pol_password, sizeof(p->pol_password), "%s", text);
        p->pol_password[sizeof(p->pol_password) - 1] = '\0';
    } else if (what == EDIT_SAY) {
        chat_command(m, text);      /* chat, not a setting: nothing to save */
        chat_scroll = 0;
        return item;
    } else if (what == EDIT_NAME) {
        _snprintf(p->name, sizeof(p->name), "%s", text);
        p->name[sizeof(p->name) - 1] = '\0';
    } else if (what == EDIT_NEW && m->preset_count < MENU_MAX_PRESETS) {
        MENU_PRESET *fresh = &m->presets[m->preset_count];
        memset(fresh, 0, sizeof(*fresh));
        _snprintf(fresh->name, sizeof(fresh->name), "%s", text);
        fresh->name[sizeof(fresh->name) - 1] = '\0';
        strcpy(fresh->revision, p->revision);
        m->current = m->preset_count++;
        item = ITEM_ADDRESS;
    }
    m->changed = 1;
    return item;
}

static void draw_menu(MENU *m, int item, const MENU_CHECK *info, const char *status)
{
    const MENU_PRESET *p = &m->presets[m->current];
    char server[MENU_TEXT + 16], pol[96];
    mu_Rect bottom;
    mu_begin(&ui);
    if (mu_begin_window_ex(&ui, "launcher", mu_rect(MARGIN_X, MARGIN_Y, SCREEN_W - 2 * MARGIN_X,
                                                    SCREEN_H - 2 * MARGIN_Y),
                           MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOCLOSE)) {
        bar("FINAL FANTASY XI  -  114360 launcher", NULL);
        gap();
        _snprintf(server, sizeof(server), "%s  (%d of %d)", p->name, m->current + 1, m->preset_count);
        server[sizeof(server) - 1] = '\0';
        field("Server", server, item == ITEM_SERVER, 1, WHITE);
        field("Address", p->address[0] ? p->address : "(none)", item == ITEM_ADDRESS, 0, WHITE);
        field("Revision", p->revision[0] ? p->revision : "(none)", item == ITEM_REVISION, 1, WHITE);
        field("Platform", platform_label(p->platform), item == ITEM_PLATFORM, 1, WHITE);
        field("PlayOnline", pol_label(p->pol_version, info->pol, pol, sizeof(pol)), item == ITEM_POL, 1, WHITE);
        /* The game runs the revision's PolCore; the launcher supports one build (engine.c), so this row shows it
           and cannot be changed. */
        field("PolCore", info->pol[0] ? info->pol : "-", 0, 0, GREY);
        field("Client", info->client[0] ? info->client : "-", 0, 0, GREY);
        field("Data", info->data[0] ? info->data : "-", 0, 0, GREY);
        field("Storage", info->storage[0] ? info->storage : "-", 0, 0, GREY);
        field("Language", m->language[0] ? m->language : "PlayOnline's choice", item == ITEM_LANGUAGE, 1, WHITE);
        field("Font", ((const FONT *)ui.style->font)->name, item == ITEM_FONT, 1, WHITE);
        field("Packet dump", m->packet_dump[0] ? m->packet_dump : "off", item == ITEM_DUMP, 0, WHITE);
        field("PlayOnline ID", p->pol_id[0] ? p->pol_id : "none (bridge login)", item == ITEM_POLID, 0, WHITE);
        field("Password", p->pol_password[0] ? "********" : "(none)", item == ITEM_POLPASS, 0, WHITE);
        gap();
        action("Start game", item == ITEM_START, info->problem[0] == '\0');
        action(m->pol != NULL && m->pol->state == POL_ONLINE ? "PlayOnline chat (signed in)" : "PlayOnline chat",
               item == ITEM_CHAT, 1);
        action("New server", item == ITEM_NEW, m->preset_count < MENU_MAX_PRESETS);
        action("Delete server", item == ITEM_DELETE, m->preset_count > 1);
        action("Credits", item == ITEM_CREDITS, 1);
        action("Exit", item == ITEM_EXIT, 1);
        gap();
        field("", status, 0, 0, WHITE);
        if (m->mode != NULL) {
            field("Mode", m->mode, 0, 0, GREY);
        }
        bottom = mu_rect(MARGIN_X, SCREEN_H - MARGIN_Y - cell_h(), SCREEN_W - 2 * MARGIN_X, cell_h());
        bar("A Select   Up/Down Move   Left/Right Change   START Play", &bottom);
        mu_end_window(&ui);
    }
    mu_end(&ui);
}

/* The chat page, terminal style: a status bar on top, the session's log filling the screen with the newest line
   at the bottom (long lines wrapped), and a prompt line under it. A types a line (system keyboard), X signs in
   or out, LB/RB scroll back through the log (POL_LOG_LINES lines), B goes back to the menu. */
struct CHAT_ROW {
    int line, off, len;
};

static void draw_chat(MENU *m, const char *status)
{
    static CHAT_ROW vrows[POL_LOG_LINES * 4];
    const MENU_PRESET *p = &m->presets[m->current];
    const POL_SESSION *s = m->pol;
    char line[POL_LOG_TEXT + 64];
    mu_Rect r;
    int rows, cols, kept, count = 0, n, i, len, k, top;
    mu_begin(&ui);
    if (mu_begin_window_ex(&ui, "launcher", mu_rect(MARGIN_X, MARGIN_Y, SCREEN_W - 2 * MARGIN_X,
                                                    SCREEN_H - 2 * MARGIN_Y),
                           MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOCLOSE)) {
        if (s->state == POL_ONLINE) {
            _snprintf(line, sizeof(line), "PlayOnline  %s@%s  %s", s->pol_id, p->address,
                      m->channel[0] ? m->channel : "(no channel: /join #name)");
        } else if (s->state == POL_FAILED) {
            _snprintf(line, sizeof(line), "PlayOnline  not signed in: %s", s->error);
        } else {
            _snprintf(line, sizeof(line), "PlayOnline  not signed in (/login, /register, /help)");
        }
        line[sizeof(line) - 1] = '\0';
        bar(line, NULL);

        /* Visual rows, newest first: each log line cut into screen-wide pieces. */
        rows = (SCREEN_H - 2 * MARGIN_Y) / cell_h() - 3;
        cols = (SCREEN_W - 2 * MARGIN_X) / cell_w() - 1;
        kept = s->log_count < POL_LOG_LINES ? s->log_count : POL_LOG_LINES;
        for (n = s->log_count - 1; n >= s->log_count - kept && count < (int)(sizeof(vrows) / sizeof(vrows[0])); n--) {
            len = (int)strlen(s->log[n % POL_LOG_LINES]);
            for (k = len > 0 ? (len - 1) / cols : 0; k >= 0 && count < (int)(sizeof(vrows) / sizeof(vrows[0])); k--) {
                vrows[count].line = n % POL_LOG_LINES;
                vrows[count].off = k * cols;
                vrows[count].len = len - k * cols < cols ? len - k * cols : cols;
                count++;
            }
        }
        if (chat_scroll > count - rows) {
            chat_scroll = count - rows > 0 ? count - rows : 0;
        }
        top = MARGIN_Y + cell_h();
        for (i = 0; i < rows; i++) {
            int v = chat_scroll + rows - 1 - i;
            if (v >= count) {
                continue;
            }
            memcpy(line, s->log[vrows[v].line] + vrows[v].off, vrows[v].len);
            line[vrows[v].len] = '\0';
            text_at(mu_rect(MARGIN_X, top + i * cell_h(), SCREEN_W - 2 * MARGIN_X, cell_h()), line, WHITE);
        }

        /* The prompt (or what the launcher is doing), then the key help. */
        if (status[0]) {
            _snprintf(line, sizeof(line), "%s", status);
        } else if (chat_scroll > 0) {
            _snprintf(line, sizeof(line), "-- %d more below (RB) --", chat_scroll);
        } else {
            _snprintf(line, sizeof(line), "%s> %s_", s->state == POL_ONLINE ? s->pol_id : "PlayOnline", chat_line);
        }
        line[sizeof(line) - 1] = '\0';
        r = mu_rect(MARGIN_X, SCREEN_H - MARGIN_Y - 2 * cell_h(), SCREEN_W - 2 * MARGIN_X, cell_h());
        text_at(r, line, status[0] ? GREY : WHITE);
        r = mu_rect(MARGIN_X, SCREEN_H - MARGIN_Y - cell_h(), SCREEN_W - 2 * MARGIN_X, cell_h());
        bar(s->state == POL_ONLINE ? "Type or A   X/F2 Sign out   LB/RB/PgUp/PgDn Scroll   B/Esc Back   START/F5 Play"
                                   : "Type or A   X/F2 Sign in   LB/RB/PgUp/PgDn Scroll   B/Esc Back   START/F5 Play", &r);
        mu_end_window(&ui);
    }
    mu_end(&ui);
}

/* The credits page: a title bar, the credits text word-wrapped to the font's width (Up/Down and LB/RB scroll),
   the key help. */
static void draw_credits(void)
{
    static CHAT_ROW vrows[CREDITS_LINES * 3];
    char line[128];
    mu_Rect r;
    int rows, cols, count = 0, i, top, len, off, cut;
    mu_begin(&ui);
    if (mu_begin_window_ex(&ui, "launcher", mu_rect(MARGIN_X, MARGIN_Y, SCREEN_W - 2 * MARGIN_X,
                                                    SCREEN_H - 2 * MARGIN_Y),
                           MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOCLOSE)) {
        bar("Credits and licenses", NULL);
        rows = (SCREEN_H - 2 * MARGIN_Y) / cell_h() - 2;
        cols = (SCREEN_W - 2 * MARGIN_X) / cell_w() - 1;
        if (cols > (int)sizeof(line) - 1) {
            cols = (int)sizeof(line) - 1;
        }
        for (i = 0; i < CREDITS_LINES; i++) {
            len = (int)strlen(credits[i]);
            off = 0;
            do {
                cut = len - off;
                if (cut > cols) {
                    cut = cols;
                    while (cut > 0 && credits[i][off + cut] != ' ') {
                        cut--;
                    }
                    if (cut == 0) {
                        cut = cols;
                    }
                }
                if (count < (int)(sizeof(vrows) / sizeof(vrows[0]))) {
                    vrows[count].line = i;
                    vrows[count].off = off;
                    vrows[count].len = cut;
                    count++;
                }
                off += cut;
                while (off < len && credits[i][off] == ' ') {
                    off++;
                }
            } while (off < len);
        }
        if (credits_scroll > count - rows) {
            credits_scroll = count - rows > 0 ? count - rows : 0;
        }
        top = MARGIN_Y + cell_h();
        for (i = 0; i < rows && credits_scroll + i < count; i++) {
            const CHAT_ROW *v = &vrows[credits_scroll + i];
            memcpy(line, credits[v->line] + v->off, v->len);
            line[v->len] = '\0';
            text_at(mu_rect(MARGIN_X, top + i * cell_h(), SCREEN_W - 2 * MARGIN_X, cell_h()), line, WHITE);
        }
        r = mu_rect(MARGIN_X, SCREEN_H - MARGIN_Y - cell_h(), SCREEN_W - 2 * MARGIN_X, cell_h());
        bar("Up/Down LB/RB/PgUp/PgDn Scroll   B/Esc Back", &r);
        mu_end_window(&ui);
    }
    mu_end(&ui);
}

static IDirect3DVertexShader9 *compile_vertex(void)
{
    ID3DXBuffer *code = NULL, *errors = NULL;
    IDirect3DVertexShader9 *vs = NULL;
    if (D3DXCompileShader(vertex_source, sizeof(vertex_source) - 1, NULL, NULL, "main", "vs_3_0", 0, &code, &errors,
                          NULL) >= 0) {
        dev->CreateVertexShader((const DWORD *)code->GetBufferPointer(), &vs);
        code->Release();
    } else if (errors != NULL) {
        DbgPrint("114360: menu vertex shader: %s\n", (const char *)errors->GetBufferPointer());
    }
    if (errors != NULL) {
        errors->Release();
    }
    return vs;
}

static IDirect3DPixelShader9 *compile_pixel(void)
{
    ID3DXBuffer *code = NULL, *errors = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    if (D3DXCompileShader(pixel_source, sizeof(pixel_source) - 1, NULL, NULL, "main", "ps_3_0", 0, &code, &errors,
                          NULL) >= 0) {
        dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps);
        code->Release();
    } else if (errors != NULL) {
        DbgPrint("114360: menu pixel shader: %s\n", (const char *)errors->GetBufferPointer());
    }
    if (errors != NULL) {
        errors->Release();
    }
    return ps;
}

/* Background: DOS black, or DOS blue for a preset that signs in to PlayOnline on the console (rather than the
   bridge's direct login), so the two are told apart at a glance. */
static D3DCOLOR background(const MENU_PRESET *p)
{
    return p->pol_id[0] ? D3DCOLOR_XRGB(0, 0, 170) : D3DCOLOR_XRGB(0, 0, 0);
}

static void frame(MENU *m, int item, const MENU_CHECK *info, const char *status, IDirect3DVertexDeclaration9 *decl,
                  IDirect3DVertexShader9 *vs, IDirect3DPixelShader9 *ps)
{
    dev->Clear(0, NULL, D3DCLEAR_TARGET, background(&m->presets[m->current]), 1.0f, 0);
    dev->SetVertexDeclaration(decl);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    if (chat_page) {
        draw_chat(m, status);
    } else if (credits_page) {
        draw_credits();
    } else {
        draw_menu(m, item, info, status);
    }
    render(&ui);
    dev->Present(NULL, NULL, NULL, NULL);
}

/* Sign in for the current preset (blocking for a few seconds), with a frame saying so first. */
static int sign_in(MENU *m, int item, const MENU_CHECK *info, IDirect3DVertexDeclaration9 *decl,
                   IDirect3DVertexShader9 *vs, IDirect3DPixelShader9 *ps)
{
    char status[96];
    _snprintf(status, sizeof(status), "Signing in to PlayOnline as %s...", m->presets[m->current].pol_id);
    status[sizeof(status) - 1] = '\0';
    frame(m, item, info, status, decl, vs, ps);
    return m->signin(&m->presets[m->current]);
}

/* The menu loop proper. Returns MENU_START or MENU_EXIT. */
static int run(MENU *m, IDirect3DVertexDeclaration9 *decl, IDirect3DVertexShader9 *vs, IDirect3DPixelShader9 *ps)
{
    MENU_CHECK info;
    char checked[MENU_TEXT + 1], status[160];
    int item = ITEM_START, checked_preset = -1;
    LONG checked_install = -1;
    DWORD deadline = 0, flash_until = 0, port = 0;
    int counting = m->autostart > 0 && !m->returned;

    status[0] = '\0';
    checked[0] = '\0';
    for (;;) {
        MENU_PRESET *p = &m->presets[m->current];
        DWORD now = GetTickCount();
        WORD pressed;
        int start = 0;

        /* The revision check reads files: redo it only when the preset or its revision changes, or when the
           launcher's install check (a thread) finished another revision. A failed install check stops the
           countdown: the game would crash on that data; START still starts it. */
        if (checked_preset != m->current || strcmp(checked, p->revision) != 0 || checked_install != m->install_done) {
            memset(&info, 0, sizeof(info));
            checked_install = m->install_done;
            m->check(p, &info);
            checked_preset = m->current;
            strcpy(checked, p->revision);
            if (info.problem[0] != '\0' || strstr(info.storage, " E") != NULL) {
                counting = 0;
            }
        }
        if (counting && deadline == 0) {
            deadline = now + m->autostart * 1000;
        }
        if (m->pol->state == POL_ONLINE) {
            pol_poll(m->pol, 0);
        }
        if (chat_signin_pending) {
            chat_signin_pending = 0;
            if (sign_in(m, item, &info, decl, vs, ps) && m->channel[0]) {
                pol_join(m->pol, m->channel);
            }
        }

        if (editing != EDIT_NONE) {
            if (XHasOverlappedIoCompleted(&keyboard_wait)) {
                item = finish_edit(m, item);
            }
            pressed = 0;
        } else {
            pressed = read_pads(&port) | read_keys(m);
        }
        if (pressed != 0 && counting) {
            counting = 0;
        }
        if (credits_page) {
            if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_LEFT_SHOULDER)) {
                credits_scroll -= pressed & XINPUT_GAMEPAD_LEFT_SHOULDER ? 10 : 1;
                if (credits_scroll < 0) credits_scroll = 0;
            }
            if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) credits_scroll += 1;
            if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) credits_scroll += 10;
            if (pressed & XINPUT_GAMEPAD_B) credits_page = 0;
        } else if (chat_page) {
            if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) chat_scroll += 4;
            if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) chat_scroll = chat_scroll > 4 ? chat_scroll - 4 : 0;
            if (pressed & XINPUT_GAMEPAD_B) chat_page = 0;
            if (pressed & XINPUT_GAMEPAD_X) {
                if (m->pol->state == POL_ONLINE) {
                    pol_signout(m->pol);
                    pol_log(m->pol, "PlayOnline: signed out");
                } else if (p->pol_id[0]) {
                    chat_signin_pending = 1;
                } else {
                    pol_log(m->pol, "no PlayOnline ID in [%s]: /login ID password or /register", p->name);
                }
            }
            if (pressed & XINPUT_GAMEPAD_A) {
                open_keyboard(port, EDIT_SAY, "", L"PlayOnline chat",
                              L"Text for the channel, or a command: /help, /login, /register, /tell ID text");
            }
            start = (pressed & XINPUT_GAMEPAD_START) != 0;
        } else {
            if (pressed & XINPUT_GAMEPAD_DPAD_UP) item = (item + ITEM_COUNT - 1) % ITEM_COUNT;
            if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) item = (item + 1) % ITEM_COUNT;
            if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) change(m, item, -1);
            if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) change(m, item, 1);
            if (pressed & XINPUT_GAMEPAD_A) {
                switch (item) {
                case ITEM_SERVER:
                    open_keyboard(port, EDIT_NAME, p->name, L"Server name", L"Name of this server preset");
                    break;
                case ITEM_ADDRESS:
                    open_keyboard(port, EDIT_ADDRESS, p->address, L"Server address",
                                  L"IPv4 address (no DNS lookup) or host name of the lobby server");
                    break;
                case ITEM_DUMP:
                    open_keyboard(port, EDIT_DUMP, m->packet_dump, L"Packet dump",
                                  L"IPv4:port to send every server packet to (tools/packet-dump-recv.py), or off");
                    break;
                case ITEM_POLID:
                    open_keyboard(port, EDIT_POLID, p->pol_id, L"PlayOnline ID",
                                  L"PlayOnline ID to sign in with on this server (none = the bridge's direct login)");
                    break;
                case ITEM_POLPASS:
                    open_keyboard(port, EDIT_POLPASS, "", L"PlayOnline password", L"Password for this PlayOnline ID");
                    break;
                case ITEM_CHAT:
                    chat_page = 1;
                    break;
                case ITEM_CREDITS:
                    credits_page = 1;
                    credits_scroll = 0;
                    break;
                case ITEM_NEW:
                    if (m->preset_count < MENU_MAX_PRESETS) {
                        open_keyboard(port, EDIT_NEW, "", L"New server", L"Name of the new server preset");
                    }
                    break;
                case ITEM_DELETE:
                    if (m->preset_count > 1) {
                        memmove(&m->presets[m->current], &m->presets[m->current + 1],
                                (m->preset_count - m->current - 1) * sizeof(MENU_PRESET));
                        m->preset_count--;
                        if (m->current >= m->preset_count) {
                            m->current = m->preset_count - 1;
                        }
                        m->changed = 1;
                    }
                    break;
                case ITEM_EXIT:
                    return MENU_EXIT;
                case ITEM_START:
                    break;
                default:
                    change(m, item, 1);
                    break;
                }
            }
            start = (pressed & XINPUT_GAMEPAD_START) || ((pressed & XINPUT_GAMEPAD_A) && item == ITEM_START);
        }
        if (start || (counting && (int)(now - deadline) >= 0)) {
            if (info.problem[0] != '\0') {
                flash_until = now + 2000;
            } else if (p->pol_id[0] && (m->pol->state != POL_ONLINE || _stricmp(m->pol->pol_id, p->pol_id) != 0)
                       && !sign_in(m, item, &info, decl, vs, ps)) {
                /* A preset that signs in to PlayOnline does not start without a sign-in: stay in the menu with
                   the reason, and stop any countdown. */
                counting = 0;
                flash_until = now + 4000;
            } else {
                return MENU_START;
            }
        }

        /* The chat page shows a status in place of its prompt, so it gets only the passing ones (a refused
           start, the countdown); the lasting ones (a preset's problem, back from the game) stay on the menu. */
        if (info.problem[0] != '\0' && (!chat_page || (int)(flash_until - now) > 0)) {
            _snprintf(status, sizeof(status), "%s%s", (int)(flash_until - now) > 0 ? "Cannot start: " : "",
                      info.problem);
        } else if (m->pol->state == POL_FAILED && (int)(flash_until - now) > 0) {
            _snprintf(status, sizeof(status), "Cannot start: PlayOnline %s", m->pol->error);
        } else if (counting) {
            _snprintf(status, sizeof(status), "Starting in %d s. Press any button to stay here.",
                      (int)((deadline - now + 999) / 1000));
        } else if (m->returned && !chat_page) {
            _snprintf(status, sizeof(status), "Back from the game. START plays again.");
        } else {
            status[0] = '\0';
        }
        status[sizeof(status) - 1] = '\0';
        frame(m, item, &info, status, decl, vs, ps);
    }
}

extern "C" int menu_run(MENU *m)
{
    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END()
    };
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    IDirect3DVertexDeclaration9 *decl = NULL;
    IDirect3DVertexShader9 *vs = NULL;
    IDirect3DPixelShader9 *ps = NULL;
    D3DPRESENT_PARAMETERS pp;
    int result = MENU_START;

    memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth = SCREEN_W;
    pp.BackBufferHeight = SCREEN_H;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;   /* alpha: the coverage of the string being drawn (render_text) */
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (d3d == NULL || d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) < 0) {
        DbgPrint("114360: menu: no Direct3D device\n");
        return MENU_START;
    }
    mu_init(&ui);
    ui.text_width = text_width;
    ui.text_height = text_height;
    {
        /* SD or 4:3 output: the larger safe area. */
        XVIDEO_MODE mode;
        XGetVideoMode(&mode);
        if (!mode.fIsWideScreen || mode.dwDisplayHeight < 720) {
            margin_x = SCREEN_W / 10;
            margin_y = SCREEN_H / 10;
        }
        DbgPrint("114360: menu: video %ux%u%s, safe margins %d x %d\n", mode.dwDisplayWidth, mode.dwDisplayHeight,
                 mode.fIsWideScreen ? " widescreen" : " 4:3", margin_x, margin_y);
    }
    set_font(font_named(m->font));
    ui.style->padding = 0;
    ui.style->spacing = 0;
    vs = compile_vertex();
    ps = compile_pixel();
    if (vs != NULL && ps != NULL && dev->CreateVertexDeclaration(elements, &decl) >= 0) {
        result = run(m, decl, vs, ps);
    }
    if (decl != NULL) {
        decl->Release();
    }
    if (ps != NULL) {
        dev->SetPixelShader(NULL);
        ps->Release();
    }
    if (vs != NULL) {
        dev->SetVertexShader(NULL);
        vs->Release();
    }
    dev->Release();
    dev = NULL;
    d3d->Release();
    return result;
}
