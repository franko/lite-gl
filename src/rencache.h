#ifndef RENCACHE_H
#define RENCACHE_H

#include <stdbool.h>
#include <lua.h>
#include "renderer.h"

#define CELLS_X 80
#define CELLS_Y 50

typedef struct RenCache {
    unsigned cells_buf1[CELLS_X * CELLS_Y];
    unsigned cells_buf2[CELLS_X * CELLS_Y];
    unsigned *cells_prev;
    unsigned *cells;
    RenRect rect_buf[CELLS_X * CELLS_Y / 2];
    size_t command_buf_size;
    uint8_t *command_buf;
    bool resize_issue;
    int command_buf_idx;
    RenRect surface_rect;
    RenRect last_clip_rect;
    int rect_count;
    int x_origin, y_origin;
    bool show_debug;
} RenCache;

void rencache_init(RenCache *cache, int x_origin, int y_origin);
void rencache_destroy(RenCache* cache);
void  rencache_show_debug(RenCache* cache, bool enable);
void  rencache_set_clip_rect(RenCache* cache, RenRect rect);
void  rencache_draw_rect(RenCache* cache, RenRect rect, RenColor color);
double rencache_draw_text(RenCache* cache, RenFont **font, const char *text, size_t len, double x, int y, RenColor color);
void  rencache_invalidate(RenCache* cache);
void  rencache_begin_frame(RenCache* cache, RenSurface* rs);
void  rencache_end_frame(RenCache* cache, RenSurface* rs);
void  rencache_swap_buffers(RenCache* cache);
void  rencache_update_rects(RenCache* cache, RenSurface *rs);

#endif
