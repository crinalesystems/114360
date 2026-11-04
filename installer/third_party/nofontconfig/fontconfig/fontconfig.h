// Fontconfig stand-in for the Windows build of the installer: mingw has FreeType but no Fontconfig. dg_presdl.h
// only uses Fontconfig to look fonts up by family name; the installer loads its own font file
// (dg_presdl_font_set_file), so every lookup here simply finds nothing and preSDL keeps that font.
#pragma once

typedef unsigned char FcChar8;
typedef int FcBool;
typedef struct FcPattern FcPattern;
typedef struct FcObjectSet FcObjectSet;
typedef struct FcFontSet {
    int nfont, sfont;
    FcPattern **fonts;
} FcFontSet;
typedef enum { FcResultMatch, FcResultNoMatch, FcResultTypeMismatch, FcResultNoId, FcResultOutOfMemory } FcResult;
typedef enum { FcMatchPattern, FcMatchFont, FcMatchScan } FcMatchKind;

#define FC_FAMILY "family"
#define FC_FILE "file"

static inline FcBool FcInit(void) { return 0; }
static inline void FcFini(void) {}
static inline FcPattern *FcNameParse(const FcChar8 *name) { (void)name; return nullptr; }
static inline FcPattern *FcPatternCreate(void) { return nullptr; }
static inline void FcPatternDestroy(FcPattern *p) { (void)p; }
static inline FcBool FcConfigSubstitute(void *config, FcPattern *p, FcMatchKind kind)
{
    (void)config; (void)p; (void)kind;
    return 0;
}
static inline void FcDefaultSubstitute(FcPattern *p) { (void)p; }
static inline FcPattern *FcFontMatch(void *config, FcPattern *p, FcResult *result)
{
    (void)config; (void)p;
    *result = FcResultNoMatch;
    return nullptr;
}
static inline FcResult FcPatternGetString(const FcPattern *p, const char *object, int n, FcChar8 **s)
{
    (void)p; (void)object; (void)n; (void)s;
    return FcResultNoMatch;
}
static inline FcObjectSet *FcObjectSetBuild(const char *first, ...) { (void)first; return nullptr; }
static inline void FcObjectSetDestroy(FcObjectSet *os) { (void)os; }
static inline FcFontSet *FcFontList(void *config, FcPattern *p, FcObjectSet *os)
{
    (void)config; (void)p; (void)os;
    return nullptr;
}
static inline void FcFontSetDestroy(FcFontSet *fs) { (void)fs; }
