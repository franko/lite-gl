#ifndef RENCACHE_H
#define RENCACHE_H

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <lua.h>
#include "renderer.h"

#define CELLS_X 80
#define CELLS_Y 50

typedef struct RenCache {
    // Buffers for command storage remain
    size_t command_buf_size;
    uint8_t *command_buf;
    int command_buf_idx;
    bool resize_issue;

    // Simplified hash tracking
    unsigned current_hash;
    unsigned previous_hash;

    // Frame state
    SDL_FRect surface_rect;
    SDL_FRect last_clip_rect;
    float x_origin, y_origin;
    bool frame_started;
    bool first_draw;
    bool show_debug;
} RenCache;

void rencache_init(RenCache *cache, float x_origin, float y_origin);
void rencache_destroy(RenCache* cache);
void  rencache_show_debug(RenCache* cache, bool enable);
void  rencache_set_clip_rect(RenCache* cache, const SDL_FRect *rect);
void  rencache_draw_rect(RenCache* cache, const SDL_FRect *rect, RenColor color);
double rencache_draw_text(RenCache* cache, RenFont **font, const char *text, size_t len, double x, int y, RenColor color);
void  rencache_invalidate(RenCache* cache);
void  rencache_begin_frame(RenCache* cache, RenSurface* rs);
void  rencache_end_frame(RenCache* cache, RenSurface* rs);

#endif
