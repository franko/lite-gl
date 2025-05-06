#include <fontconfig/fontconfig.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "font_fc.h"

char *fc_resolve_font(const char *pattern,
                      int want_bold,
                      int want_italic)
{
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

bool fc_load_custom_config(const char *datadir_utf8, const char *cfg_path)
{
  FcInit();

  if (!datadir_utf8) return false;

  FcConfig *cfg = FcConfigCreate();
  if (!cfg)
    return false;

  fprintf(stderr, "DEBUG: explicit load of fontconfig config %s\n", cfg_path); fflush(stderr);
  /* Load bundled configuration */
  if (!FcConfigParseAndLoad(cfg, (const FcChar8*)cfg_path, FcTrue)) {
    FcConfigDestroy(cfg);
    return false;
  }

  /* Add bundled fonts directory (safe if already present in XML) */
  char fonts_dir[PATH_MAX];
  snprintf(fonts_dir, sizeof(fonts_dir), "%s%sfonts", datadir_utf8, FC_PATHSEP);
  fprintf(stderr, "DEBUG: adding application fonts directory %s to fontconfig\n", fonts_dir); fflush(stderr);
  FcConfigAppFontAddDir(cfg, (const FcChar8*)fonts_dir);

  FcConfigSetCurrent(cfg);
  return true;
}

