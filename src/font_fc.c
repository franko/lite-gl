#include <fontconfig/fontconfig.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "font_fc.h"

static char *app_fonts_dir = NULL; /* set from Lua */

void fc_set_app_fonts_dir(const char *dir_utf8)
{
  if (app_fonts_dir) return;          /* already set */
  app_fonts_dir = strdup(dir_utf8);
}

/* ---------------------------------------------------------------------------
 * Make Fontconfig aware of the fonts shipped inside Lite XL.
 * We add DATADIR/fonts as an “application font” directory; fonts found there
 * are matched before any system-wide ones, so a plain family name like
 * “JetBrains Mono” resolves to the bundled copy when available.
 * --------------------------------------------------------------------------- */
static void fc_register_app_fonts(void)
{
  static int done = 0;
  if (done) return;
  done = 1;

  if (!app_fonts_dir)
    return;

  if (!FcInit())
    return;

  const char *dir = app_fonts_dir;
  FcConfig *cfg = FcConfigGetCurrent();
  if (FcConfigAppFontAddDir(cfg, (const FcChar8*)dir)) {
    /* Re-generate font database so the new faces are immediately usable. */
    FcConfigBuildFonts(cfg);
  } else {
    fprintf(stderr, "Fontconfig: failed to add app font dir %s\n", dir);
  }

  free(app_fonts_dir);
  app_fonts_dir = NULL;
}

char *fc_resolve_font(const char *pattern,
                      int want_bold,
                      int want_italic)
{
  /* Ensure bundled fonts are registered before matching. */
  fc_register_app_fonts();

  if (!FcInit())
    return NULL;

  FcPattern *p = FcNameParse((const FcChar8*)pattern);
  if (!p)
    return NULL;

  /* Apply desired style hints if specified */
  if (want_bold   >= 0)
    FcPatternAddInteger(p, FC_WEIGHT,
                        want_bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
  if (want_italic >= 0)
    FcPatternAddInteger(p, FC_SLANT,
                        want_italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

  FcConfigSubstitute(NULL, p, FcMatchPattern);
  FcDefaultSubstitute(p);

  FcResult res;
  FcPattern *match = FcFontMatch(NULL, p, &res);
  FcPatternDestroy(p);
  if (!match)
    return NULL;

  FcChar8 *file = NULL;
  if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
    FcPatternDestroy(match);
    return NULL;
  }

  char *out = strdup((const char*)file);
  FcPatternDestroy(match);
  return out; /* caller frees */
}
#include <limits.h>

#ifdef _WIN32
  #define FC_PATHSEP "\\"
#else
  #define FC_PATHSEP "/"
#endif

#if defined(_WIN32) || defined(__APPLE__)
bool fc_load_custom_config(const char *datadir_utf8)
{
  if (!datadir_utf8) return false;

  char cfg_path[PATH_MAX];
  snprintf(cfg_path, sizeof(cfg_path), "%s%sfontconfig%sfonts.conf",
           datadir_utf8, FC_PATHSEP, FC_PATHSEP);

  /* Ensure Fontconfig is initialised */
  FcInit();

  FcConfig *cfg = FcConfigCreate();
  if (!cfg)
    return false;

  /* Load bundled configuration */
  if (!FcConfigParseAndLoad(cfg, (const FcChar8*)cfg_path, FcTrue)) {
    FcConfigDestroy(cfg);
    return false;
  }

  /* Add bundled fonts directory (safe if already present in XML) */
  char fonts_dir[PATH_MAX];
  snprintf(fonts_dir, sizeof(fonts_dir), "%s%sfonts", datadir_utf8, FC_PATHSEP);
  FcConfigAppFontAddDir(cfg, (const FcChar8*)fonts_dir);

  FcConfigSetCurrent(cfg);
  return true;
}
#endif
