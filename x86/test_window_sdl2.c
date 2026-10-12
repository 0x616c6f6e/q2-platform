#define _POSIX_C_SOURCE 200809L
#include "window.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                                  \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
struct q2_lvgl {
    unsigned calls;
    int x, y, scroll;
    bool pressed;
};
q2_result_t q2_lvgl_pointer(q2_lvgl_t *d, int x, int y, bool pressed) {
    d->calls++;
    d->x = x;
    d->y = y;
    d->pressed = pressed;
    return Q2_OK;
}
q2_result_t q2_lvgl_scroll(q2_lvgl_t *d, int delta) {
    d->scroll += delta;
    return Q2_OK;
}
static void run(bool headless) {
    q2_pc_window_t *w = NULL;
    q2_lvgl_t display = {0};
    bool open, home;
    CHECK(q2_pc_window_create(63, 64, headless, &w) == Q2_ERR_ARGUMENT);
    CHECK(q2_pc_window_create(64, 96, headless, &w) == Q2_OK);
    q2_pixel_backend_t b;
    CHECK(q2_pc_window_backend(w, &b) == Q2_OK);
    CHECK(b.width == 64 && b.height == 96);
    uint32_t pixels[] = {0xff0000, 0x00ff00, 0x123456, 0x0000ff, 0xffffff, 0x654321};
    CHECK(b.flush(b.context, 0, 0, 2, 2, pixels, 12) == Q2_OK);
    pixels[0] = 0; /* Flush must copy, not retain caller-owned pixels. */
    CHECK(b.flush(b.context, 63, 95, 2, 1, pixels, 12) == Q2_ERR_ARGUMENT);
    CHECK(b.flush(b.context, 0, 0, 2, 1, pixels, 4) == Q2_ERR_ARGUMENT);
    CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK && open);
    char capture[] = "/tmp/q2-sdl-capture-XXXXXX";
    int fd = mkstemp(capture);
    CHECK(fd >= 0 && close(fd) == 0);
    CHECK(q2_pc_window_capture(w, capture) == Q2_OK);
    SDL_Surface *loaded = SDL_LoadBMP(capture);
    CHECK(loaded && loaded->w == 64 && loaded->h == 96);
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    CHECK(rgb != NULL);
    uint32_t *first = rgb->pixels;
    uint32_t *second = (uint32_t *)((unsigned char *)rgb->pixels + rgb->pitch);
    CHECK((first[0] & 0xffffff) == 0xff0000 && (first[1] & 0xffffff) == 0x00ff00);
    CHECK((second[0] & 0xffffff) == 0x0000ff && (second[1] & 0xffffff) == 0xffffff);
    CHECK((first[2] & 0xffffff) == 0);
    SDL_FreeSurface(rgb);
    SDL_FreeSurface(loaded);
    CHECK(unlink(capture) == 0);
    CHECK(q2_pc_window_click(w, 10, 20) == Q2_OK);
    CHECK(display.x == 10 && display.y == 20 && !display.pressed);
    CHECK(q2_pc_window_click(w, 64, 0) == Q2_ERR_ARGUMENT);
    if (!headless) {
        SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = 10;
        e.button.y = 20;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK);
        CHECK(display.pressed && display.x == 10 && display.y == 20);
        e = (SDL_Event){.type = SDL_MOUSEMOTION};
        e.motion.state = SDL_BUTTON_LMASK;
        e.motion.x = -10;
        e.motion.y = 200;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK);
        CHECK(display.x == 0 && display.y == 95 && display.pressed);
        e = (SDL_Event){.type = SDL_WINDOWEVENT};
        e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK && !display.pressed);
        e = (SDL_Event){.type = SDL_MOUSEWHEEL};
        e.wheel.y = -2;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK && display.scroll == -80);
        e = (SDL_Event){.type = SDL_KEYDOWN};
        e.key.keysym.sym = SDLK_ESCAPE;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK);
        CHECK(q2_pc_window_take_home(w, &home) == Q2_OK && home);
        CHECK(q2_pc_window_take_home(w, &home) == Q2_OK && !home);
        e = (SDL_Event){.type = SDL_QUIT};
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(q2_pc_window_pump(w, &display, &open) == Q2_OK && !open);
    }
    CHECK(q2_pc_window_destroy(&w) == Q2_OK && !w);
}
int main(void) {
    run(true);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    run(false);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    puts("PASS: SDL2/headless RGB, stride/copy, capture, input, clamp, focus loss, navigation, close");
    return 0;
}
