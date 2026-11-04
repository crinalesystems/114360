// dg_presdl.h
// SPDX-FileCopyrightText: 2026 Miguel Dias De Freitas <miguel@crinalesystems.com>
// SPDX-License-Identifier: BSD-3-Clause
// DragonGuilt PreSDL — Agnostic Preconfig UI via SDL_Renderer

#pragma once

#if !defined(__cplusplus) && __STDC_VERSION__ < 202311L
#error "This file requires C23 or later."
#endif

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Real glyph rendering: FreeType rasterizes, Fontconfig resolves family names
// and enumerates what is installed. Define DG_PRESDL_NO_FREETYPE before the
// include to build without either — every text call then falls back to SDL's
// 8x8 debug font and every metric to its fixed 8px cell, exactly as this
// header behaved before the atlas existed.
#ifndef DG_PRESDL_NO_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#endif


// ─── Colours (simple palette) ──────────────────────────────────────────────

// Colours: define any of these before including this header to theme it (each is { r, g, b, a }).
#ifndef DG_PRESDL_COL_BG
#define DG_PRESDL_COL_BG          {  18,  18,  24, 255 }
#endif
#ifndef DG_PRESDL_COL_PANEL
#define DG_PRESDL_COL_PANEL       {  28,  28,  36, 255 }
#endif
#ifndef DG_PRESDL_COL_ACCENT
#define DG_PRESDL_COL_ACCENT      {  80,  80, 200, 255 }
#endif
#ifndef DG_PRESDL_COL_ACCENT_HOV
#define DG_PRESDL_COL_ACCENT_HOV  { 100, 100, 220, 255 }
#endif
#ifndef DG_PRESDL_COL_ON_ACCENT                         // text on an accent fill (buttons, hovered option)
#define DG_PRESDL_COL_ON_ACCENT   { 220, 220, 220, 255 }
#endif
#ifndef DG_PRESDL_COL_TEXT
#define DG_PRESDL_COL_TEXT        { 220, 220, 220, 255 }
#endif
#ifndef DG_PRESDL_COL_TEXT_DIM
#define DG_PRESDL_COL_TEXT_DIM    { 130, 130, 140, 255 }
#endif
#ifndef DG_PRESDL_COL_BORDER
#define DG_PRESDL_COL_BORDER      {  50,  50,  65, 255 }
#endif
#ifndef DG_PRESDL_COL_CHECK_ON
#define DG_PRESDL_COL_CHECK_ON    {  80, 200, 120, 255 }
#endif
#ifndef DG_PRESDL_COL_HOVER                             // a hovered row (checkbox)
#define DG_PRESDL_COL_HOVER       {  35,  35,  45, 255 }
#endif
#ifndef DG_PRESDL_COL_FIELD                             // text fields, key boxes, dropdown lists
#define DG_PRESDL_COL_FIELD       {  24,  24,  34, 255 }
#endif
#ifndef DG_PRESDL_COL_DROPDOWN                          // a closed dropdown's box, and hovered or open
#define DG_PRESDL_COL_DROPDOWN    {  30,  30,  40, 255 }
#endif
#ifndef DG_PRESDL_COL_DROPDOWN_HOV
#define DG_PRESDL_COL_DROPDOWN_HOV {  45,  45,  60, 255 }
#endif
#ifndef DG_PRESDL_COL_SELECTED                          // the selected option in an open list
#define DG_PRESDL_COL_SELECTED    {  40,  40,  70, 255 }
#endif
#ifndef DG_PRESDL_COL_SECTION_TITLE
#define DG_PRESDL_COL_SECTION_TITLE {  22,  22,  30, 255 }
#endif
#ifndef DG_PRESDL_COL_SCROLL_TRACK
#define DG_PRESDL_COL_SCROLL_TRACK {  30,  30,  40, 255 }
#endif
#ifndef DG_PRESDL_COL_TAB_ACTIVE
#define DG_PRESDL_COL_TAB_ACTIVE  {  80,  80, 200, 255 }
#endif
#ifndef DG_PRESDL_COL_TAB_IDLE
#define DG_PRESDL_COL_TAB_IDLE    {  38,  38,  50, 255 }
#endif
#ifndef DG_PRESDL_COL_TAB_HOV
#define DG_PRESDL_COL_TAB_HOV     {  48,  48,  64, 255 }
#endif
#ifndef DG_PRESDL_COL_TAB_MARK                          // the active tab's underline
#define DG_PRESDL_COL_TAB_MARK    { 120, 120, 255, 255 }
#endif
#ifndef DG_PRESDL_COL_TAB_TEXT                          // the active tab's title
#define DG_PRESDL_COL_TAB_TEXT    { 255, 255, 255, 255 }
#endif

// ─── Layout constants ──────────────────────────────────────────────────────

#define DG_PRESDL_TAB_H          36
#define DG_PRESDL_ENTRY_H        38
#define DG_PRESDL_ENTRY_PAD       8
#define DG_PRESDL_SECTION_PAD    12
#define DG_PRESDL_SCROLL_SPEED   20
#define DG_PRESDL_FONT_SCALE      2   // SDL_RenderDebugText scale multiplier (1 char = 8*scale px)
#define DG_PRESDL_DEBUG_CELL      8   // the debug font's cell, px — the fallback metric
// Default rasterizer size: the debug cell times the historical scale, so an app
// that never calls dg_presdl_font_set() keeps the same visual weight.
#define DG_PRESDL_FONT_PX        (DG_PRESDL_DEBUG_CELL * DG_PRESDL_FONT_SCALE)
#define DG_PRESDL_ATLAS_PX      512   // glyph sheet edge, px
#define DG_PRESDL_SLIDER_TRACK_H  8   // bar thickness, not a font metric

// ─── UTF-8 helpers ─────────────────────────────────────────────────────────

// Text arrives as multi-byte UTF-8 from SDL_EVENT_TEXT_INPUT, so byte-wise
// editing corrupts a field the moment a non-ASCII character is typed. They live
// up here because the glyph cache below is keyed by code point.

static inline bool impl_dg_utf8_cont_(unsigned char c) { return (c & 0xC0u) == 0x80u; }

static inline size_t impl_dg_utf8_seqlen_(unsigned char c) {
    if (c < 0x80u)           return 1;
    if ((c & 0xE0u) == 0xC0u) return 2;
    if ((c & 0xF0u) == 0xE0u) return 3;
    if ((c & 0xF8u) == 0xF0u) return 4;
    return 1;   // stray continuation byte: consume it alone so we always advance
}

// Byte offset of the start of the code point ending at s[len-1].
static inline size_t impl_dg_utf8_prev_(const char* s, size_t len) {
    if (len == 0) return 0;
    size_t i = len - 1;
    while (i > 0 && impl_dg_utf8_cont_((unsigned char)s[i])) i--;
    return i;
}

// Code points in the first `bytes` bytes.
static inline size_t impl_dg_utf8_count_(const char* s, size_t bytes) {
    size_t n = 0;
    for (size_t i = 0; i < bytes; i++)
        if (!impl_dg_utf8_cont_((unsigned char)s[i])) n++;
    return n;
}

// Byte offset of the last `want` code points of s[0..bytes).
static inline size_t impl_dg_utf8_tail_(const char* s, size_t bytes, size_t want) {
    size_t i = bytes;
    while (i > 0 && want > 0) {
        i = impl_dg_utf8_prev_(s, i);
        want--;
    }
    return i;
}

// Append what fits of `add` to a NUL-terminated dst of capacity `cap`, never
// leaving a half-written code point behind.
static inline void impl_dg_utf8_append_(char* dst, size_t dstLen, size_t cap, const char* add) {
    if (cap == 0 || dstLen + 1 >= cap) return;
    size_t room = cap - dstLen - 1;
    size_t n    = strlen(add);
    if (n > room) n = room;
    if (n > 0) {
        size_t start = impl_dg_utf8_prev_(add, n);
        if (start + impl_dg_utf8_seqlen_((unsigned char)add[start]) > n)
            n = start;   // the cut landed mid-sequence — drop the partial char
    }
    memcpy(dst + dstLen, add, n);
    dst[dstLen + n] = '\0';
}


// Decode the code point starting at *i, advancing *i past it. Malformed or
// truncated sequences consume exactly one byte, so the walk always terminates.
static inline uint32_t impl_dg_utf8_decode_(const char* s, size_t bytes, size_t* i) {
    size_t p = *i;
    unsigned char c = (unsigned char)s[p];
    size_t n = impl_dg_utf8_seqlen_(c);
    if (n > bytes - p) n = 1;
    uint32_t cp;
    switch (n) {
        case 2:  cp = (uint32_t)(c & 0x1Fu); break;
        case 3:  cp = (uint32_t)(c & 0x0Fu); break;
        case 4:  cp = (uint32_t)(c & 0x07u); break;
        default: cp = c; n = 1; break;
    }
    for (size_t k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[p + k];
        if (!impl_dg_utf8_cont_(cc)) { cp = c; n = k; break; }
        cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
    }
    *i = p + n;
    // U+0000 is the glyph cache's empty-slot marker and never renders anyway.
    return cp ? cp : 0xFFFDu;
}

// ─── Glyph atlas types ─────────────────────────────────────────────────────

// One cached glyph: where it sits in an atlas sheet and how to place it on the
// baseline. Atlas < 0 means "no bitmap" (space, or a glyph FreeType refused —
// cached all the same so a failing load is not retried every frame).
typedef struct dg_presdl_glyph {
    uint32_t Code;      // 0 = free slot
    float  Adv;       // pen advance, px
    int    X, Y;      // position inside the sheet
    int    W, H;      // bitmap size
    int    Bx, By;    // FreeType bitmap_left / bitmap_top
    int    Atlas;     // sheet index, -1 = blank
} dg_presdl_glyph;

// A rasterizer bound to one renderer: SDL textures belong to the renderer that
// created them, so a second window cannot share these sheets — only the face
// and size behind them. FT_Library/FT_Face are held as void* so this struct,
// and every public struct embedding it, keeps one shape in both build modes.
typedef struct dg_presdl_font {
    void*            Lib;        // FT_Library
    void*            Face;       // FT_Face
    SDL_Renderer*    Renderer;
    SDL_Texture**    Atlases;
    int              AtlasCount;
    int              PenX, PenY, RowH;  // shelf-packing cursor in the last sheet
    dg_presdl_glyph* Slots;      // open-addressed cache, SlotCount a power of two
    uint32_t         SlotCount;
    uint32_t         SlotUsed;
    int              PixelSize;
    float            Ascent, Descent;   // px, from the sized face metrics
    bool             Ready;      // false -> every caller falls back to the debug font
    char             Path[512];
    char             Family[128];
} dg_presdl_font;

// ─── Value types ───────────────────────────────────────────────────────────

typedef enum dg_presdl_preconfig_value_type {
    DG_PRESDL_GROUPING,
    DG_PRESDL_IMAGE,
    DG_PRESDL_SECTIONIONING,
    DG_PRESDL_CHECKBOX,
    DG_PRESDL_BUTTON,
    DG_PRESDL_INSERT_32,
    DG_PRESDL_INSERT_64,
    DG_PRESDL_SLIDER_32,
    DG_PRESDL_SLIDER_64,
    DG_PRESDL_STEPPER_SLIDER,
    DG_PRESDL_DROPDOWN,
    DG_PRESDL_TEXT,
    DG_PRESDL_MONITORS,   // append below: existing values must keep their numbers
    DG_PRESDL_KEYBIND,
    DG_PRESDL_PROMPT,
    DG_PRESDL_PARAGRAPH,  // Label as body text, word-wrapped to the entry's width ('\n' = new paragraph)
} dg_sdl_preconfig_value_type;

// ─── Complements ───────────────────────────────────────────────────────────

typedef struct dg_presdl_complement_checkbox {
    bool*       Value;
    const char* HelpText;
} dg_presdl_complement_checkbox;

typedef struct dg_presdl_complement_dropdown {
    const char** Options;
    uint32_t     OptionCount;
    uint32_t*    Selected;
    bool         Open;       // internal — is dropdown expanded
    float        Scroll;     // internal — first visible option row while open
    const char*  HelpText;
} dg_presdl_complement_dropdown;

typedef struct dg_presdl_complement_slider_32 {
    float*      Value;
    float       Min;
    float       Max;
    float       Step;
    bool        Dragging;    // internal
    const char* HelpText;
} dg_presdl_complement_slider_32;

typedef struct dg_presdl_complement_button {
    const char* Label;
    void (*OnClick)(void* userdata);
    void*       UserData;
} dg_presdl_complement_button;

// A single-line editable text field. Buffer is caller-owned and edited in place
// (NUL-terminated, never exceeding BufSize-1 chars). Text input is delivered via
// SDL's platform-agnostic text-input events, so no font/toolkit assumptions.
typedef struct dg_presdl_complement_text {
    char*       Buffer;
    size_t      BufSize;
    const char* HelpText;
    const char* Placeholder; // shown dimmed when the buffer is empty
} dg_presdl_complement_text;

typedef struct dg_presdl_complement_image {
    SDL_Texture* Texture;
    int          W, H;
} dg_presdl_complement_image;

// ─── Monitor arrangement canvas ────────────────────────────────────────────

// One monitor in the arrangement canvas.
typedef struct dg_presdl_monitor {
    const char *Name;      // connector name, e.g. "eDP-1"
    int         X, Y;      // layout position in output pixels; EDITED BY DRAGGING
    int         W, H;      // logical size (mode size / scale), read-only to the widget
    bool        Enabled;   // disabled monitors draw dimmed and are not draggable
    bool        Internal;  // built-in laptop panel (drawn with a distinct accent)
} dg_presdl_monitor;

typedef struct dg_presdl_complement_monitors {
    dg_presdl_monitor *Monitors;
    uint32_t           Count;
    uint32_t          *Selected;  // index of highlighted monitor; may be nullptr
    float              Height;    // canvas height in px; 0 -> 180
    const char        *HelpText;
} dg_presdl_complement_monitors;

// ─── Key-capture row ───────────────────────────────────────────────────────

// One rebindable action: the binding text, an optional toggle-vs-held box and
// a [D] button that restores the caller's default. Clicking the binding box
// arms capture; the next non-modifier key press is handed to Format(), which
// owns the naming scheme (the widget knows SDL keycodes, not what the consumer
// calls them). Esc or a click elsewhere cancels.
typedef struct dg_presdl_complement_keybind {
    char*  Buffer;    // caller-owned binding text, rewritten on capture
    size_t BufSize;
    bool*  Toggle;    // NULL -> this action has no toggle-vs-held choice
    void (*Format)(void* userdata, SDL_Keycode key, SDL_Keymod mods,
                   char* out, size_t size);
    void (*OnDefault)(void* userdata); // [D] — restore Buffer/Toggle defaults
    void*  UserData;
    const char* HelpText;
} dg_presdl_complement_keybind;

// ─── Button-opened text prompt ─────────────────────────────────────────────

// A read-only mirror of Buffer plus a button that opens a modal text prompt
// (dg_presdl_prompt_text) to fill it. Buffer is caller-owned and only ever
// written when the dialog is accepted; Changed is set on that accept and is the
// caller's to clear.
typedef struct dg_presdl_complement_prompt {
    char*       Buffer;
    size_t      Capacity;
    const char* Title;        // dialog title bar; falls back to the entry Label
    const char* Message;      // one-line explanation above the field, may be NULL
    const char* ButtonLabel;  // defaults to "Edit..."
    bool        Changed;
} dg_presdl_complement_prompt;

typedef union dg_presdl_complement {
    dg_presdl_complement_checkbox  Checkbox;
    dg_presdl_complement_dropdown  Dropdown;
    dg_presdl_complement_slider_32 Slider32;
    dg_presdl_complement_button    Button;
    dg_presdl_complement_image     Image;
    dg_presdl_complement_text      Text;
    dg_presdl_complement_monitors  Monitors;
    dg_presdl_complement_keybind   Keybind;
    dg_presdl_complement_prompt    Prompt;
} dg_presdl_complement;

// ─── Entry ─────────────────────────────────────────────────────────────────

typedef struct dg_presdl_entry {
    const char*                 Label;
    dg_sdl_preconfig_value_type Type;
    dg_presdl_complement        Complement;
    uint32_t                    SectionIndex;
} dg_presdl_entry;

// ─── Section — bounded WxH region inside a page ────────────────────────────

typedef struct dg_presdl_section {
    const char* Title;
    uint32_t    EntryStart;  // index into root Entries array
    uint32_t    EntryCount;
    // bounds within the page content area (0,0 = page origin)
    float       X, Y;        // top-left offset
    float       W, H;        // explicit size — 0 = auto (fill width, auto height)
} dg_presdl_section;

// ─── Page — tab entry with scrolling ───────────────────────────────────────

typedef struct dg_presdl_page {
    const char*        Title;
    SDL_Texture*       HeaderImage;
    int                HeaderImageH;  // 0 = no header image
    dg_presdl_section* Sections;
    uint32_t           SectionCount;
    float              ScrollY;       // internal scroll offset
    float              ContentH;      // internal total content height
} dg_presdl_page;

// ─── Root window ───────────────────────────────────────────────────────────

typedef struct dg_presdl_preconfig_window {
    dg_presdl_entry*  Entries;
    uint64_t          EntryCount;
    dg_presdl_page*   Pages;
    uint32_t          PageCount;
    uint32_t          CurrentPage;
    uint32_t          PrevPage;
    SDL_Window*       Window;
    SDL_Renderer*     Renderer;
    bool              ShouldClose;   // set true by a button OnClick to break loop
    int               W, H;          // current window size
    int               ActiveEntryIndex;

    // Internal pointer state: activation happens on click-release (not press) so
    // a vertical drag scrolls the page instead of triggering a widget.
    bool              _pressed;      // left button currently held
    bool              _dragged;      // moved past the drag threshold since press
    float             _pressY;       // pointer Y at button-down
    float             _pressScrollY; // page ScrollY at button-down

    // Text-field editing: which entry has keyboard focus, and the input that
    // arrived this frame (applied by the focused text widget while drawing).
    int               TextFocus;     // entry index of the focused text field, -1 none
    char              _textInput[32];// UTF-8 text typed this frame
    int               _backspaces;   // backspaces pressed this frame
    char*             _paste;        // clipboard text pasted this frame (Ctrl+V, Shift+Insert), else NULL

    // Key capture. While an entry is armed it swallows every key press: the
    // point is to bind Esc or PageDown as readily as any other key.
    int               KeyCapture;    // entry index arming capture, -1 none
    SDL_Keycode       _capKey;       // key pressed this frame, SDLK_UNKNOWN none
    SDL_Keymod        _capMods;      // modifiers held with it

    // Open-dropdown state. An expanded option list is modal: it is drawn on top
    // of the page (never inline, so it neither resizes its row nor inflates the
    // page scroll range) and it owns the pointer and the wheel until it closes.
    int               _ddModal;      // entry index open at frame start, -1 none
    int               _ddOpen;       // entry index that drew a header this frame
    bool              _ddJustOpened; // opened by *this* frame's click
    float             _ddX, _ddY, _ddW, _ddH; // that header's box, window coords

    // Glyph rasterizer. Owned by the window because its atlas textures belong
    // to Renderer; dg_presdl_init installs a system sans, and every metric in
    // this header is measured against it.
    dg_presdl_font    Font;

    // Optional row above the tab bar: one entry of Entries (a mode dropdown, say), drawn across the window; the
    // tabs and pages move down by its height. NULL = none, the layout as before.
    dg_presdl_entry*  TopBar;
} dg_presdl_preconfig_window;

// Height of the TopBar row (0 without one); the tab bar starts below it.
static inline float dg_presdl_top_h(const dg_presdl_preconfig_window* win) {
    return win && win->TopBar ? (float)DG_PRESDL_ENTRY_H + 8.0f : 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Glyph rasterizer
// ═══════════════════════════════════════════════════════════════════════════

// The font every impl_dg_text_()/measurement call uses next. It is file scope for
// the same reason the pointer and monitor-drag state are: a measurement helper
// takes a string and a scale, not a window, and exactly one window or surface
// is being drawn at any instant. dg_presdl_frame() and the surface begin/present
// pair keep it pointing at whoever owns the current renderer.
static dg_presdl_font* impl_dg_font_ = nullptr;

static inline void impl_dg_font_bind_(dg_presdl_font* f) { impl_dg_font_ = f; }

static inline dg_presdl_font* impl_dg_font_current_(void) {
    return (impl_dg_font_ && impl_dg_font_->Ready) ? impl_dg_font_ : nullptr;
}

// Open-addressed probe. SlotCount is a power of two and the table is kept below
// 75% full, so an empty slot is always reachable.
static inline dg_presdl_glyph* impl_dg_font_slot_(dg_presdl_font* f, uint32_t cp) {
    uint32_t mask = f->SlotCount - 1;
    uint32_t i = (cp * 2654435761u) & mask;
    while (f->Slots[i].Code != 0 && f->Slots[i].Code != cp) i = (i + 1) & mask;
    return &f->Slots[i];
}

static inline bool impl_dg_font_grow_(dg_presdl_font* f) {
    uint32_t n = f->SlotCount ? f->SlotCount * 2 : 256;
    dg_presdl_glyph* fresh = (dg_presdl_glyph*)SDL_calloc(n, sizeof *fresh);
    if (!fresh) return false;
    dg_presdl_glyph* old = f->Slots;
    uint32_t oldN = f->SlotCount;
    f->Slots     = fresh;
    f->SlotCount = n;
    for (uint32_t i = 0; i < oldN; i++)
        if (old[i].Code) *impl_dg_font_slot_(f, old[i].Code) = old[i];
    SDL_free(old);
    return true;
}

// Release everything the handle owns and leave it in the zeroed state a fresh
// window/surface has, so a partially built font (a face with no atlas, an atlas
// with no cache) tears down exactly like a complete one and a second call is a
// no-op. Must run before the renderer dies: the sheets belong to it.
static inline void impl_dg_font_free_(dg_presdl_font* f) {
    if (!f) return;
    for (int i = 0; i < f->AtlasCount; i++)
        if (f->Atlases[i]) SDL_DestroyTexture(f->Atlases[i]);
    SDL_free(f->Atlases);
    SDL_free(f->Slots);
#ifndef DG_PRESDL_NO_FREETYPE
    if (f->Face) FT_Done_Face((FT_Face)f->Face);
    if (f->Lib)  FT_Done_FreeType((FT_Library)f->Lib);
#endif
    if (impl_dg_font_ == f) impl_dg_font_ = nullptr;   // never leave a dangling current font
    *f = (dg_presdl_font){ 0 };
}

static inline bool impl_dg_font_new_atlas_(dg_presdl_font* f) {
    SDL_Texture* t = SDL_CreateTexture(f->Renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC,
                                       DG_PRESDL_ATLAS_PX, DG_PRESDL_ATLAS_PX);
    if (!t) return false;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    // A new texture's contents are undefined and the 1px gutters between glyphs
    // are sampled whenever text is drawn at scale > 1, so start it transparent.
    void* zero = SDL_calloc((size_t)DG_PRESDL_ATLAS_PX * DG_PRESDL_ATLAS_PX, 4);
    if (!zero) { SDL_DestroyTexture(t); return false; }
    bool cleared = SDL_UpdateTexture(t, nullptr, zero, DG_PRESDL_ATLAS_PX * 4);
    SDL_free(zero);                      // scratch is done with either way
    if (!cleared) { SDL_DestroyTexture(t); return false; }

    SDL_Texture** grown = (SDL_Texture**)SDL_realloc(
        f->Atlases, (size_t)(f->AtlasCount + 1) * sizeof *grown);
    if (!grown) { SDL_DestroyTexture(t); return false; }
    f->Atlases = grown;
    f->Atlases[f->AtlasCount++] = t;
    f->PenX = 0; f->PenY = 0; f->RowH = 0;
    return true;
}

// Fetch a glyph, rasterizing and packing it on first use. Never returns a
// pointer that outlives the next grow, so callers must use it immediately.
static inline dg_presdl_glyph* impl_dg_font_glyph_(dg_presdl_font* f, uint32_t cp) {
#ifdef DG_PRESDL_NO_FREETYPE
    (void)f; (void)cp;
    return nullptr;
#else
    if (!f || !f->Slots || !f->Face || !f->Renderer) return nullptr;
    dg_presdl_glyph* g = impl_dg_font_slot_(f, cp);
    if (g->Code == cp) return g;

    if ((f->SlotUsed + 1) * 4 >= f->SlotCount * 3) {
        if (!impl_dg_font_grow_(f)) return nullptr;
        g = impl_dg_font_slot_(f, cp);
    }

    // Claim the slot before anything can fail: a blank entry is a valid cached
    // answer, and it is what stops a broken glyph being retried every frame.
    *g = (dg_presdl_glyph){ .Code = cp, .Atlas = -1 };
    f->SlotUsed++;

    FT_Face face = (FT_Face)f->Face;
    if (FT_Load_Glyph(face, FT_Get_Char_Index(face, cp), FT_LOAD_DEFAULT) != 0)
        return g;
    if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP &&
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        return g;

    g->Adv = (float)face->glyph->advance.x / 64.0f;
    g->Bx  = face->glyph->bitmap_left;
    g->By  = face->glyph->bitmap_top;

    FT_Bitmap* bm = &face->glyph->bitmap;
    int bw = (int)bm->width, bh = (int)bm->rows;
    if (bw <= 0 || bh <= 0) return g;                       // space and friends
    if (bw > DG_PRESDL_ATLAS_PX || bh > DG_PRESDL_ATLAS_PX) return g;
    if (bm->pixel_mode != FT_PIXEL_MODE_GRAY &&
            bm->pixel_mode != FT_PIXEL_MODE_MONO) return g; // colour strike, skip

    // Shelf packing: fill the current row, drop to a new one when it is full,
    // start a new sheet when the current one is.
    if (f->AtlasCount == 0 && !impl_dg_font_new_atlas_(f)) return g;
    if (f->PenX + bw + 1 > DG_PRESDL_ATLAS_PX) {
        f->PenX  = 0;
        f->PenY += f->RowH + 1;
        f->RowH  = 0;
    }
    if (f->PenY + bh + 1 > DG_PRESDL_ATLAS_PX && !impl_dg_font_new_atlas_(f)) return g;

    // White with the coverage in alpha, so SDL_SetTextureColorMod tints the
    // glyph and the existing SDL_Color argument keeps working unchanged.
    Uint32* px = (Uint32*)SDL_malloc((size_t)bw * (size_t)bh * sizeof *px);
    if (!px) return g;
    for (int row = 0; row < bh; row++) {
        const unsigned char* srcRow = bm->buffer + row * bm->pitch;
        Uint32* dstRow = px + (size_t)row * (size_t)bw;
        if (bm->pixel_mode == FT_PIXEL_MODE_MONO) {
            for (int col = 0; col < bw; col++)
                dstRow[col] = ((srcRow[col >> 3] >> (7 - (col & 7))) & 1u)
                            ? 0xFFFFFFFFu : 0x00FFFFFFu;
        } else {
            for (int col = 0; col < bw; col++)
                dstRow[col] = ((Uint32)srcRow[col] << 24) | 0x00FFFFFFu;
        }
    }
    SDL_Rect dst = { f->PenX, f->PenY, bw, bh };
    bool ok = SDL_UpdateTexture(f->Atlases[f->AtlasCount - 1], &dst,
                                px, bw * (int)sizeof *px);
    SDL_free(px);
    if (!ok) return g;

    g->Atlas = f->AtlasCount - 1;
    g->X = f->PenX; g->Y = f->PenY; g->W = bw; g->H = bh;
    f->PenX += bw + 1;
    if (bh > f->RowH) f->RowH = bh;
    return g;
#endif
}

// Build a font into a scratch handle and only swap it in once it is complete:
// a failed load must leave the caller's current font — face, atlas and all —
// exactly as it was.
static inline bool impl_dg_font_load_(dg_presdl_font* f, SDL_Renderer* r,
                                 const char* path, int pixelSize) {
#ifdef DG_PRESDL_NO_FREETYPE
    (void)f; (void)r; (void)path; (void)pixelSize;
    return false;
#else
    if (!f || !r || !path || !path[0]) return false;
    if (pixelSize < 6)   pixelSize = 6;
    if (pixelSize > 256) pixelSize = 256;

    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) != 0) return false;
    FT_Face face = nullptr;
    if (FT_New_Face(lib, path, 0, &face) != 0) {
        FT_Done_FreeType(lib);
        return false;
    }
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixelSize) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return false;
    }

    dg_presdl_font n = { 0 };
    n.Lib       = lib;
    n.Face      = face;
    n.Renderer  = r;
    n.PixelSize = pixelSize;
    n.Ascent    =  (float)face->size->metrics.ascender  / 64.0f;
    n.Descent   = -(float)face->size->metrics.descender / 64.0f;
    // A bitmap-only strike can report nothing useful; a zero line height would
    // collapse every vertical metric derived from it.
    if (!(n.Ascent  > 0.0f)) n.Ascent  = (float)pixelSize;
    if (!(n.Descent > 0.0f)) n.Descent = 0.0f;
    SDL_strlcpy(n.Path, path, sizeof n.Path);
    if (!impl_dg_font_grow_(&n)) {
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return false;
    }
    n.Ready = true;

    // Warm printable ASCII up front: one upload burst instead of a hitch the
    // first time each character appears on screen.
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) impl_dg_font_glyph_(&n, cp);

    bool wasBound = (impl_dg_font_ == f);
    impl_dg_font_free_(f);          // drops the old sheets, and the binding with them
    *f = n;
    if (wasBound) impl_dg_font_ = f;
    return true;
#endif
}

#ifndef DG_PRESDL_NO_FREETYPE
// Resolve a family name to a font file. The process-wide Fontconfig config is
// initialised on demand and deliberately never torn down: FcFini() would pull
// it out from under any other consumer living in the same process.
static inline bool impl_dg_fc_resolve_(const char* family, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    if (!FcInit()) return false;
    FcPattern* pat = FcNameParse((const FcChar8*)
        ((family && family[0]) ? family : "sans-serif"));
    if (!pat) return false;
    if (!FcConfigSubstitute(nullptr, pat, FcMatchPattern)) {
        FcPatternDestroy(pat);
        return false;
    }
    FcDefaultSubstitute(pat);
    FcResult res = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pat, &res);
    FcPatternDestroy(pat);
    if (!match) return false;
    FcChar8* file = nullptr;
    bool ok = (res == FcResultMatch) &&
              FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file;
    if (ok) SDL_strlcpy(out, (const char*)file, outSize);
    FcPatternDestroy(match);
    return ok;
}

static inline int impl_dg_str_cmp_(const void* a, const void* b) {
    return SDL_strcmp(*(const char* const*)a, *(const char* const*)b);
}
#endif

// ─── Public font selection ─────────────────────────────────────────────────

// Load `path` at `pixel_size` (<= 0 -> the default) into the window's atlas.
// The previous font survives a failure untouched.
static inline bool dg_presdl_font_set_file(dg_presdl_preconfig_window* win,
                                           const char* path, int pixel_size) {
    if (!win || !win->Renderer) return false;
    if (pixel_size <= 0) pixel_size = DG_PRESDL_FONT_PX;
    if (!impl_dg_font_load_(&win->Font, win->Renderer, path, pixel_size)) return false;
    win->Font.Family[0] = '\0';   // chosen by path: there is no family to report
    impl_dg_font_bind_(&win->Font);
    return true;
}

// Resolve `family` through Fontconfig and load it. A NULL or empty family means
// "the system default sans". Returns false and keeps the previous font when the
// name resolves to nothing or the file will not load.
static inline bool dg_presdl_font_set(dg_presdl_preconfig_window* win,
                                      const char* family, int pixel_size) {
#ifdef DG_PRESDL_NO_FREETYPE
    (void)win; (void)family; (void)pixel_size;
    return false;
#else
    if (!win || !win->Renderer) return false;
    char path[512];
    if (!impl_dg_fc_resolve_(family, path, sizeof path)) return false;
    if (pixel_size <= 0) pixel_size = DG_PRESDL_FONT_PX;
    if (!impl_dg_font_load_(&win->Font, win->Renderer, path, pixel_size)) return false;
    SDL_strlcpy(win->Font.Family, (family && family[0]) ? family : "sans-serif",
                sizeof win->Font.Family);
    impl_dg_font_bind_(&win->Font);
    return true;
#endif
}

// Every installed family, sorted and de-duplicated, ready to feed a dropdown.
// Returns the count and stores a caller-owned array in *out_families; free it
// with dg_presdl_font_list_free().
static inline int dg_presdl_font_list(char*** out_families) {
    if (!out_families) return 0;
    *out_families = nullptr;
#ifdef DG_PRESDL_NO_FREETYPE
    return 0;
#else
    if (!FcInit()) return 0;
    FcPattern*   pat = FcPatternCreate();
    FcObjectSet* set = FcObjectSetBuild(FC_FAMILY, (char*)nullptr);
    if (!pat || !set) {
        if (pat) FcPatternDestroy(pat);
        if (set) FcObjectSetDestroy(set);
        return 0;
    }
    FcFontSet* fonts = FcFontList(nullptr, pat, set);
    FcPatternDestroy(pat);
    FcObjectSetDestroy(set);
    if (!fonts) return 0;
    if (fonts->nfont <= 0) { FcFontSetDestroy(fonts); return 0; }

    char** names = (char**)SDL_calloc((size_t)fonts->nfont, sizeof *names);
    if (!names) { FcFontSetDestroy(fonts); return 0; }
    int n = 0;
    for (int i = 0; i < fonts->nfont; i++) {
        FcChar8* fam = nullptr;
        if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &fam) != FcResultMatch)
            continue;
        if (!fam) continue;
        char* dup = SDL_strdup((const char*)fam);
        if (dup) names[n++] = dup;
    }
    FcFontSetDestroy(fonts);

    SDL_qsort(names, (size_t)n, sizeof *names, impl_dg_str_cmp_);
    // Fontconfig lists one pattern per file, so a family with four weights
    // appears four times; collapse the runs the sort just made adjacent.
    int unique = 0;
    for (int i = 0; i < n; i++) {
        if (unique > 0 && SDL_strcmp(names[unique - 1], names[i]) == 0) {
            SDL_free(names[i]);
            continue;
        }
        names[unique++] = names[i];
    }
    if (unique == 0) { SDL_free(names); return 0; }
    *out_families = names;
    return unique;
#endif
}

static inline void dg_presdl_font_list_free(char** families, int n) {
    if (!families) return;
    for (int i = 0; i < n; i++) SDL_free(families[i]);
    SDL_free(families);
}

// ─── Text metrics ──────────────────────────────────────────────────────────
//
// Everything that used to assume an 8px cell asks these instead. With no font
// loaded they return exactly the debug font's metrics, so a Fontconfig-less
// system lays out identically to how this header always did.

// Line height: what one row of text occupies, top of the cell to the bottom.
static inline float dg_presdl_text_height(int scale) {
    if (scale < 1) scale = 1;
    const dg_presdl_font* f = impl_dg_font_current_();
    if (!f) return (float)(DG_PRESDL_DEBUG_CELL * scale);
    return (f->Ascent + f->Descent) * (float)scale;
}

static inline float impl_dg_text_width_n_(const char* utf8, size_t bytes, int scale) {
    if (!utf8 || bytes == 0) return 0.0f;
    if (scale < 1) scale = 1;
    dg_presdl_font* f = impl_dg_font_current_();
    if (!f)
        return (float)(impl_dg_utf8_count_(utf8, bytes) *
                       (size_t)(DG_PRESDL_DEBUG_CELL * scale));
    float total = 0.0f;
    size_t i = 0;
    while (i < bytes) {
        const dg_presdl_glyph* g = impl_dg_font_glyph_(f, impl_dg_utf8_decode_(utf8, bytes, &i));
        if (g) total += g->Adv;
    }
    return total * (float)scale;
}

// Rendered width of a UTF-8 string, in the same pixels impl_dg_text_() will use.
static inline float dg_presdl_text_width(const char* utf8, int scale) {
    return utf8 ? impl_dg_text_width_n_(utf8, strlen(utf8), scale) : 0.0f;
}

static inline float impl_dg_glyph_adv_(dg_presdl_font* f, uint32_t cp, int scale) {
    if (!f) return (float)(DG_PRESDL_DEBUG_CELL * scale);
    const dg_presdl_glyph* g = impl_dg_font_glyph_(f, cp);
    return g ? g->Adv * (float)scale : 0.0f;
}

// Byte length of the longest prefix of s[0..bytes) that fits in maxW px.
// Counting characters stopped meaning anything the moment glyphs became
// proportional, so every truncation in this header measures instead.
static inline size_t impl_dg_text_fit_head_(const char* s, size_t bytes,
                                       float maxW, int scale) {
    if (!s || bytes == 0 || !(maxW > 0.0f)) return 0;
    if (scale < 1) scale = 1;
    dg_presdl_font* f = impl_dg_font_current_();
    float used = 0.0f;
    size_t i = 0, fit = 0;
    while (i < bytes) {
        size_t next = i;
        float adv = impl_dg_glyph_adv_(f, impl_dg_utf8_decode_(s, bytes, &next), scale);
        if (used + adv > maxW) break;
        used += adv;
        i = next;
        fit = next;
    }
    return fit;
}

// Byte offset of the longest suffix of s[0..bytes) that fits in maxW px.
static inline size_t impl_dg_text_fit_tail_(const char* s, size_t bytes,
                                       float maxW, int scale) {
    if (!s || bytes == 0) return bytes;
    if (!(maxW > 0.0f)) return bytes;
    if (scale < 1) scale = 1;
    dg_presdl_font* f = impl_dg_font_current_();
    float used = 0.0f;
    size_t i = bytes, start = bytes;
    while (i > 0) {
        size_t p = impl_dg_utf8_prev_(s, i);
        size_t q = p;
        float adv = impl_dg_glyph_adv_(f, impl_dg_utf8_decode_(s, bytes, &q), scale);
        if (used + adv > maxW) break;
        used += adv;
        i = p;
        start = p;
    }
    return start;
}

// Copy the longest head of src that fits both maxW px and cap bytes into dst,
// never splitting a code point.
static inline void impl_dg_text_elide_(char* dst, size_t cap, const char* src,
                                  float maxW, int scale) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;
    size_t fit = impl_dg_text_fit_head_(src, strlen(src), maxW, scale);
    if (fit > cap - 1) {
        fit = cap - 1;
        while (fit > 0 && impl_dg_utf8_cont_((unsigned char)src[fit])) fit--;
    }
    memcpy(dst, src, fit);
    dst[fit] = '\0';
}

// Draw one line from the atlas. y is the top of the cell, matching what
// SDL_RenderDebugText does, so no call site had to move.
static inline void impl_dg_font_draw_(dg_presdl_font* f, SDL_Renderer* r,
                                 float x, float y, const char* text,
                                 SDL_Color c, float scale) {
    size_t bytes = strlen(text);
    float pen  = x;
    float base = y + f->Ascent * scale;
    size_t i = 0;
    while (i < bytes) {
        const dg_presdl_glyph* g = impl_dg_font_glyph_(f, impl_dg_utf8_decode_(text, bytes, &i));
        if (!g) continue;
        if (g->Atlas >= 0 && g->Atlas < f->AtlasCount && g->W > 0) {
            SDL_Texture* t = f->Atlases[g->Atlas];
            SDL_SetTextureColorMod(t, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(t, c.a);
            SDL_FRect src = { (float)g->X, (float)g->Y, (float)g->W, (float)g->H };
            SDL_FRect dst = { pen + (float)g->Bx * scale,
                              base - (float)g->By * scale,
                              (float)g->W * scale, (float)g->H * scale };
            SDL_RenderTexture(r, t, &src, &dst);
        }
        pen += g->Adv * scale;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal draw helpers
// ═══════════════════════════════════════════════════════════════════════════

static inline void impl_dg_set_col_(SDL_Renderer* r, SDL_Color c) {
    // A NULL renderer reached every primitive below (e.g. drawing after a
    // failed init, or into a destroyed surface) and was passed straight to SDL.
    if (!r) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static inline void impl_dg_fill_rect_(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c) {
    if (!r) return;
    impl_dg_set_col_(r, c);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static inline void impl_dg_draw_rect_(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c) {
    if (!r) return;
    impl_dg_set_col_(r, c);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderRect(r, &rect);
}

static inline void impl_dg_text_(SDL_Renderer* r, float x, float y, const char* text, SDL_Color c, int scale) {
    // A caller-owned Label/Option/Name is allowed to be NULL; SDL_RenderDebugText
    // walks the string unconditionally, so an unset label crashed the frame.
    // scale <= 0 divided by zero (and asked SDL for a degenerate render scale).
    if (!r || !text) return;
    if (scale < 1) scale = 1;
    // The atlas sheets belong to the renderer that built them, so a font bound
    // for another window must not be drawn with this one — that falls back too.
    dg_presdl_font* f = impl_dg_font_current_();
    if (f && f->Renderer == r) {
        impl_dg_font_draw_(f, r, x, y, text, c, (float)scale);
        return;
    }
    impl_dg_set_col_(r, c);
    SDL_SetRenderScale(r, (float)scale, (float)scale);
    SDL_RenderDebugText(r, x / (float)scale, y / (float)scale, text);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

static inline bool impl_dg_mouse_in_(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

// Transient drag state for the monitor-arrangement canvas. It lives at file
// scope for the same reason the pointer state does: the complement struct is
// caller-owned data (a monitor list), not a place to stash interaction state.
// Only one drag can be in flight at a time and `activeEntry` says which widget
// owns it, so a single slot is enough.
static int   impl_dg_mon_drag_ = -1;        // monitor index being dragged, -1 = none
static float impl_dg_mon_grab_x_ = 0.0f;    // pointer-to-monitor-origin offset, output px
static float impl_dg_mon_grab_y_ = 0.0f;

// ═══════════════════════════════════════════════════════════════════════════
// Entry handlers — one draw function per type
// ═══════════════════════════════════════════════════════════════════════════

// Returns the height consumed
static inline float dg_presdl_draw_checkbox(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked)
{
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colOn     = (SDL_Color)DG_PRESDL_COL_CHECK_ON;
    SDL_Color colPanel  = (SDL_Color)DG_PRESDL_COL_PANEL;

    float boxSz = 20.0f;
    float boxX  = x + w - boxSz - DG_PRESDL_ENTRY_PAD;
    float boxY  = y + (DG_PRESDL_ENTRY_H - boxSz) * 0.5f;

    bool hov = impl_dg_mouse_in_(mx, my, x, y, w, DG_PRESDL_ENTRY_H);
    impl_dg_fill_rect_(r, x, y, w, DG_PRESDL_ENTRY_H,
        hov ? (SDL_Color)DG_PRESDL_COL_HOVER : colPanel);

    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 10, e->Label, colText, 1);

    bool val = e->Complement.Checkbox.Value ? *e->Complement.Checkbox.Value : false;
    impl_dg_fill_rect_(r, boxX, boxY, boxSz, boxSz, val ? colOn : colPanel);
    impl_dg_draw_rect_(r, boxX, boxY, boxSz, boxSz, colBorder);
    if (val) {
        // Centred by measurement: the tick is no longer a fixed 8x8 cell.
        float tw = dg_presdl_text_width("X", 1), th = dg_presdl_text_height(1);
        impl_dg_text_(r, boxX + (boxSz - tw) * 0.5f, boxY + (boxSz - th) * 0.5f,
                 "X", (SDL_Color){ 0,0,0,255 }, 1);
    }

    if (clicked && hov && e->Complement.Checkbox.Value)
        *e->Complement.Checkbox.Value = !(*e->Complement.Checkbox.Value);

    return DG_PRESDL_ENTRY_H;
}

static inline float dg_presdl_draw_button(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked,
    int* activeEntry, dg_presdl_preconfig_window* win)
{
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;
    SDL_Color colHov    = (SDL_Color)DG_PRESDL_COL_ACCENT_HOV;

    bool hov = impl_dg_mouse_in_(mx, my, x, y, w, DG_PRESDL_ENTRY_H);
    impl_dg_fill_rect_(r, x + DG_PRESDL_ENTRY_PAD, y + 4,
                  w - DG_PRESDL_ENTRY_PAD * 2, DG_PRESDL_ENTRY_H - 8,
                  hov ? colHov : colAccent);

    const char* lbl = e->Complement.Button.Label
                    ? e->Complement.Button.Label : e->Label;
    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD * 2, y + 10, lbl, (SDL_Color)DG_PRESDL_COL_ON_ACCENT, 1);

    if (clicked && hov && e->Complement.Button.OnClick) {
        e->Complement.Button.OnClick(e->Complement.Button.UserData);

        // ← Reset all interaction state after any button press
        if (activeEntry) *activeEntry = -1;
        // win/Entries are caller-owned: a window with no entry array (or a
        // stale non-zero EntryCount) walked a NULL pointer here.
        impl_dg_mon_drag_ = -1;
        for (uint64_t i = 0; win && win->Entries && i < win->EntryCount; i++) {
            if (win->Entries[i].Type == DG_PRESDL_DROPDOWN)
                win->Entries[i].Complement.Dropdown.Open = false;
            if (win->Entries[i].Type == DG_PRESDL_SLIDER_32)
                win->Entries[i].Complement.Slider32.Dragging = false;
        }
    }

    return DG_PRESDL_ENTRY_H;
}

// Draws the collapsed row only. The expanded option list is NOT drawn here: it
// is recorded and painted by the overlay pass after the whole page, because
// (a) drawing it inline let later entries paint over it, (b) its height was
// added to the row, which moved every following widget while it was open and
// grew the page's scroll range by the entire option list, and (c) the list was
// hit-tested with the very click that opened it.
static inline float dg_presdl_draw_dropdown(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked,
    int entryIndex, dg_presdl_preconfig_window* win)
{
    SDL_Color colPanel  = (SDL_Color)DG_PRESDL_COL_PANEL;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;

    dg_presdl_complement_dropdown* dd = &e->Complement.Dropdown;
    float dropW = w * 0.55f;
    float dropX = x + w - dropW - DG_PRESDL_ENTRY_PAD;
    float dropY = y + 6;
    float dropH = DG_PRESDL_ENTRY_H - 12;

    bool hov = impl_dg_mouse_in_(mx, my, dropX, dropY, dropW, dropH);
    impl_dg_fill_rect_(r, x, y, w, DG_PRESDL_ENTRY_H, colPanel);
    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 10, e->Label, colText, 1);

    impl_dg_fill_rect_(r, dropX, dropY, dropW, dropH,
        (hov || dd->Open) ? (SDL_Color)DG_PRESDL_COL_DROPDOWN_HOV : (SDL_Color)DG_PRESDL_COL_DROPDOWN);
    impl_dg_draw_rect_(r, dropX, dropY, dropW, dropH, colBorder);
    // Caret, so a collapsed dropdown reads as one.
    impl_dg_fill_rect_(r, dropX + dropW - 14, dropY + dropH * 0.5f - 1, 8, 2, colAccent);

    // A stale Selected left over from a longer Options array indexed past the
    // end of the list; clamp it back in range before anything reads it.
    if (dd->Selected && dd->OptionCount > 0 && *dd->Selected >= dd->OptionCount)
        *dd->Selected = dd->OptionCount - 1;

    if (dd->Options && dd->Selected && *dd->Selected < dd->OptionCount) {
        // Elided to the box: a proportional font makes a long family name run
        // straight through the caret and out of the row.
        char clip[128];
        impl_dg_text_elide_(clip, sizeof clip, dd->Options[*dd->Selected], dropW - 22.0f, 1);
        impl_dg_text_(r, dropX + 6, dropY + (dropH - dg_presdl_text_height(1)) * 0.5f,
                 clip, colText, 1);
    }

    if (clicked && hov) {
        if (dd->Open) {
            dd->Open = false;                      // click the header to collapse
        } else if (dd->Options && dd->OptionCount > 0) {
            // Opening an empty list (OptionCount == 0, or Options never set)
            // left the dropdown stuck open with nothing to pick and no visible
            // way out, so only a populated list opens at all.
            for (uint64_t i = 0; win->Entries && i < win->EntryCount; i++) {
                if (win->Entries[i].Type == DG_PRESDL_DROPDOWN)
                    win->Entries[i].Complement.Dropdown.Open = false;
            }
            dd->Open   = true;
            dd->Scroll = 0.0f;
            // The list appears *this* frame while the same release is still
            // live: without this the overlay treated it as a pick and
            // committed whatever option landed under the cursor.
            win->_ddJustOpened = true;
        }
    }

    if (dd->Open) {
        win->_ddOpen = entryIndex;
        win->_ddX = dropX; win->_ddY = dropY;
        win->_ddW = dropW; win->_ddH = dropH;
    }

    return DG_PRESDL_ENTRY_H;
}

// Overlay pass: draw and hit-test the option list of the open dropdown, on top
// of everything else and outside the page clip. Called once per frame, after
// the page. Long lists (fc-list can hand us hundreds of font families) are
// windowed to what fits between the tab bar and the bottom edge and scrolled
// with the wheel; the list flips above the header when there is more room
// there. It never overlaps its own header, which is what made a click land on
// an option the instant the list opened.
static inline void dg_presdl_draw_dropdown_overlay(
    dg_presdl_preconfig_window* win,
    float mx, float my, bool clicked, float scrollDelta)
{
    if (!win || !win->Renderer || win->_ddOpen < 0) return;
    if (!win->Entries || (uint64_t)win->_ddOpen >= win->EntryCount) return;

    dg_presdl_entry* e = &win->Entries[win->_ddOpen];
    if (e->Type != DG_PRESDL_DROPDOWN) return;
    dg_presdl_complement_dropdown* dd = &e->Complement.Dropdown;
    if (!dd->Open) return;
    if (!dd->Options || dd->OptionCount == 0) { dd->Open = false; return; }

    SDL_Renderer* r = win->Renderer;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;

    float rowH = win->_ddH > 12.0f ? win->_ddH : 12.0f;
    // The list stays off the tab bar, except the top bar's own, which opens down over it.
    bool fromTop = win->TopBar && win->TopBar == e;
    float top  = fromTop ? 0.0f : dg_presdl_top_h(win) + (float)DG_PRESDL_TAB_H;
    float bottom;
    int outW = 0, outH = 0;
    if (SDL_GetCurrentRenderOutputSize(r, &outW, &outH) && outH > 0)
        bottom = (float)outH;
    else
        bottom = win->_ddY + win->_ddH + rowH;

    // The row can be scrolled out of the page while the list is open; with no
    // anchor left on screen the list would float over unrelated widgets.
    if (win->_ddY + win->_ddH <= top || win->_ddY >= bottom) { dd->Open = false; return; }

    float below = bottom - (win->_ddY + win->_ddH);
    float above = win->_ddY - top;
    bool  down  = below >= above;
    float space = down ? below : above;

    uint32_t fit = (uint32_t)(space / rowH);
    if (fit < 1) fit = 1;
    uint32_t vis = dd->OptionCount < fit ? dd->OptionCount : fit;
    float listH = (float)vis * rowH;
    float listY = down ? win->_ddY + win->_ddH : win->_ddY - listH;
    if (listY < top) listY = top;

    // Wheel/arrow scroll belongs to the list while it is open.
    float maxFirst = (float)(dd->OptionCount - vis);
    dd->Scroll -= scrollDelta;
    if (dd->Scroll < 0.0f)      dd->Scroll = 0.0f;
    if (dd->Scroll > maxFirst)  dd->Scroll = maxFirst;
    uint32_t first = (uint32_t)dd->Scroll;

    float lx = win->_ddX, lw = win->_ddW;
    bool onList = impl_dg_mouse_in_(mx, my, lx, listY, lw, listH);
    bool onHead = impl_dg_mouse_in_(mx, my, win->_ddX, win->_ddY, win->_ddW, win->_ddH);
    uint32_t hovRow = dd->OptionCount;
    if (onList) {
        uint32_t row = first + (uint32_t)((my - listY) / rowH);
        if (row < dd->OptionCount) hovRow = row;
    }

    impl_dg_fill_rect_(r, lx, listY, lw, listH, (SDL_Color)DG_PRESDL_COL_FIELD);
    for (uint32_t i = 0; i < vis; i++) {
        uint32_t opt = first + i;
        float  oy  = listY + (float)i * rowH;
        bool   sel = dd->Selected && *dd->Selected == opt;
        if (opt == hovRow)
            impl_dg_fill_rect_(r, lx, oy, lw, rowH, colAccent);
        else if (sel)
            impl_dg_fill_rect_(r, lx, oy, lw, rowH, (SDL_Color)DG_PRESDL_COL_SELECTED);
        char clip[128];
        impl_dg_text_elide_(clip, sizeof clip, dd->Options[opt], lw - 16.0f, 1);
        impl_dg_text_(r, lx + 6, oy + (rowH - dg_presdl_text_height(1)) * 0.5f, clip,
                      opt == hovRow ? (SDL_Color)DG_PRESDL_COL_ON_ACCENT : colText, 1);
    }
    impl_dg_draw_rect_(r, lx, listY, lw, listH, colBorder);

    if (maxFirst > 0.0f) {
        float thumbH = listH * ((float)vis / (float)dd->OptionCount);
        if (thumbH < 10.0f) thumbH = 10.0f;
        float frac = dd->Scroll / maxFirst;
        impl_dg_fill_rect_(r, lx + lw - 4, listY, 4, listH, (SDL_Color)DG_PRESDL_COL_SCROLL_TRACK);
        impl_dg_fill_rect_(r, lx + lw - 4, listY + frac * (listH - thumbH), 4, thumbH, colAccent);
    }

    if (clicked && !win->_ddJustOpened) {
        if (hovRow < dd->OptionCount) {
            if (dd->Selected) *dd->Selected = hovRow;
            dd->Open = false;
        } else if (!onHead) {
            dd->Open = false;   // click anywhere else dismisses, changing nothing
        }
    }
}

static inline float dg_presdl_draw_slider_32(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked, bool held, int entryIndex, int* activeEntry)
{
    (void)clicked;   // kept for the shared entry-draw signature
    SDL_Color colPanel  = (SDL_Color)DG_PRESDL_COL_PANEL;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;

    dg_presdl_complement_slider_32* sl = &e->Complement.Slider32;
    float trackW = w * 0.5f;
    float trackX = x + w - trackW - DG_PRESDL_ENTRY_PAD;
    float trackH = (float)DG_PRESDL_SLIDER_TRACK_H;
    float trackY = y + (DG_PRESDL_ENTRY_H - trackH) * 0.5f;

    impl_dg_fill_rect_(r, x, y, w, DG_PRESDL_ENTRY_H, colPanel);
    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 10, e->Label, colText, 1);

    // Track
    impl_dg_fill_rect_(r, trackX, trackY, trackW, trackH, (SDL_Color){ 40, 40, 55, 255 });
    impl_dg_draw_rect_(r, trackX, trackY, trackW, trackH, colBorder);

    // Min/Max/Step/Value are caller-owned and were used unchecked: an empty or
    // inverted range divided by zero, a NaN value poisoned every derived
    // coordinate, and a value outside [Min,Max] drew the fill and handle off
    // the end of the track (negative-width rect).
    float range = sl->Max - sl->Min;
    if (sl->Value) {
        if (SDL_isnanf(*sl->Value)) *sl->Value = sl->Min;
        if (range >= 0.0f) {
            if (*sl->Value < sl->Min) *sl->Value = sl->Min;
            if (*sl->Value > sl->Max) *sl->Value = sl->Max;
        }
    }

    // Fill
    float t = (sl->Value && range > 0.0f) ? (*sl->Value - sl->Min) / range : 0.0f;
    impl_dg_fill_rect_(r, trackX, trackY, trackW * t, trackH, colAccent);

    // Handle
    float hx = trackX + trackW * t - 6;
    impl_dg_fill_rect_(r, hx, trackY - 4, 12, trackH + 8, colAccent);

    // Drag
    bool hov = impl_dg_mouse_in_(mx, my, trackX, trackY - 6, trackW, trackH + 12);

    // Grab on press (held), not release: the click-release model fires `clicked`
    // only when the button comes up, which is too late to start a drag.
    if (held && hov && activeEntry && *activeEntry == -1)
        *activeEntry = entryIndex;

    // Only drag if WE are active
    if (held && activeEntry && *activeEntry == entryIndex && sl->Value &&
            trackW > 0.0f && range > 0.0f) {
        float newT = (mx - trackX) / trackW;   // trackW == 0 in a squeezed row -> inf/NaN
        if (newT < 0.0f) newT = 0.0f;
        if (newT > 1.0f) newT = 1.0f;
        float newVal = sl->Min + newT * range;
        if (sl->Step > 0.0f) {
            // The old (int) cast truncated toward zero — negative values snapped
            // the wrong way — and overflowed for a large value/Step ratio.
            newVal = SDL_floorf(newVal / sl->Step + 0.5f) * sl->Step;
            if (newVal < sl->Min) newVal = sl->Min;  // quantising can overshoot an end
            if (newVal > sl->Max) newVal = sl->Max;
        }
        *sl->Value = newVal;
    }

    // Release
    if (!held && activeEntry && *activeEntry == entryIndex)
        *activeEntry = -1;

    return DG_PRESDL_ENTRY_H;
}

// Body text: Label word-wrapped to the entry's width by the measured font (as a laid-out info window does it),
// re-flowing with the window; '\n' starts a new paragraph, a word wider than a line is put on its own line. Lines
// longer than 511 bytes are cut. Returns the height drawn.
static inline float dg_presdl_draw_paragraph(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w)
{
    SDL_Color col = (SDL_Color)DG_PRESDL_COL_TEXT;
    float lineH = dg_presdl_text_height(1) + 6.0f;
    float maxW = w - 2.0f * DG_PRESDL_ENTRY_PAD;
    float cy = y + 6.0f;
    const char* p = e->Label ? e->Label : "";
    while (*p) {
        char line[512] = "";
        size_t len = 0;
        while (*p && *p != '\n') {
            size_t word = 0;
            while (p[word] && p[word] != ' ' && p[word] != '\n') word++;
            char trial[512];
            int t = SDL_snprintf(trial, sizeof(trial), "%s%s%.*s", line, len ? " " : "", (int)word, p);
            if (len && (t >= (int)sizeof(trial) || dg_presdl_text_width(trial, 1) > maxW))
                break;                      // the word starts the next line
            SDL_strlcpy(line, trial, sizeof(line));
            len = SDL_strlen(line);
            p += word;
            while (*p == ' ') p++;
        }
        impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, cy, line, col, 1);
        cy += lineH;
        if (*p == '\n') p++;
    }
    return cy - y + 4.0f;
}

static inline float dg_presdl_draw_image(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w)
{
    dg_presdl_complement_image* img = &e->Complement.Image;
    if (!img->Texture) return 0;
    float ih = (img->H > 0) ? (float)img->H : 80.0f;
    SDL_FRect dst = { x, y, w, ih };
    SDL_RenderTexture(r, img->Texture, NULL, &dst);
    return ih;
}

// ─── Editable text field ──────────────────────────────────────────────────

// One frame of typing applied to a NUL-terminated caller buffer: `backspaces`
// deletions followed by whatever UTF-8 arrived. Shared by the inline field and
// the modal prompt, which must edit identically — including the two bugs this
// avoids: backspace erasing a single byte out of a multi-byte character, and an
// append that copies a prefix of one when the buffer is nearly full.
static inline void impl_dg_text_edit_(char* buf, size_t cap, int backspaces,
                                 const char* input) {
    if (!buf || cap == 0) return;
    size_t len = strlen(buf);
    for (int b = 0; b < backspaces && len > 0; b++) {
        len = impl_dg_utf8_prev_(buf, len);
        buf[len] = '\0';
    }
    if (input && input[0] != '\0') impl_dg_utf8_append_(buf, len, cap, input);
}

// Frame, contents and caret of a single-line edit box. The text is shown from
// the tail so the caret stays in view, and the tail is chosen by measured width
// — a proportional font makes "boxW / 8 characters" meaningless.
static inline void impl_dg_text_field_(SDL_Renderer* r, float bx, float by,
                                  float bw, float bh, const char* text,
                                  const char* placeholder, bool focused) {
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colDim    = (SDL_Color)DG_PRESDL_COL_TEXT_DIM;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;

    impl_dg_fill_rect_(r, bx, by, bw, bh, (SDL_Color)DG_PRESDL_COL_FIELD);
    impl_dg_draw_rect_(r, bx, by, bw, bh, focused ? colAccent : colBorder);

    float th = dg_presdl_text_height(1);
    float ty = by + (bh - th) * 0.5f;
    float visW = 0.0f;   // width of what was actually drawn — places the caret
    if (text && text[0] != '\0') {
        size_t bytes = strlen(text);
        size_t off   = impl_dg_text_fit_tail_(text, bytes, bw - 12.0f, 1);
        visW = impl_dg_text_width_n_(text + off, bytes - off, 1);
        impl_dg_text_(r, bx + 6, ty, text + off, colText, 1);
    } else if (placeholder) {
        impl_dg_text_(r, bx + 6, ty, placeholder, colDim, 1);
    }
    if (focused)
        impl_dg_fill_rect_(r, bx + 7 + visW, by + 4, 1, bh - 8, colText);
}

static inline float dg_presdl_draw_text(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked,
    int entryIndex, dg_presdl_preconfig_window* win)
{
    // win carries the focus index and this frame's keystrokes; it was
    // dereferenced below without ever being checked.
    if (!win) return DG_PRESDL_ENTRY_H;
    dg_presdl_complement_text* tx = &e->Complement.Text;
    // A caller-owned buffer that is not terminated inside BufSize made every
    // strlen()/render below run off the end; force the terminator once.
    if (tx->Buffer && tx->BufSize > 0) tx->Buffer[tx->BufSize - 1] = '\0';
    SDL_Color colText = (SDL_Color)DG_PRESDL_COL_TEXT;

    float boxW = w * 0.5f;
    float boxX = x + w - boxW - DG_PRESDL_ENTRY_PAD;
    float boxY = y + 4;
    float boxH = DG_PRESDL_ENTRY_H - 8;

    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 10, e->Label, colText, 1);

    bool hov = impl_dg_mouse_in_(mx, my, boxX, boxY, boxW, boxH);
    if (clicked) {
        win->TextFocus = hov ? entryIndex : (win->TextFocus == entryIndex ? -1 : win->TextFocus);
    }
    bool focused = (win->TextFocus == entryIndex);

    // Apply this frame's input while focused.
    if (focused && tx->Buffer && tx->BufSize > 0) {
        impl_dg_text_edit_(tx->Buffer, tx->BufSize, win->_backspaces, win->_textInput);
        if (win->_paste) impl_dg_text_edit_(tx->Buffer, tx->BufSize, 0, win->_paste);
    }

    impl_dg_text_field_(r, boxX, boxY, boxW, boxH, tx->Buffer, tx->Placeholder, focused);
    return DG_PRESDL_ENTRY_H;
}

// ─── Key-capture row ───────────────────────────────────────────────────────

// Modifier keys are never the bound key: they are the modifiers.
static inline bool impl_dg_key_is_mod_(SDL_Keycode k) {
    return k == SDLK_LCTRL  || k == SDLK_RCTRL  ||
           k == SDLK_LSHIFT || k == SDLK_RSHIFT ||
           k == SDLK_LALT   || k == SDLK_RALT   ||
           k == SDLK_LGUI   || k == SDLK_RGUI   ||
           k == SDLK_MODE   || k == SDLK_NUMLOCKCLEAR || k == SDLK_CAPSLOCK;
}

static inline float dg_presdl_draw_keybind(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked,
    int entryIndex, dg_presdl_preconfig_window* win)
{
    if (!win) return DG_PRESDL_ENTRY_H;
    dg_presdl_complement_keybind* kb = &e->Complement.Keybind;
    if (kb->Buffer && kb->BufSize > 0) kb->Buffer[kb->BufSize - 1] = '\0';

    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colDim    = (SDL_Color)DG_PRESDL_COL_TEXT_DIM;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;
    SDL_Color colPanel  = (SDL_Color)DG_PRESDL_COL_PANEL;
    SDL_Color colOn     = (SDL_Color)DG_PRESDL_COL_CHECK_ON;

    float rowH = DG_PRESDL_ENTRY_H;
    float boxY = y + 4;
    float boxH = rowH - 8;

    // Laid out right to left: [Tgl [x]] [D] [ binding ] with the label filling
    // whatever is left, so a long action name never pushes the controls off.
    float rx = x + w - DG_PRESDL_ENTRY_PAD;

    float th = dg_presdl_text_height(1);

    float tglX = 0, tglSz = 18.0f;
    if (kb->Toggle) {
        // "Tgl" was placed three 8px cells wide; it is measured now, or the
        // label overlaps the box it belongs to as soon as the font is real.
        float tglLblW = dg_presdl_text_width("Tgl", 1);
        tglX = rx - tglSz;
        impl_dg_text_(r, tglX - tglLblW - 4.0f, y + (rowH - th) * 0.5f, "Tgl", colDim, 1);
        rx = tglX - tglLblW - 10.0f;
    }

    float defLblW = dg_presdl_text_width("D", 1);
    float defW = defLblW + 14.0f;
    float defX = rx - defW;
    rx = defX - 6;

    float boxX = x + w * 0.40f;
    float boxW = rx - boxX;
    if (boxW < 60.0f) { boxW = 60.0f; boxX = rx - boxW; }

    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 10, e->Label, colText, 1);

    bool hovBox = impl_dg_mouse_in_(mx, my, boxX, boxY, boxW, boxH);
    bool hovDef = impl_dg_mouse_in_(mx, my, defX, boxY, defW, boxH);
    bool hovTgl = kb->Toggle &&
        impl_dg_mouse_in_(mx, my, tglX, y + (rowH - tglSz) * 0.5f, tglSz, tglSz);
    bool capturing = (win->KeyCapture == entryIndex);

    if (clicked) {
        if (hovBox) {
            capturing = true;
            win->KeyCapture = entryIndex;
            win->_capKey = SDLK_UNKNOWN;   // ignore anything typed before arming
            win->TextFocus = -1;           // capture and text editing are exclusive
        } else if (capturing) {
            capturing = false;             // clicking anywhere else cancels
            win->KeyCapture = -1;
        }
        if (hovDef && kb->OnDefault) kb->OnDefault(kb->UserData);
        if (hovTgl && kb->Toggle)    *kb->Toggle = !*kb->Toggle;
    }

    // Consume this frame's captured key.
    if (capturing && win->_capKey != SDLK_UNKNOWN) {
        if (kb->Format && kb->Buffer && kb->BufSize > 0)
            kb->Format(kb->UserData, win->_capKey, win->_capMods,
                       kb->Buffer, kb->BufSize);
        win->KeyCapture = -1;
        win->_capKey = SDLK_UNKNOWN;
        capturing = false;
    }

    impl_dg_fill_rect_(r, boxX, boxY, boxW, boxH, (SDL_Color)DG_PRESDL_COL_FIELD);
    impl_dg_draw_rect_(r, boxX, boxY, boxW, boxH,
                  capturing ? colAccent : (hovBox ? colText : colBorder));
    const char* shown = capturing ? "press a key..."
        : ((kb->Buffer && kb->Buffer[0] != '\0') ? kb->Buffer : "(unbound)");
    char clip[64];
    impl_dg_text_elide_(clip, sizeof clip, shown, boxW - 12.0f, 1);
    impl_dg_text_(r, boxX + 6, boxY + (boxH - th) * 0.5f, clip,
             capturing || (kb->Buffer && kb->Buffer[0] != '\0') ? colText : colDim, 1);

    impl_dg_fill_rect_(r, defX, boxY, defW, boxH, hovDef ? colAccent : colPanel);
    impl_dg_draw_rect_(r, defX, boxY, defW, boxH, colBorder);
    impl_dg_text_(r, defX + (defW - defLblW) * 0.5f, boxY + (boxH - th) * 0.5f,
             "D", colText, 1);

    if (kb->Toggle) {
        float ty = y + (rowH - tglSz) * 0.5f;
        impl_dg_fill_rect_(r, tglX, ty, tglSz, tglSz, *kb->Toggle ? colOn : colPanel);
        impl_dg_draw_rect_(r, tglX, ty, tglSz, tglSz, hovTgl ? colText : colBorder);
        if (*kb->Toggle) {
            float xw = dg_presdl_text_width("X", 1);
            impl_dg_text_(r, tglX + (tglSz - xw) * 0.5f, ty + (tglSz - th) * 0.5f,
                     "X", (SDL_Color){ 0,0,0,255 }, 1);
        }
    }

    return rowH;
}

// ─── Monitor arrangement canvas ────────────────────────────────────────────

#define DG_PRESDL_MON_SNAP  40   // edge-snap threshold, output px

// Layout coordinates are kept inside a sane range: converting an out-of-range
// float to int is undefined behaviour, and X+W must not wrap while snapping.
static inline int impl_dg_mon_coord_(float v) {
    if (!(v > -1.0e6f)) return -1000000;   // written NaN-safe on purpose
    if (v >  1.0e6f)    return  1000000;
    return (int)SDL_lroundf(v);
}

static inline bool impl_dg_mon_overlap_(const dg_presdl_monitor* m, int px, int py, int pw, int ph) {
    return px < m->X + m->W && px + pw > m->X &&
           py < m->Y + m->H && py + ph > m->Y;
}

// Snap the just-dropped monitor to its neighbours: try every combination of the
// four horizontal and four vertical edge alignments (butt right/left, align
// left/right — and the vertical equivalents) and keep the cheapest move within
// the threshold that leaves no rect overlapping. Rejecting overlaps is what
// stops "align both left edges" from stacking two monitors on top of each other.
static inline void impl_dg_mon_snap_(dg_presdl_complement_monitors* mo, uint32_t idx) {
    dg_presdl_monitor* d = &mo->Monitors[idx];
    if (d->W <= 0 || d->H <= 0) return;

    int bestX = d->X, bestY = d->Y, bestCost = -1;

    for (uint32_t n = 0; n < mo->Count; n++) {
        if (n == idx) continue;
        const dg_presdl_monitor* o = &mo->Monitors[n];
        if (!o->Enabled || o->W <= 0 || o->H <= 0) continue;

        const int xs[4] = { o->X + o->W, o->X - d->W, o->X, o->X + o->W - d->W };
        const int ys[4] = { o->Y + o->H, o->Y - d->H, o->Y, o->Y + o->H - d->H };

        for (int a = 0; a < 4; a++) {
            int dx = xs[a] - d->X;
            if (dx > DG_PRESDL_MON_SNAP || dx < -DG_PRESDL_MON_SNAP) continue;
            for (int b = 0; b < 4; b++) {
                int dy = ys[b] - d->Y;
                if (dy > DG_PRESDL_MON_SNAP || dy < -DG_PRESDL_MON_SNAP) continue;

                int cost = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (bestCost >= 0 && cost >= bestCost) continue;

                bool clear = true;
                for (uint32_t k = 0; k < mo->Count && clear; k++) {
                    const dg_presdl_monitor* q = &mo->Monitors[k];
                    if (k == idx || !q->Enabled || q->W <= 0 || q->H <= 0) continue;
                    clear = !impl_dg_mon_overlap_(q, xs[a], ys[b], d->W, d->H);
                }
                if (!clear) continue;

                bestX = xs[a]; bestY = ys[b]; bestCost = cost;
            }
        }
    }

    if (bestCost >= 0) { d->X = bestX; d->Y = bestY; }
}

// Drag-to-arrange canvas for a set of outputs. Returns the height consumed.
static inline float dg_presdl_draw_monitors(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked, bool held, int entryIndex, int* activeEntry)
{
    dg_presdl_complement_monitors* mo = &e->Complement.Monitors;

    SDL_Color colPanel  = (SDL_Color)DG_PRESDL_COL_PANEL;
    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colDim    = (SDL_Color)DG_PRESDL_COL_TEXT_DIM;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;

    float th      = dg_presdl_text_height(1);
    float labelH  = th + 6.0f;
    float canvasH = (mo->Height > 0.0f) ? mo->Height : 180.0f;
    float canvasX = x + DG_PRESDL_ENTRY_PAD;
    float canvasY = y + labelH;
    float canvasW = w - DG_PRESDL_ENTRY_PAD * 2;
    float total   = labelH + canvasH + DG_PRESDL_ENTRY_PAD;

    impl_dg_fill_rect_(r, x, y, w, total, colPanel);
    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 3.0f, e->Label, colText, 1);

    if (canvasW <= 0.0f) return total;   // section squeezed to nothing

    impl_dg_fill_rect_(r, canvasX, canvasY, canvasW, canvasH, (SDL_Color){ 22, 22, 30, 255 });
    impl_dg_draw_rect_(r, canvasX, canvasY, canvasW, canvasH, colBorder);

    // Room for the hint line, so it never sits under a monitor rect.
    float helpH = (mo->HelpText && canvasH > 48.0f) ? th + 6.0f : 0.0f;
    if (mo->HelpText && helpH > 0.0f)
        impl_dg_text_(r, canvasX + 6, canvasY + canvasH - helpH + 3.0f, mo->HelpText, colDim, 1);

    if (!mo->Monitors || mo->Count == 0) return total;

    // A stale Selected (an output was unplugged and the list shrank) indexed
    // past the end of the array on the very next frame.
    if (mo->Selected && *mo->Selected >= mo->Count) *mo->Selected = mo->Count - 1;

    // Bounding box of every monitor. Accumulated in float so a wild X + W from
    // a config file cannot overflow on the way in.
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool  any = false;
    for (uint32_t i = 0; i < mo->Count; i++) {
        const dg_presdl_monitor* m = &mo->Monitors[i];
        if (m->W <= 0 || m->H <= 0) continue;
        float l = (float)m->X,        t = (float)m->Y;
        float rr = l + (float)m->W,   b = t + (float)m->H;
        if (!any) { minX = l; minY = t; maxX = rr; maxY = b; any = true; continue; }
        if (l  < minX) minX = l;
        if (t  < minY) minY = t;
        if (rr > maxX) maxX = rr;
        if (b  > maxY) maxY = b;
    }

    float bbW = maxX - minX, bbH = maxY - minY;
    float availW = canvasW - 20.0f, availH = canvasH - helpH - 20.0f;
    // Degenerate box (no monitors with a size) or a canvas with no room: the
    // fit below would divide by zero. Leave the empty canvas as drawn.
    if (!any || bbW <= 0.0f || bbH <= 0.0f || availW <= 0.0f || availH <= 0.0f)
        return total;

    float scale = availW / bbW;
    if (availH / bbH < scale) scale = availH / bbH;
    if (!(scale > 0.0f)) return total;

    float offX = canvasX + (canvasW - bbW * scale) * 0.5f - minX * scale;
    float offY = canvasY + (canvasH - helpH - bbH * scale) * 0.5f - minY * scale;

    // Topmost (last drawn) monitor under the pointer.
    int hovIdx = -1;
    for (uint32_t i = 0; i < mo->Count; i++) {
        const dg_presdl_monitor* m = &mo->Monitors[i];
        if (m->W <= 0 || m->H <= 0) continue;
        if (impl_dg_mouse_in_(mx, my, offX + (float)m->X * scale, offY + (float)m->Y * scale,
                         (float)m->W * scale, (float)m->H * scale))
            hovIdx = (int)i;
    }

    bool mine = (activeEntry && *activeEntry == entryIndex);

    // Grab on press, exactly like the slider: `clicked` only fires on release,
    // which is far too late to start a drag.
    if (held && !mine && activeEntry && *activeEntry == -1 &&
            hovIdx >= 0 && mo->Monitors[hovIdx].Enabled) {
        *activeEntry   = entryIndex;
        mine           = true;
        impl_dg_mon_drag_   = hovIdx;
        impl_dg_mon_grab_x_ = (mx - offX) / scale - (float)mo->Monitors[hovIdx].X;
        impl_dg_mon_grab_y_ = (my - offY) / scale - (float)mo->Monitors[hovIdx].Y;
    }

    // Move: the pointer delta is canvas px, the layout is output px.
    if (held && mine && impl_dg_mon_drag_ >= 0 && (uint32_t)impl_dg_mon_drag_ < mo->Count) {
        dg_presdl_monitor* d = &mo->Monitors[impl_dg_mon_drag_];
        if (d->Enabled) {
            d->X = impl_dg_mon_coord_((mx - offX) / scale - impl_dg_mon_grab_x_);
            d->Y = impl_dg_mon_coord_((my - offY) / scale - impl_dg_mon_grab_y_);
        }
    }

    // Release: snap to the nearest neighbouring edge, then hand the pointer back.
    if (!held && mine) {
        if (impl_dg_mon_drag_ >= 0 && (uint32_t)impl_dg_mon_drag_ < mo->Count &&
                mo->Monitors[impl_dg_mon_drag_].Enabled)
            impl_dg_mon_snap_(mo, (uint32_t)impl_dg_mon_drag_);
        impl_dg_mon_drag_ = -1;
        *activeEntry = -1;
    }

    if (clicked && hovIdx >= 0 && mo->Selected) *mo->Selected = (uint32_t)hovIdx;

    // Draw after the interaction so a dragged monitor tracks the pointer in the
    // same frame it moves.
    for (uint32_t i = 0; i < mo->Count; i++) {
        const dg_presdl_monitor* m = &mo->Monitors[i];
        if (m->W <= 0 || m->H <= 0) continue;

        float rx = offX + (float)m->X * scale, ry = offY + (float)m->Y * scale;
        float rw = (float)m->W * scale,        rh = (float)m->H * scale;

        SDL_Color fill = !m->Enabled ? (SDL_Color){ 34, 34, 42, 255 }
                       :  m->Internal ? colAccent
                                      : colPanel;
        impl_dg_fill_rect_(r, rx, ry, rw, rh, fill);

        bool sel = (mo->Selected && *mo->Selected == i);
        SDL_Color edge = sel ? (SDL_Color){ 150, 150, 255, 255 } : colBorder;
        impl_dg_draw_rect_(r, rx, ry, rw, rh, edge);
        if (sel && rw > 6.0f && rh > 6.0f) {
            // One hairline is invisible against the fill — thicken the selection.
            impl_dg_draw_rect_(r, rx + 1, ry + 1, rw - 2, rh - 2, edge);
            impl_dg_draw_rect_(r, rx + 2, ry + 2, rw - 4, rh - 4, edge);
        }

        // Both labels are drawn only when the rect can actually hold them.
        // The fit was strlen() against an 8px cell, which under-measures a wide
        // proportional name and over-measures a narrow one.
        SDL_Color lbl = m->Enabled ? colText : colDim;
        if (m->Name && rh >= th + 6.0f && rw >= dg_presdl_text_width(m->Name, 1) + 8.0f)
            impl_dg_text_(r, rx + 4, ry + 4, m->Name, lbl, 1);

        char size[32];
        int n = SDL_snprintf(size, sizeof size, "%dx%d", m->W, m->H);
        if (n > 0 && rh >= th * 2.0f + 8.0f &&
                rw >= dg_presdl_text_width(size, 1) + 8.0f)
            impl_dg_text_(r, rx + 4, ry + 4.0f + th, size, m->Enabled ? colDim : colBorder, 1);
    }
    return total;
}

// ─── Button-opened text prompt ─────────────────────────────────────────────

// Defined with the other secondary-window code further down, because it is
// built on dg_presdl_surface; declared here so the widget row can open it.
static inline int dg_presdl_prompt_text(const char* title, const char* message,
                                        char* buffer, size_t capacity);

// A read-only mirror of the buffer plus a button that opens the modal editor.
// Returns the height consumed, like every other row handler.
static inline float dg_presdl_draw_prompt(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked,
    int entryIndex, dg_presdl_preconfig_window* win)
{
    if (!win) return DG_PRESDL_ENTRY_H;
    dg_presdl_complement_prompt* pr = &e->Complement.Prompt;
    // Same guard the text and keybind rows take: a caller buffer that is not
    // terminated inside Capacity makes every strlen() below run off the end.
    if (pr->Buffer && pr->Capacity > 0) pr->Buffer[pr->Capacity - 1] = '\0';

    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colPanel  = (SDL_Color)DG_PRESDL_COL_PANEL;
    SDL_Color colAccent = (SDL_Color)DG_PRESDL_COL_ACCENT;
    SDL_Color colHov    = (SDL_Color)DG_PRESDL_COL_ACCENT_HOV;

    const char* label = pr->ButtonLabel ? pr->ButtonLabel : "Edit...";
    float th   = dg_presdl_text_height(1);
    float btnW = dg_presdl_text_width(label, 1) + DG_PRESDL_ENTRY_PAD * 2.0f;
    float btnH = DG_PRESDL_ENTRY_H - 12.0f;
    float btnX = x + w - btnW - DG_PRESDL_ENTRY_PAD;
    float btnY = y + (DG_PRESDL_ENTRY_H - btnH) * 0.5f;

    float boxY = y + 4.0f;
    float boxH = DG_PRESDL_ENTRY_H - 8.0f;
    float boxX = x + w * 0.40f;
    float boxW = btnX - 6.0f - boxX;
    if (boxW < 40.0f) { boxW = 40.0f; boxX = btnX - 6.0f - boxW; }

    impl_dg_fill_rect_(r, x, y, w, DG_PRESDL_ENTRY_H, colPanel);
    impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, y + 10, e->Label, colText, 1);
    // Never focused: the modal is the only way to edit it, so no caret.
    impl_dg_text_field_(r, boxX, boxY, boxW, boxH, pr->Buffer, "(empty)", false);

    bool hov = impl_dg_mouse_in_(mx, my, btnX, btnY, btnW, btnH);
    impl_dg_fill_rect_(r, btnX, btnY, btnW, btnH, hov ? colHov : colAccent);
    impl_dg_text_(r, btnX + DG_PRESDL_ENTRY_PAD, btnY + (btnH - th) * 0.5f, label, colText, 1);

    if (clicked && hov && pr->Buffer && pr->Capacity > 1) {
        // The dialog owns the pointer and the keyboard while it is up: claim
        // the active slot so no page swipe can start behind it, and drop the
        // parent's press state afterwards — its button-up went to the dialog.
        win->ActiveEntryIndex = entryIndex;
        win->TextFocus        = -1;
        win->KeyCapture       = -1;
        if (dg_presdl_prompt_text(pr->Title ? pr->Title : e->Label,
                                  pr->Message, pr->Buffer, pr->Capacity) == 1)
            pr->Changed = true;
        win->ActiveEntryIndex = -1;
        win->_pressed = false;
        win->_dragged = false;
    }

    return DG_PRESDL_ENTRY_H;
}

// ─── Dispatch a single entry ───────────────────────────────────────────────

static inline float dg_presdl_draw_entry(
    SDL_Renderer* r, dg_presdl_entry* e,
    float x, float y, float w,
    float mx, float my, bool clicked, bool held,
    int entryIndex, dg_presdl_preconfig_window* win)
{
    if (!e || !win) return 0.0f;   // dispatch was reached with no entry/window
    // An open dropdown is modal: its list is painted over the page, so every
    // other widget must ignore the pointer or a click meant for an option also
    // toggled whatever sat underneath it.
    if (win->_ddModal >= 0 && win->_ddModal != entryIndex) {
        clicked = false;
        held    = false;
    }
    switch (e->Type) {
        case DG_PRESDL_CHECKBOX:
            return dg_presdl_draw_checkbox(r, e, x, y, w, mx, my, clicked);
        case DG_PRESDL_BUTTON:
            return dg_presdl_draw_button(r, e, x, y, w, mx, my, clicked, &win->ActiveEntryIndex, win);
        case DG_PRESDL_DROPDOWN:
            return dg_presdl_draw_dropdown(r, e, x, y, w, mx, my, clicked, entryIndex, win);
        case DG_PRESDL_SLIDER_32:
            return dg_presdl_draw_slider_32(r, e, x, y, w, mx, my, clicked, held, entryIndex, &win->ActiveEntryIndex);
        case DG_PRESDL_TEXT:
            return dg_presdl_draw_text(r, e, x, y, w, mx, my, clicked, entryIndex, win);
        case DG_PRESDL_MONITORS:
            return dg_presdl_draw_monitors(r, e, x, y, w, mx, my, clicked, held, entryIndex, &win->ActiveEntryIndex);
        case DG_PRESDL_KEYBIND:
            return dg_presdl_draw_keybind(r, e, x, y, w, mx, my, clicked, entryIndex, win);
        case DG_PRESDL_PROMPT:
            return dg_presdl_draw_prompt(r, e, x, y, w, mx, my, clicked, entryIndex, win);
        case DG_PRESDL_PARAGRAPH:
            return dg_presdl_draw_paragraph(r, e, x, y, w);
        case DG_PRESDL_IMAGE:
            return dg_presdl_draw_image(r, e, x, y, w);
        case DG_PRESDL_SECTIONIONING: {
            SDL_Color colText = (SDL_Color)DG_PRESDL_COL_TEXT_DIM;
            SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
            // The rule used to run under the caption at a fixed height, which a
            // taller glyph cell now crosses. It starts after the caption instead.
            float rowH = DG_PRESDL_ENTRY_H * 0.75f;
            float th   = dg_presdl_text_height(1);
            float ty   = y + (rowH - th) * 0.5f;
            float lw   = dg_presdl_text_width(e->Label, 1);
            float lx   = x + DG_PRESDL_ENTRY_PAD + (lw > 0.0f ? lw + 8.0f : 0.0f);
            if (lx < x + w)
                impl_dg_fill_rect_(r, lx, ty + th * 0.5f, x + w - lx, 1, colBorder);
            impl_dg_text_(r, x + DG_PRESDL_ENTRY_PAD, ty, e->Label, colText, 1);
            return rowH;
        }
        default:
            return DG_PRESDL_ENTRY_H;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Section handler
// ═══════════════════════════════════════════════════════════════════════════

// Returns total height drawn
static inline float dg_presdl_draw_section(
    SDL_Renderer* r,
    dg_presdl_preconfig_window* win,
    dg_presdl_section* sec,
    float ox, float oy,       // page content origin (after tabs/header)
    float pageW,
    float mx, float my,
    bool clicked, bool held)
{
    // sec/win are caller-owned and were dereferenced on the next line.
    if (!sec || !win) return 0.0f;
    float x = ox + sec->X;
    float y = oy + sec->Y;
    float w = (sec->W > 0.0f) ? sec->W : (pageW - sec->X - DG_PRESDL_SECTION_PAD);

    SDL_Color colBorder = (SDL_Color)DG_PRESDL_COL_BORDER;
    SDL_Color colText   = (SDL_Color)DG_PRESDL_COL_TEXT_DIM;

    float cursor = y;

    // Section title bar
    if (sec->Title && sec->Title[0]) {
        impl_dg_fill_rect_(r, x, cursor, w, 24, (SDL_Color)DG_PRESDL_COL_SECTION_TITLE);
        impl_dg_draw_rect_(r, x, cursor, w, 24, colBorder);
        impl_dg_text_(r, x + 8, cursor + (24.0f - dg_presdl_text_height(1)) * 0.5f,
                 sec->Title, colText, 1);
        cursor += 24;
    }

    // Section background
    float sectionStartY = cursor;
    float sectionH = (sec->H > 0.0f) ? sec->H : 0; // 0 = auto

    // Entries
    // EntryStart + EntryCount are both uint32_t: a section whose range ran past
    // 2^32 wrapped and the loop silently drew nothing (or, with a smaller
    // wrap, a wrong slice). Compute the bound in 64 bits and clamp it to the
    // array, and stop trusting Entries to be non-NULL.
    uint64_t first = (uint64_t)sec->EntryStart;
    uint64_t last  = first + (uint64_t)sec->EntryCount;
    if (last > win->EntryCount) last = win->EntryCount;
    for (uint64_t i = first; win->Entries && i < last; i++) {
        // separator between entries
        if (i > first) {
            impl_dg_fill_rect_(r, x + 2, cursor, w - 4, 1, colBorder);
        }

        float used = dg_presdl_draw_entry(r, &win->Entries[i], x, cursor, w, mx, my, clicked, held, (int)i, win);
        cursor += used;
    }

    // Draw section outline after we know height
    float autoH = cursor - sectionStartY;
    impl_dg_draw_rect_(r, x, sectionStartY, w,
        sectionH > 0 ? sectionH : autoH, colBorder);
    float totalConsumed = cursor - y;
    return totalConsumed;
}

// ═══════════════════════════════════════════════════════════════════════════
// Page handler
// ═══════════════════════════════════════════════════════════════════════════

static inline void dg_presdl_draw_page(
    SDL_Renderer* r,
    dg_presdl_preconfig_window* win,
    dg_presdl_page* page,
    float tabBarH,
    float mx, float my,
    bool clicked, bool held,
    float scrollDelta)
{
    if (!win || !page) return;   // both are caller-owned handles
    float winW = (float)win->W;
    float winH = (float)win->H;
    float contentY = tabBarH;

    // Header image
    if (page->HeaderImage && page->HeaderImageH > 0) {
        SDL_FRect hdst = { 0, contentY, winW, (float)page->HeaderImageH };
        SDL_RenderTexture(r, page->HeaderImage, NULL, &hdst);
        contentY += (float)page->HeaderImageH;
    }

    // Scroll
    page->ScrollY -= scrollDelta * DG_PRESDL_SCROLL_SPEED;
    float visibleH = winH - contentY;
    // A window shorter than the tab bar + header image (or minimised to zero)
    // gave a negative visible height: a negative-height clip rect, and a
    // maxScroll large enough to let the page scroll away underneath itself.
    if (visibleH < 0.0f) visibleH = 0.0f;
    float maxScroll = page->ContentH - visibleH;
    if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (page->ScrollY < 0.0f)      page->ScrollY = 0.0f;
    if (page->ScrollY > maxScroll) page->ScrollY = maxScroll;
    float oy = contentY - page->ScrollY;
    float totalH = 0;

    // Clip to content area
    SDL_Rect clip = { 0, (int)contentY, win->W, (int)visibleH };
    SDL_SetRenderClipRect(r, &clip);

    // Draw sections
    for (uint32_t s = 0; page->Sections && s < page->SectionCount; s++) {
        dg_presdl_section* sec = &page->Sections[s];
        float used = dg_presdl_draw_section(r, win, sec,
            DG_PRESDL_SECTION_PAD, oy + totalH,
            winW - DG_PRESDL_SECTION_PAD * 2,
            mx, my, clicked, held);
        totalH += used + DG_PRESDL_SECTION_PAD;
    }

    page->ContentH = totalH; // update for scroll clamping next frame

    // Scrollbar
    if (visibleH > 0.0f && page->ContentH > visibleH) {
        float sbH    = visibleH;
        float thumbH = sbH * (sbH / page->ContentH);
        if (thumbH < 12.0f) thumbH = 12.0f;
        if (thumbH > sbH)   thumbH = sbH;
        // The thumb was placed at ScrollY/ContentH of the track, which is not
        // the scrolled fraction: at the bottom of a page it hung off the end
        // of the bar. Scale by the actual scroll range instead.
        float span = page->ContentH - visibleH;
        float frac = span > 0.0f ? page->ScrollY / span : 0.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        float thumbY = contentY + frac * (sbH - thumbH);
        impl_dg_fill_rect_(r, winW - 6, contentY, 6, sbH, (SDL_Color)DG_PRESDL_COL_SCROLL_TRACK);
        impl_dg_fill_rect_(r, winW - 5, thumbY, 4, thumbH, (SDL_Color)DG_PRESDL_COL_ACCENT);
    }

    SDL_SetRenderClipRect(r, NULL);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tab bar
// ═══════════════════════════════════════════════════════════════════════════

static inline void dg_presdl_draw_tabs(
    SDL_Renderer* r,
    dg_presdl_preconfig_window* win,
    float mx, float my, bool clicked)
{
    if (!win) return;
    float tabW = (win->PageCount > 0)
               ? (float)win->W / (float)win->PageCount : (float)win->W;
    float ty = dg_presdl_top_h(win);

    // Pages is caller-owned: PageCount without an array walked a NULL pointer.
    for (uint32_t i = 0; win->Pages && i < win->PageCount; i++) {
        float tx = (float)i * tabW;
        bool active = (i == win->CurrentPage);
        bool hov    = impl_dg_mouse_in_(mx, my, tx, ty, tabW, DG_PRESDL_TAB_H);

        SDL_Color bg = active ? (SDL_Color)DG_PRESDL_COL_TAB_ACTIVE
                     : hov   ? (SDL_Color)DG_PRESDL_COL_TAB_HOV
                              : (SDL_Color)DG_PRESDL_COL_TAB_IDLE;
        impl_dg_fill_rect_(r, tx, ty, tabW, DG_PRESDL_TAB_H, bg);

        // Active indicator bar at bottom of tab
        if (active)
            impl_dg_fill_rect_(r, tx, ty + DG_PRESDL_TAB_H - 3, tabW, 3,
                (SDL_Color)DG_PRESDL_COL_TAB_MARK);

        SDL_Color tc = active ? (SDL_Color)DG_PRESDL_COL_TAB_TEXT
                              : (SDL_Color)DG_PRESDL_COL_TEXT_DIM;
        if (win->Pages[i].Title)
            impl_dg_text_(r, tx + 10,
                     ty + (DG_PRESDL_TAB_H - dg_presdl_text_height(1)) * 0.5f,
                     win->Pages[i].Title, tc, 1);

        if (clicked && hov) win->CurrentPage = i;
    }

    // Bottom border of tab bar
    impl_dg_fill_rect_(r, 0, ty + DG_PRESDL_TAB_H, (float)win->W, 1,
        (SDL_Color)DG_PRESDL_COL_BORDER);
}

// ═══════════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════════

static inline uint32_t dg_presdl_init(
    dg_presdl_preconfig_window* win,
    const char* title,
    int w, int h)
{
    if (!win) return 1;

    // Every failure path below must leave the handle exactly as shutdown()
    // expects it and give back the video refcount it took.
    win->Window   = NULL;
    win->Renderer = NULL;
    win->Font     = (dg_presdl_font){ 0 };

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_InitSubSystem failed: %s", SDL_GetError());
        return 2;
    }

    win->Window = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (!win->Window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);   // was leaked on this path
        return 3;
    }

    win->Renderer = SDL_CreateRenderer(win->Window, NULL);
    if (!win->Renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win->Window);
        // The window was destroyed but its pointer was left dangling, so the
        // obvious caller response — call shutdown() and bail — double-freed it.
        win->Window = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 4;
    }

    win->W           = w;
    win->H           = h;
    win->CurrentPage = 0;
    win->ShouldClose = false;
    win->ActiveEntryIndex = -1;
    impl_dg_mon_drag_     = -1;   // no monitor drag can be in flight in a fresh window
    win->TextFocus   = -1;
    win->KeyCapture  = -1;
    win->_capKey     = SDLK_UNKNOWN;
    win->_ddModal    = -1;   // no expanded dropdown in a fresh window
    win->_ddOpen     = -1;
    win->_ddJustOpened = false;
    // Real glyphs by default, so an app that never calls dg_presdl_font_set()
    // simply looks better. With no Fontconfig match every metric falls back to
    // the 8px debug cell and the window lays out exactly as it always did.
    if (!dg_presdl_font_set(win, nullptr, DG_PRESDL_FONT_PX))
        SDL_Log("dg_presdl: no system font resolved, using the debug font");
    impl_dg_font_bind_(&win->Font);
    // Text-input events (platform-agnostic) flow while any field is focused.
    SDL_StartTextInput(win->Window);
    return 0;
}

static inline void dg_presdl_shutdown(dg_presdl_preconfig_window* win) {
    if (!win) return;
    // Balance the init's SDL_InitSubSystem only when there was a window to tear
    // down: a second shutdown, or one after a failed init, would otherwise drop
    // the video refcount again and pull SDL out from under any live surface.
    bool live = (win->Window != NULL);
    // Atlas sheets first: they are textures owned by the renderer below.
    impl_dg_font_free_(&win->Font);
    if (win->Renderer) SDL_DestroyRenderer(win->Renderer);
    if (win->Window)   SDL_DestroyWindow(win->Window);
    win->Renderer = NULL;
    win->Window   = NULL;
    win->TextFocus = -1;
    if (win->_paste) { SDL_free(win->_paste); win->_paste = NULL; }
    win->KeyCapture = -1;
    win->ActiveEntryIndex = -1;
    impl_dg_mon_drag_ = -1;
    if (live) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// ═══════════════════════════════════════════════════════════════════════════
// Secondary surfaces — extra window handles
// ═══════════════════════════════════════════════════════════════════════════
//
// A bare window + renderer handle with no page/tab chrome: for dialogs, an app
// chooser, tool palettes, or any extra surface an app wants to open alongside
// its main window. Pure SDL, so it stays platform-agnostic. Draw into it with
// the _dg_* primitives (or raw SDL) between begin() and present(). Events are
// routed by matching the SDL window id, so one event loop can drive many.

typedef struct dg_presdl_surface {
    SDL_Window*   Window;
    SDL_Renderer* Renderer;
    SDL_WindowID  Id;         // for routing events to this handle
    int           W, H;       // current size
    bool          ShouldClose;// set when this window's close is requested

    // Its own glyph atlas: sheets are textures, and a texture belongs to the
    // renderer that made it, so a surface can share a *font* with a window but
    // never its sheets. Left unloaded, text here falls back to the debug font.
    dg_presdl_font  Font;
    dg_presdl_font* _PrevFont;  // font bound around begin()/present()
} dg_presdl_surface;

// Create a new window handle/surface. Returns false on failure (handle zeroed).
// Assumes SDL video is already initialized (dg_presdl_init or another surface).
static inline bool dg_presdl_surface_create(
    dg_presdl_surface* s, const char* title, int w, int h)
{
    if (!s) return false;
    *s = (dg_presdl_surface){ 0 };
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return false;
    }
    s->Window = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (!s->Window) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);   // the refcount taken above leaked
        return false;
    }
    s->Renderer = SDL_CreateRenderer(s->Window, NULL);
    if (!s->Renderer) {
        SDL_DestroyWindow(s->Window);
        s->Window = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);   // ditto, with the window already gone
        return false;
    }
    s->Id = SDL_GetWindowID(s->Window);
    s->W  = w;
    s->H  = h;
    return true;
}

// Feed a polled event to a surface: updates size and sets ShouldClose on its
// own close request. Ignores events for other windows, so it is safe to call
// for every surface with every event.
static inline void dg_presdl_surface_handle_event(
    dg_presdl_surface* s, const SDL_Event* e)
{
    if (!s || !e || !s->Window) return;
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            e->window.windowID == s->Id) {
        s->ShouldClose = true;
    } else if (e->type == SDL_EVENT_WINDOW_RESIZED &&
            e->window.windowID == s->Id) {
        s->W = e->window.data1;
        s->H = e->window.data2;
    }
}

// Adopt `family` at `pixel_size` for this surface's own atlas. Same contract as
// dg_presdl_font_set(): false leaves whatever was loaded before intact.
static inline bool dg_presdl_surface_font_set(dg_presdl_surface* s,
                                              const char* family, int pixel_size) {
#ifdef DG_PRESDL_NO_FREETYPE
    (void)s; (void)family; (void)pixel_size;
    return false;
#else
    if (!s || !s->Renderer) return false;
    char path[512];
    if (!impl_dg_fc_resolve_(family, path, sizeof path)) return false;
    if (pixel_size <= 0) pixel_size = DG_PRESDL_FONT_PX;
    if (!impl_dg_font_load_(&s->Font, s->Renderer, path, pixel_size)) return false;
    SDL_strlcpy(s->Font.Family, (family && family[0]) ? family : "sans-serif",
                sizeof s->Font.Family);
    return true;
#endif
}

// Borrow the face and size of whichever window is currently drawing (falling
// back to the system sans), rasterized into this surface's own sheets. This is
// what makes a dialog match the app that opened it.
static inline bool dg_presdl_surface_font_inherit(dg_presdl_surface* s) {
    if (!s || !s->Renderer) return false;
    const dg_presdl_font* cur = impl_dg_font_current_();
    if (cur && cur->Path[0]) {
        int px = cur->PixelSize;
        char path[512];
        SDL_strlcpy(path, cur->Path, sizeof path);
        if (impl_dg_font_load_(&s->Font, s->Renderer, path, px)) return true;
    }
    return dg_presdl_surface_font_set(s, nullptr, DG_PRESDL_FONT_PX);
}

// Clear the surface to bg, ready for drawing.
static inline void dg_presdl_surface_begin(dg_presdl_surface* s, SDL_Color bg) {
    if (!s || !s->Renderer) return;
    // Text drawn into this surface must come out of this surface's sheets; the
    // caller's font is put back by present().
    s->_PrevFont = impl_dg_font_;
    impl_dg_font_bind_(&s->Font);
    SDL_SetRenderDrawColor(s->Renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(s->Renderer);
}

// Present what was drawn since begin().
static inline void dg_presdl_surface_present(dg_presdl_surface* s) {
    if (!s || !s->Renderer) return;
    SDL_RenderPresent(s->Renderer);
    impl_dg_font_bind_(s->_PrevFont);
    s->_PrevFont = nullptr;
}

// Destroy the window handle (idempotent).
static inline void dg_presdl_surface_destroy(dg_presdl_surface* s) {
    if (!s) return;
    // Balance create()'s SDL_InitSubSystem only while this handle still owns a
    // window: destroy() is documented idempotent, and a repeated quit would
    // drop the video refcount below what the surviving windows hold.
    bool live = (s->Window != NULL);
    // Sheets before the renderer that owns them; also clears impl_dg_font_ if this
    // surface's font happened to be the bound one.
    impl_dg_font_free_(&s->Font);
    s->_PrevFont = nullptr;
    if (s->Renderer) SDL_DestroyRenderer(s->Renderer);
    if (s->Window)   SDL_DestroyWindow(s->Window);
    s->Renderer = NULL;
    s->Window   = NULL;
    s->Id       = 0;
    if (live) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}


// ═══════════════════════════════════════════════════════════════════════════
// Modal text prompt
// ═══════════════════════════════════════════════════════════════════════════

// A blocking text-entry dialog on its own window: title, optional message, one
// edit field pre-seeded with `buffer`, and OK / Cancel. Enter accepts, Escape
// cancels, closing the window cancels. Returns 1 on accept (buffer rewritten,
// NUL-terminated, never cut mid-code-point), 0 on cancel, -1 if the window
// could not be created.
//
// It pumps events itself, which is what makes it callable as a plain function
// from inside a widget handler. That pump is deliberately not filtered by
// window id for keys: a modal owns the keyboard, and under a bare X server with
// no window manager nothing may hold focus at all. Mouse events *are* filtered,
// so a click in the parent window cannot press OK.
static inline int dg_presdl_prompt_text(const char* title, const char* message,
                                        char* buffer, size_t capacity)
{
    if (!buffer || capacity < 2) return -1;
    buffer[capacity - 1] = '\0';

    dg_presdl_surface s;
    if (!dg_presdl_surface_create(&s, (title && title[0]) ? title : "Input", 460, 200))
        return -1;
    dg_presdl_surface_font_inherit(&s);

    // Edited on a copy: Cancel must leave the caller's buffer byte-identical.
    char* edit = (char*)SDL_malloc(capacity);
    if (!edit) {
        dg_presdl_surface_destroy(&s);
        return -1;
    }
    SDL_strlcpy(edit, buffer, capacity);

    // Size the dialog to the font it actually got, then centre it.
    dg_presdl_font* prevFont = impl_dg_font_;
    impl_dg_font_bind_(&s.Font);
    float th     = dg_presdl_text_height(1);
    float pad    = 16.0f;
    float fieldH = th + 12.0f;
    float btnH   = th + 14.0f;
    int   wantH  = (int)(pad * 2.0f + th + 8.0f +
                         ((message && message[0]) ? th + 10.0f : 0.0f) +
                         fieldH + 16.0f + btnH);
    impl_dg_font_bind_(prevFont);
    if (wantH > 120) {
        SDL_SetWindowSize(s.Window, 460, wantH);
        s.H = wantH;
    }
    SDL_SetWindowPosition(s.Window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_StartTextInput(s.Window);

    SDL_Color colText = (SDL_Color)DG_PRESDL_COL_TEXT;
    SDL_Color colDim  = (SDL_Color)DG_PRESDL_COL_TEXT_DIM;
    SDL_Color colAcc  = (SDL_Color)DG_PRESDL_COL_ACCENT;
    SDL_Color colHov  = (SDL_Color)DG_PRESDL_COL_ACCENT_HOV;
    SDL_Color colIdle = (SDL_Color)DG_PRESDL_COL_TAB_IDLE;
    SDL_Color colBrd  = (SDL_Color)DG_PRESDL_COL_BORDER;

    int  result   = 0;
    bool running  = true;
    bool pressed  = false;
    float mx = -1.0f, my = -1.0f;

    while (running) {
        int  backspaces = 0;
        char input[32]  = { 0 };
        bool released   = false;
        SDL_Event ev;

        while (SDL_PollEvent(&ev)) {
            dg_presdl_surface_handle_event(&s, &ev);
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    result  = 0;
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    if (ev.text.text)
                        SDL_strlcat(input, ev.text.text, sizeof input);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (ev.key.key == SDLK_ESCAPE)            { running = false; result = 0; }
                    else if (ev.key.key == SDLK_RETURN ||
                             ev.key.key == SDLK_KP_ENTER)     { running = false; result = 1; }
                    else if (ev.key.key == SDLK_BACKSPACE)      backspaces++;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (ev.motion.windowID == s.Id) { mx = ev.motion.x; my = ev.motion.y; }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (ev.button.windowID == s.Id && ev.button.button == SDL_BUTTON_LEFT) {
                        pressed = true;
                        mx = ev.button.x; my = ev.button.y;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (ev.button.windowID == s.Id &&
                            ev.button.button == SDL_BUTTON_LEFT && pressed) {
                        pressed  = false;
                        released = true;
                        mx = ev.button.x; my = ev.button.y;
                    }
                    break;
            }
        }
        if (s.ShouldClose) { running = false; result = 0; }

        impl_dg_text_edit_(edit, capacity, backspaces, input);

        dg_presdl_surface_begin(&s, (SDL_Color)DG_PRESDL_COL_BG);

        float W = (float)s.W, H = (float)s.H;
        th     = dg_presdl_text_height(1);
        fieldH = th + 12.0f;
        btnH   = th + 14.0f;

        float cy = pad;
        impl_dg_text_(s.Renderer, pad, cy, (title && title[0]) ? title : "Input", colText, 1);
        cy += th + 8.0f;
        if (message && message[0]) {
            impl_dg_text_(s.Renderer, pad, cy, message, colDim, 1);
            cy += th + 10.0f;
        }
        impl_dg_text_field_(s.Renderer, pad, cy, W - pad * 2.0f, fieldH, edit, nullptr, true);

        float okW  = dg_presdl_text_width("OK", 1) + 40.0f;
        float canW = dg_presdl_text_width("Cancel", 1) + 30.0f;
        float by   = H - pad - btnH;
        float okX  = W - pad - okW;
        float canX = okX - 8.0f - canW;
        bool  hovOk  = impl_dg_mouse_in_(mx, my, okX,  by, okW,  btnH);
        bool  hovCan = impl_dg_mouse_in_(mx, my, canX, by, canW, btnH);

        impl_dg_fill_rect_(s.Renderer, okX, by, okW, btnH, hovOk ? colHov : colAcc);
        impl_dg_text_(s.Renderer, okX + (okW - dg_presdl_text_width("OK", 1)) * 0.5f,
                 by + (btnH - th) * 0.5f, "OK", colText, 1);
        impl_dg_fill_rect_(s.Renderer, canX, by, canW, btnH, hovCan ? (SDL_Color){ 60, 60, 78, 255 } : colIdle);
        impl_dg_draw_rect_(s.Renderer, canX, by, canW, btnH, colBrd);
        impl_dg_text_(s.Renderer, canX + (canW - dg_presdl_text_width("Cancel", 1)) * 0.5f,
                 by + (btnH - th) * 0.5f, "Cancel", colText, 1);

        dg_presdl_surface_present(&s);

        if (released) {
            if (hovOk)       { running = false; result = 1; }
            else if (hovCan) { running = false; result = 0; }
        }
        SDL_Delay(8);
    }

    if (result == 1) SDL_strlcpy(buffer, edit, capacity);
    SDL_StopTextInput(s.Window);
    SDL_free(edit);
    dg_presdl_surface_destroy(&s);
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main frame — call once per loop iteration
// ═══════════════════════════════════════════════════════════════════════════

static inline void dg_presdl_frame(dg_presdl_preconfig_window* win) {
    if (!win || !win->Renderer) return;   // nothing to poll for or draw into
    // Every metric and glyph this frame comes from this window's atlas; a modal
    // prompt opened mid-frame swaps it out and back around its own drawing.
    impl_dg_font_bind_(&win->Font);
    SDL_Event event;
    bool released    = false;
    float scrollDelta = 0;
    float mx = 0, my = 0;

    SDL_GetMouseState(&mx, &my);

    dg_presdl_page* cur =
        (win->Pages && win->PageCount > 0 && win->CurrentPage < win->PageCount)
        ? &win->Pages[win->CurrentPage] : NULL;

    // Per-frame text input, consumed by the focused text field while drawing.
    win->_textInput[0] = '\0';
    win->_backspaces = 0;
    if (win->_paste) { SDL_free(win->_paste); win->_paste = NULL; }   // last frame's paste was applied
    win->_capKey = SDLK_UNKNOWN; // capture is per-frame, like the text input
    bool editing = (win->TextFocus >= 0);

    // Which dropdown (if any) is expanded going into this frame. It owns the
    // pointer, the wheel and Esc until it closes; _ddOpen/_ddJustOpened are
    // re-established by the draw pass below.
    win->_ddModal = -1;
    for (uint64_t i = 0; win->Entries && i < win->EntryCount; i++) {
        if (win->Entries[i].Type == DG_PRESDL_DROPDOWN &&
                win->Entries[i].Complement.Dropdown.Open) {
            win->_ddModal = (int)i;
            break;
        }
    }
    win->_ddOpen = -1;
    win->_ddJustOpened = false;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                win->ShouldClose = true;
                break;
            case SDL_EVENT_TEXT_INPUT: {
                // Append the typed UTF-8, bounded to the frame buffer. SDL only
                // documents text as non-NULL for real input; a synthesised event
                // with none would have been walked by strlcat.
                if (event.text.text)
                    SDL_strlcat(win->_textInput, event.text.text, sizeof(win->_textInput));
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                // An armed key-capture row owns the keyboard: Esc cancels it,
                // every other non-modifier key becomes the new binding.
                if (win->KeyCapture >= 0) {
                    if (event.key.key == SDLK_ESCAPE) win->KeyCapture = -1;
                    else if (!impl_dg_key_is_mod_(event.key.key)) {
                        win->_capKey  = event.key.key;
                        win->_capMods = event.key.mod;
                    }
                    break;
                }
                if (event.key.key == SDLK_ESCAPE) {
                    // Esc collapses an open dropdown first, then blurs a
                    // focused field, then closes the window.
                    if (win->_ddModal >= 0) {
                        win->Entries[win->_ddModal].Complement.Dropdown.Open = false;
                        win->_ddModal = -1;
                    }
                    else if (editing) win->TextFocus = -1;
                    else win->ShouldClose = true;
                } else if (editing && ((event.key.key == SDLK_V && (event.key.mod & SDL_KMOD_CTRL)) ||
                                       (event.key.key == SDLK_INSERT && (event.key.mod & SDL_KMOD_SHIFT)))) {
                    // Paste into the focused field; a field is one line, so line breaks and tabs become spaces.
                    if (!win->_paste && SDL_HasClipboardText()) {
                        win->_paste = SDL_GetClipboardText();
                        for (char* c = win->_paste; c && *c; c++)
                            if (*c == '\r' || *c == '\n' || *c == '\t') *c = ' ';
                    }
                } else if (editing && event.key.key == SDLK_BACKSPACE) {
                    win->_backspaces++;
                } else if (editing && (event.key.key == SDLK_RETURN ||
                                       event.key.key == SDLK_KP_ENTER)) {
                    win->TextFocus = -1; // commit / blur
                } else if (!editing && cur && event.key.key == SDLK_DOWN)      scrollDelta -= 1;
                else if (!editing && cur && event.key.key == SDLK_UP)        scrollDelta += 1;
                else if (!editing && cur && event.key.key == SDLK_PAGEDOWN)  scrollDelta -= 5;
                else if (!editing && cur && event.key.key == SDLK_PAGEUP)    scrollDelta += 5;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    win->_pressed = true;
                    win->_dragged = false;
                    win->_pressY = event.button.y;
                    win->_pressScrollY = cur ? cur->ScrollY : 0;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && win->_pressed) {
                    released = true;
                    win->_pressed = false;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                // Was a plain assignment: a second wheel event in the same frame
                // (or a wheel notch after an arrow key) discarded the earlier
                // scroll instead of adding to it.
                scrollDelta += event.wheel.y;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                win->W = event.window.data1;
                win->H = event.window.data2;
                break;
        }
    }

    // A held press that moves past a threshold is a swipe (scrolls the page);
    // released in place it is a click. This also lets touch/drag scroll menus
    // whose content runs off-screen.
    // Once a widget owns the pointer (slider handle, monitor being dragged) the
    // motion belongs to it. Without this, any vertical travel past the 6px
    // threshold turned the gesture into a page swipe and cancelled the drag —
    // which makes a two-axis drag widget impossible.
    bool grabbed = (win->ActiveEntryIndex >= 0);
    if (win->_pressed && cur && !grabbed && win->_ddModal < 0) {
        float dy = my - win->_pressY;
        float ady = dy < 0 ? -dy : dy;
        if (!win->_dragged && ady > 6.0f) win->_dragged = true;
        if (win->_dragged) cur->ScrollY = win->_pressScrollY - dy; // clamped in draw
    }
    bool clicked = released && !win->_dragged;
    // Sliders track the pointer only while pressed and not mid-scroll.
    bool held = win->_pressed && (!win->_dragged || grabbed);

    // ── Draw ──
    SDL_Color bg = (SDL_Color)DG_PRESDL_COL_BG;
    SDL_SetRenderDrawColor(win->Renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(win->Renderer);

    if (win->CurrentPage != win->PrevPage) {
        // Close all dropdowns on the page we just left
        for (uint64_t i = 0; win->Entries && i < win->EntryCount; i++) {
            if (win->Entries[i].Type == DG_PRESDL_DROPDOWN)
                win->Entries[i].Complement.Dropdown.Open = false;
        }
        win->ActiveEntryIndex = -1;
        win->KeyCapture = -1;   // an armed row on the old page must not stay armed
        win->_ddModal = -1;
        win->PrevPage = win->CurrentPage;
    }

    float topH = dg_presdl_top_h(win);
    if (win->TopBar && win->Entries && win->TopBar >= win->Entries
        && (uint64_t)(win->TopBar - win->Entries) < win->EntryCount) {
        impl_dg_fill_rect_(win->Renderer, 0, 0, (float)win->W, topH, (SDL_Color)DG_PRESDL_COL_TAB_IDLE);
        dg_presdl_draw_entry(win->Renderer, win->TopBar, DG_PRESDL_SECTION_PAD, 4.0f,
                             (float)win->W - 2.0f * DG_PRESDL_SECTION_PAD, mx, my, clicked, held,
                             (int)(win->TopBar - win->Entries), win);
    }

    dg_presdl_draw_tabs(win->Renderer, win, mx, my,
        clicked && win->_ddModal < 0);

    if (cur) {
        dg_presdl_draw_page(
            win->Renderer, win, cur,
            topH + (float)DG_PRESDL_TAB_H,
            // The wheel scrolls the open option list, not the page behind it.
            mx, my, clicked, held, win->_ddModal >= 0 ? 0.0f : scrollDelta);
    }

    // On top of the page, and after it: the list is an overlay, and its click
    // must win over every widget it covers.
    dg_presdl_draw_dropdown_overlay(win, mx, my, clicked, scrollDelta);

    SDL_RenderPresent(win->Renderer);
}

// ═══════════════════════════════════════════════════════════════════════════
// Template macro
// ═══════════════════════════════════════════════════════════════════════════

#define DG_PRESDL_TEMPLATE_INSERT \
    dg_presdl_entry entries[] = { \
        { "Rendering Device", DG_PRESDL_DROPDOWN, .Complement.Dropdown = { \
            .Options     = gpu_names, \
            .OptionCount = gpu_count, \
            .Selected    = &cfg.gpu_index, \
            .HelpText    = "Select which GPU to use" \
        }}, \
        { "Fullscreen", DG_PRESDL_CHECKBOX, .Complement.Checkbox = { \
            .Value    = &cfg.fullscreen, \
            .HelpText = "Run in fullscreen mode" \
        }}, \
        { "Launch", DG_PRESDL_BUTTON, .Complement.Button = { \
            .Label    = "Launch Game", \
            .OnClick  = on_launch_clicked, \
            .UserData = &cfg \
        }}, \
    };

