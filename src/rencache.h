#ifndef RENCACHE_H
#define RENCACHE_H

#include <stdbool.h>
#include <lua.h>
#include "renderer.h"

typedef struct RenCache {
    unsigned *cells_buf1;
    unsigned *cells_buf2;
    unsigned *cells_prev;
    unsigned *cells;
    RenRect *rect_buf;
    size_t command_buf_size;
    uint8_t *command_buf;
    bool resize_issue;
    int command_buf_idx;
    RenRect screen_rect;
    RenRect last_clip_rect;
    bool show_debug;
} RenCache;

RenCache* rencache_create();
void rencache_destroy(RenCache* cache);

void  rencache_show_debug(RenCache* cache, bool enable);
void  rencache_set_clip_rect(RenCache* cache, RenRect rect);
void  rencache_draw_rect(RenCache* cache, RenRect rect, RenColor color);
double rencache_draw_text(RenCache* cache, RenFont **font, const char *text, size_t len, double x, int y, RenColor color);
void  rencache_invalidate(RenCache* cache);
void  rencache_begin_frame(RenCache* cache, RenWindow* renwindow);
void  rencache_end_frame(RenCache* cache, RenWindow* renwindow);

extern RenCache* rencache;

#endif
