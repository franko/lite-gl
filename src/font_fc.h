#pragma once
#include <stdbool.h>

/*
 * Resolve a fontconfig pattern to an absolute file path.
 * Returns a malloc()-allocated string on success, or NULL on failure.
 * Caller must free() the returned pointer.
 */
extern char *fc_resolve_font(const char *pattern, int want_bold, int want_italic);
extern bool fc_load_custom_config(const char *datadir_utf8, const char *cfg_path);

