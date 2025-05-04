#pragma once
/*
 * Resolve a fontconfig pattern to an absolute file path.
 * Returns a malloc()-allocated string on success, or NULL on failure.
 * Caller must free() the returned pointer.
 */
void  fc_set_app_fonts_dir(const char *dir_utf8);

char *fc_resolve_font(const char *pattern,
                      int want_bold,
                      int want_italic);

#include <stdbool.h>
bool lite_fc_load_custom_config(const char *datadir_utf8);
