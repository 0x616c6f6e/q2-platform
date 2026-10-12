#ifndef Q2_PC_WINDOW_H
#define Q2_PC_WINDOW_H
#include "q2/lvgl.h"
typedef struct q2_pc_window q2_pc_window_t;
/** Create a native Win32 / Linux SDL2 client area with pixel geometry. headless skips window creation for
 * automation. */
q2_result_t q2_pc_window_create(unsigned width, unsigned height, bool headless, q2_pc_window_t **out);
/** Copy the synchronous pixel backend; the window must outlive the registered display. */
q2_result_t q2_pc_window_backend(q2_pc_window_t *window, q2_pixel_backend_t *out);
/** Dispatch window/mouse messages on the Kernel owner. out_open becomes false on close. */
q2_result_t q2_pc_window_pump(q2_pc_window_t *window, q2_lvgl_t *display, bool *out_open);
/** Consume Escape/Back as a navigation request; window close remains a separate application exit. */
q2_result_t q2_pc_window_take_home(q2_pc_window_t *window, bool *out_home);
/** Queue a test click through native window messages, or feed the same pointer API when headless. */
q2_result_t q2_pc_window_click(q2_pc_window_t *window, unsigned x, unsigned y);
/** Export the actual LVGL framebuffer as a 32-bit BMP, before shutting down the APP. */
q2_result_t q2_pc_window_capture(q2_pc_window_t *window, const char *path);
/** Destroy after runtime/kernel/provider teardown; closes only this simulator's window. */
q2_result_t q2_pc_window_destroy(q2_pc_window_t **window);
#endif
