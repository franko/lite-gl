#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#ifdef _MSC_VER
  #ifndef alignof
    #define alignof _Alignof
  #endif
  /* max_align_t is a compiler defined type, but
  ** MSVC doesn't provide it, so we'll have to improvise */
  typedef long double max_align_t;
#else
  #include <stdalign.h>
#endif

#include <lauxlib.h>
#include "rencache.h"
#include "renwindow.h"

/* A cache over the software renderer. All drawing operations are stored as
** commands. At the end of the frame, a single hash of the command buffer is
** generated and compared to the previous frame's hash. If the hashes differ,
** the entire screen is redrawn. This avoids redrawing for static frames. */

#define CMD_BUF_RESIZE_RATE 1.2
#define CMD_BUF_INIT_SIZE (1024 * 512)
#define COMMAND_BARE_SIZE offsetof(Command, command)

enum CommandType { SET_CLIP, DRAW_TEXT, DRAW_RECT };

typedef struct {
  enum CommandType type;
  uint32_t size;
  /* Commands *must* always begin with a RenRect
  ** This is done to ensure alignment */
  RenRect command[];
} Command;

typedef struct {
  RenRect rect;
} SetClipCommand;

typedef struct {
  RenRect rect;
  RenColor color;
  RenFont *fonts[FONT_FALLBACK_MAX];
  float text_x;
  size_t len;
  int8_t tab_size;
  char text[];
} DrawTextCommand;

typedef struct {
  RenRect rect;
  RenColor color;
} DrawRectCommand;

/* 32bit fnv-1a hash */
#define HASH_INITIAL 2166136261

void rencache_init(RenCache *cache, int x, int y) {
  cache->command_buf_size = 0;
  cache->command_buf = NULL;
  cache->resize_issue = false;
  cache->command_buf_idx = 0;
  cache->current_hash = HASH_INITIAL;
  cache->previous_hash = HASH_INITIAL - 1; // Ensure first frame always draws
  cache->surface_rect = (RenRect){0};
  cache->last_clip_rect = (RenRect){0};
  cache->x_origin = x;
  cache->y_origin = y;
  cache->show_debug = false;
  cache->frame_started = false;
  cache->first_draw = true;
}

void rencache_destroy(RenCache* cache) {
  free(cache->command_buf);
}

static inline int rencache_min(int a, int b) { return a < b ? a : b; }
static inline int rencache_max(int a, int b) { return a > b ? a : b; }

static inline void rect_set_from_origin(RenCache *cache, RenRect *r) {
  r->x = r->x - cache->x_origin;
  r->y = r->y - cache->y_origin;
}

static void hash(unsigned *h, const void *data, int size) {
  const unsigned char *p = data;
  while (size--) {
    *h = (*h ^ *p++) * 16777619;
  }
}


static inline bool rects_overlap(RenRect a, RenRect b) {
  return b.x + b.width  >= a.x && b.x <= a.x + a.width
      && b.y + b.height >= a.y && b.y <= a.y + a.height;
}


static RenRect intersect_rects(RenRect a, RenRect b) {
  int x1 = rencache_max(a.x, b.x);
  int y1 = rencache_max(a.y, b.y);
  int x2 = rencache_min(a.x + a.width, b.x + b.width);
  int y2 = rencache_min(a.y + a.height, b.y + b.height);
  return (RenRect) { x1, y1, rencache_max(0, x2 - x1), rencache_max(0, y2 - y1) };
}


static bool expand_command_buffer(RenCache* cache) {
  size_t new_size = cache->command_buf_size * CMD_BUF_RESIZE_RATE;
  if (new_size == 0) {
    new_size = CMD_BUF_INIT_SIZE;
  }
  uint8_t *new_command_buf = realloc(cache->command_buf, new_size);
  if (!new_command_buf) {
    return false;
  }
  cache->command_buf_size = new_size;
  cache->command_buf = new_command_buf;
  return true;
}

static void* push_command(RenCache* cache, enum CommandType type, int size) {
  if (cache->resize_issue) {
    // Don't push new commands as we had problems resizing the command buffer.
    // Let's wait for the next frame.
    return NULL;
  }
  size_t alignment = alignof(max_align_t) - 1;
  size += COMMAND_BARE_SIZE;
  size = (size + alignment) & ~alignment;
  int n = cache->command_buf_idx + size;
  while (n > cache->command_buf_size) {
    if (!expand_command_buffer(cache)) {
      fprintf(stderr, "Warning: (" __FILE__ "): unable to resize command buffer (%zu)\n",
              (size_t)(cache->command_buf_size * CMD_BUF_RESIZE_RATE));
      cache->resize_issue = true;
      return NULL;
    }
  }
  Command *cmd = (Command*) (cache->command_buf + cache->command_buf_idx);
  cache->command_buf_idx = n;
  memset(cmd, 0, size);
  cmd->type = type;
  cmd->size = size;
  return cmd->command;
}


static bool next_command(RenCache *cache, Command **prev) {
  if (*prev == NULL) {
    *prev = (Command*) cache->command_buf;
  } else {
    *prev = (Command*) (((char*) *prev) + (*prev)->size);
  }
  return *prev != ((Command*) (cache->command_buf + cache->command_buf_idx));
}


void rencache_show_debug(RenCache* cache, bool enable) {
  cache->show_debug = enable;
}


void rencache_set_clip_rect(RenCache* cache, RenRect rect) {
  rect_set_from_origin(cache, &rect);
  SetClipCommand *cmd = push_command(cache, SET_CLIP, sizeof(SetClipCommand));
  if (cmd) {
    cmd->rect = intersect_rects(rect, cache->surface_rect);
    cache->last_clip_rect = cmd->rect;
  }
}


void rencache_draw_rect(RenCache* cache, RenRect rect, RenColor color) {
  rect_set_from_origin(cache, &rect);
  if (rect.width == 0 || rect.height == 0 || !rects_overlap(cache->last_clip_rect, rect)) {
    return;
  }
  DrawRectCommand *cmd = push_command(cache, DRAW_RECT, sizeof(DrawRectCommand));
  if (cmd) {
    cmd->rect = rect;
    cmd->color = color;
  }
}

double rencache_draw_text(RenCache* cache, RenFont **fonts, const char *text, size_t len, double x, int y, RenColor color)
{
  int x_offset;
  double width = ren_font_group_get_width(fonts, text, len, &x_offset);
  RenRect rect = { x + x_offset, y, (int)(width - x_offset), ren_font_group_get_height(fonts) };
  rect_set_from_origin(cache, &rect);
  if (rects_overlap(cache->last_clip_rect, rect)) {
    int sz = len + 1;
    DrawTextCommand *cmd = push_command(cache, DRAW_TEXT, sizeof(DrawTextCommand) + sz);
    if (cmd) {
      memcpy(cmd->text, text, sz);
      cmd->color = color;
      memcpy(cmd->fonts, fonts, sizeof(RenFont*)*FONT_FALLBACK_MAX);
      cmd->rect = rect;
      cmd->text_x = x - cache->x_origin;
      cmd->len = len;
      cmd->tab_size = ren_font_group_get_tab_size(fonts);
    }
  }
  return x + width;
}


void rencache_begin_frame(RenCache* cache, RenSurface* rs) {
  /* reset state if the screen width/height has changed */
  int w, h;
  rensurf_get_size(rs, &w, &h);
  cache->resize_issue = false;

  if (cache->surface_rect.width != w || h != cache->surface_rect.height) {
    cache->surface_rect.width = w;
    cache->surface_rect.height = h;
    cache->first_draw = true;
    cache->previous_hash = HASH_INITIAL - 1; // Force redraw on resize
  }

  cache->command_buf_idx = 0;
  cache->current_hash = HASH_INITIAL;
  cache->last_clip_rect = cache->surface_rect; // Clip to whole surface initially
  cache->frame_started = true;
}


void rencache_end_frame(RenCache* cache, RenSurface *rs) {
  /* 1. Calculate a single hash for the entire frame's commands */
  Command *cmd = NULL;
  RenRect cr = cache->surface_rect;
  while (next_command(cache, &cmd)) {
    // We still need to respect clip rects for the hash calculation
    if (cmd->type == SET_CLIP) { cr = cmd->command[0]; }
    RenRect r = intersect_rects(cmd->command[0], cr);
    if (r.width == 0 || r.height == 0) { continue; }

    hash(&cache->current_hash, cmd, cmd->size);
  }

  /* 2. Compare hash with previous frame. If unchanged, do nothing. */
  if (!cache->first_draw && cache->current_hash == cache->previous_hash) {
    cache->frame_started = false;
    return; // Perfect cache hit, nothing to do.
  }

  /* 3. Hashes differ (or first frame), so redraw the entire screen. */
  ren_set_clip_rect(rs, cache->surface_rect);

  cmd = NULL;
  while (next_command(cache, &cmd)) {
    SetClipCommand *ccmd = (SetClipCommand*)&cmd->command;
    DrawRectCommand *rcmd = (DrawRectCommand*)&cmd->command;
    DrawTextCommand *tcmd = (DrawTextCommand*)&cmd->command;
    switch (cmd->type) {
      case SET_CLIP:
        ren_set_clip_rect(rs, intersect_rects(ccmd->rect, cache->surface_rect));
        break;
      case DRAW_RECT:
        ren_draw_rect(rs, rcmd->rect, rcmd->color);
        break;
      case DRAW_TEXT:
        ren_font_group_set_tab_size(tcmd->fonts, tcmd->tab_size);
        ren_draw_text(rs, tcmd->fonts, tcmd->text, tcmd->len, tcmd->text_x, tcmd->rect.y, tcmd->color);
        break;
    }
  }

  if (cache->show_debug) {
    RenColor color = { rand(), rand(), rand(), 50 };
    ren_draw_rect(rs, cache->surface_rect, color);
  }

  /* 4. Mark the entire window surface for update */
  rensurf_update_rects(rs, &cache->surface_rect, 1);

  /* 5. Store hash for next frame and update state */
  cache->previous_hash = cache->current_hash;
  cache->frame_started = false;
  cache->first_draw = false;
}
