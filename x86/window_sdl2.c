#include "window.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct q2_pc_window {
    SDL_Window *handle;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    q2_lvgl_t *display;
    unsigned width, height;
    int pointer_x, pointer_y;
    bool video, open, home, dirty, pressed;
};
static q2_result_t sdl_error(const char *operation) {
    fprintf(stderr, "Q2 SDL2 %s: %s\n", operation, SDL_GetError());
    return Q2_ERR_IO;
}
static q2_result_t pointer(q2_pc_window_t *w, int x, int y, bool pressed) {
    /* Logical-size rendering maps native events into device pixels; capture can leave the window. */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if ((unsigned)x >= w->width)
        x = (int)w->width - 1;
    if ((unsigned)y >= w->height)
        y = (int)w->height - 1;
    w->pointer_x = x;
    w->pointer_y = y;
    w->pressed = pressed;
    return q2_lvgl_pointer(w->display, x, y, pressed);
}
static q2_result_t flush(void *ctx, unsigned x, unsigned y, unsigned width, unsigned height,
                         const uint32_t *pixels, size_t stride) {
    q2_pc_window_t *w = ctx;
    if (!pixels || x > w->width || width > w->width - x || y > w->height || height > w->height - y ||
        stride < (size_t)width * sizeof(*pixels))
        return Q2_ERR_ARGUMENT;
    for (unsigned row = 0; row < height; row++)
        memcpy(w->pixels + (size_t)(y + row) * w->width + x, (const unsigned char *)pixels + row * stride,
               (size_t)width * sizeof(*pixels));
    /* LVGL can flush multiple strips in one poll. Present the composed frame once on the next pump. */
    w->dirty = true;
    return Q2_OK;
}
q2_result_t q2_pc_window_create(unsigned width, unsigned height, bool headless, q2_pc_window_t **out) {
    if (!out || width < 64 || width > 1024 || height < 64 || height > 1024)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    q2_pc_window_t *w = calloc(1, sizeof(*w));
    if (!w)
        return Q2_ERR_MEMORY;
    w->pixels = calloc((size_t)width * height, sizeof(*w->pixels));
    if (!w->pixels) {
        free(w);
        return Q2_ERR_MEMORY;
    }
    w->width = width;
    w->height = height;
    w->open = true;
    w->dirty = true;
    /* Headless runs never initialize a video driver; they also work without DISPLAY/Wayland. */
    if (!headless) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
            goto failure;
        w->video = true;
        w->handle = SDL_CreateWindow("Q2-Kernel | LVGL / SDL2", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, (int)width, (int)height,
                                     SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
        if (!w->handle)
            goto failure;
        w->renderer = SDL_CreateRenderer(w->handle, -1, SDL_RENDERER_ACCELERATED);
        if (!w->renderer)
            w->renderer = SDL_CreateRenderer(w->handle, -1, SDL_RENDERER_SOFTWARE);
        if (!w->renderer || SDL_RenderSetLogicalSize(w->renderer, (int)width, (int)height) != 0)
            goto failure;
        w->texture = SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       (int)width, (int)height);
        /* The Kernel supplies 0x00RRGGBB: ignore the high byte rather than treating it as transparency. */
        if (!w->texture || SDL_SetTextureBlendMode(w->texture, SDL_BLENDMODE_NONE) != 0)
            goto failure;
    }
    *out = w;
    return Q2_OK;
failure:
    sdl_error("create (use a desktop/WSLg session, or --headless --frames N)");
    q2_pc_window_destroy(&w);
    return Q2_ERR_IO;
}
q2_result_t q2_pc_window_backend(q2_pc_window_t *w, q2_pixel_backend_t *out) {
    if (!w || !out)
        return Q2_ERR_ARGUMENT;
    *out = (q2_pixel_backend_t){Q2_PIXEL_ABI, sizeof(*out), w, w->width, w->height, flush};
    return Q2_OK;
}
q2_result_t q2_pc_window_take_home(q2_pc_window_t *w, bool *out) {
    if (!w || !out)
        return Q2_ERR_ARGUMENT;
    *out = w->home;
    w->home = false;
    return Q2_OK;
}
q2_result_t q2_pc_window_pump(q2_pc_window_t *w, q2_lvgl_t *display, bool *out_open) {
    if (!w || !display || !out_open)
        return Q2_ERR_ARGUMENT;
    w->display = display;
    q2_result_t e = Q2_OK;
    SDL_Event event;
    while (w->handle && e == Q2_OK && SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            w->open = false;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE)
                w->open = false;
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                w->dirty = true;
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && w->pressed)
                e = pointer(w, w->pointer_x, w->pointer_y, false);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                bool down = event.type == SDL_MOUSEBUTTONDOWN;
                e = pointer(w, event.button.x, event.button.y, down);
                /* Capture is best-effort: Wayland/dummy drivers may not implement it. */
                SDL_CaptureMouse(down ? SDL_TRUE : SDL_FALSE);
            }
            break;
        case SDL_MOUSEMOTION:
            e = pointer(w, event.motion.x, event.motion.y, (event.motion.state & SDL_BUTTON_LMASK) != 0);
            break;
        case SDL_MOUSEWHEEL: {
            int steps = event.wheel.y;
            if (steps > 100)
                steps = 100;
            if (steps < -100)
                steps = -100;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                steps = -steps;
            e = q2_lvgl_scroll(display, steps * 40);
            break;
        }
        case SDL_KEYDOWN:
            if (!event.key.repeat &&
                (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_AC_BACK))
                w->home = true;
            break;
        default:
            break;
        }
    }
    /* SDL's default touch-to-mouse events share the same LVGL path; don't forward finger events twice. */
    if (e == Q2_OK && w->renderer && w->dirty) {
        if (SDL_UpdateTexture(w->texture, NULL, w->pixels, (int)w->width * 4) != 0 ||
            SDL_RenderClear(w->renderer) != 0 || SDL_RenderCopy(w->renderer, w->texture, NULL, NULL) != 0)
            e = sdl_error("present");
        else {
            SDL_RenderPresent(w->renderer);
            w->dirty = false;
        }
    }
    *out_open = w->open;
    return e;
}
q2_result_t q2_pc_window_click(q2_pc_window_t *w, unsigned x, unsigned y) {
    if (!w || !w->display || x >= w->width || y >= w->height)
        return Q2_ERR_ARGUMENT;
    q2_result_t e = pointer(w, (int)x, (int)y, true);
    return e == Q2_OK ? pointer(w, (int)x, (int)y, false) : e;
}
q2_result_t q2_pc_window_capture(q2_pc_window_t *w, const char *path) {
    if (!w || !path)
        return Q2_ERR_ARGUMENT;
    SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(w->pixels, (int)w->width, (int)w->height, 32,
                                                    (int)w->width * 4, 0xff0000, 0xff00, 0xff, 0);
    if (!surface)
        return sdl_error("capture surface");
    int result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    return result == 0 ? Q2_OK : sdl_error("save capture");
}
q2_result_t q2_pc_window_destroy(q2_pc_window_t **ref) {
    if (!ref || !*ref)
        return Q2_ERR_ARGUMENT;
    q2_pc_window_t *w = *ref;
    if (w->texture)
        SDL_DestroyTexture(w->texture);
    if (w->renderer)
        SDL_DestroyRenderer(w->renderer);
    if (w->handle)
        SDL_DestroyWindow(w->handle);
    if (w->video)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    free(w->pixels);
    free(w);
    *ref = NULL;
    return Q2_OK;
}
