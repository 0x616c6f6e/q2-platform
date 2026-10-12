#define WIN32_LEAN_AND_MEAN
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>

struct q2_pc_window {
    HWND handle;
    unsigned width, height;
    uint32_t *pixels;
    BITMAPINFO bitmap;
    q2_lvgl_t *display;
    bool open, home;
    q2_result_t error;
};
static void pointer(q2_pc_window_t *w, int x, int y, bool pressed) {
    if (!w->display)
        return;
    /* Captured mouse coordinates can lie outside the client area; clamp before forwarding. */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if ((unsigned)x >= w->width)
        x = (int)w->width - 1;
    if ((unsigned)y >= w->height)
        y = (int)w->height - 1;
    q2_result_t e = q2_lvgl_pointer(w->display, x, y, pressed);
    if (e != Q2_OK && w->error == Q2_OK)
        w->error = e;
}
static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM key, LPARAM value) {
    q2_pc_window_t *w = (q2_pc_window_t *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        w = ((CREATESTRUCTW *)value)->lpCreateParams;
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)w);
    }
    if (!w)
        return DefWindowProcW(handle, message, key, value);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(handle, &paint);
        SetDIBitsToDevice(dc, 0, 0, w->width, w->height, 0, 0, 0, w->height, w->pixels, &w->bitmap,
                          DIB_RGB_COLORS);
        EndPaint(handle, &paint);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (w->display) {
            q2_result_t e = q2_lvgl_scroll(w->display, GET_WHEEL_DELTA_WPARAM(key) / 3);
            if (e != Q2_OK && w->error == Q2_OK)
                w->error = e;
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(handle);
        pointer(w, GET_X_LPARAM(value), GET_Y_LPARAM(value), true);
        return 0;
    case WM_LBUTTONUP:
        pointer(w, GET_X_LPARAM(value), GET_Y_LPARAM(value), false);
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        pointer(w, GET_X_LPARAM(value), GET_Y_LPARAM(value), (key & MK_LBUTTON) != 0);
        return 0;
    case WM_CAPTURECHANGED:
        pointer(w, 0, 0, false);
        return 0;
    case WM_KEYDOWN:
        if ((key == VK_ESCAPE || key == VK_BROWSER_BACK) && !(value & (1L << 30)))
            w->home = true;
        return 0;
    case WM_CLOSE:
        /* Keep the HWND alive until the Kernel releases its display and flushes the last frame. */
        w->open = false;
        return 0;
    default:
        return DefWindowProcW(handle, message, key, value);
    }
}
static q2_result_t flush(void *ctx, unsigned x, unsigned y, unsigned width, unsigned height,
                         const uint32_t *pixels, size_t stride) {
    q2_pc_window_t *w = ctx;
    if (!pixels || x > w->width || width > w->width - x || y > w->height || height > w->height - y ||
        stride < (size_t)width * 4)
        return Q2_ERR_ARGUMENT;
    for (unsigned row = 0; row < height; row++)
        memcpy(w->pixels + (size_t)(y + row) * w->width + x, (const unsigned char *)pixels + row * stride,
               (size_t)width * 4);
    if (w->handle) {
        RECT area = {(LONG)x, (LONG)y, (LONG)(x + width), (LONG)(y + height)};
        InvalidateRect(w->handle, &area, FALSE);
    }
    return Q2_OK;
}
q2_result_t q2_pc_window_create(unsigned width, unsigned height, bool headless, q2_pc_window_t **out) {
    if (!out || width < 64 || width > 1024 || height < 64 || height > 1024)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    q2_pc_window_t *w = calloc(1, sizeof(*w));
    if (!w)
        return Q2_ERR_MEMORY;
    w->pixels = calloc((size_t)width * height, 4);
    if (!w->pixels) {
        free(w);
        return Q2_ERR_MEMORY;
    }
    w->width = width;
    w->height = height;
    w->open = true;
    w->bitmap.bmiHeader = (BITMAPINFOHEADER){.biSize = sizeof(BITMAPINFOHEADER),
                                             .biWidth = (LONG)width,
                                             .biHeight = -(LONG)height,
                                             .biPlanes = 1,
                                             .biBitCount = 32,
                                             .biCompression = BI_RGB};
    if (!headless) {
        SetProcessDPIAware();
        HINSTANCE instance = GetModuleHandleW(NULL);
        WNDCLASSW cls = {.lpfnWndProc = procedure,
                         .hInstance = instance,
                         .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
                         .lpszClassName = L"Q2KernelPcDisplay"};
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            free(w->pixels);
            free(w);
            return Q2_ERR_IO;
        }
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect = {0, 0, (LONG)width, (LONG)height};
        AdjustWindowRect(&rect, style, FALSE);
        w->handle =
            CreateWindowExW(0, cls.lpszClassName, L"Q2-Kernel | LVGL PC", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, instance, w);
        if (!w->handle) {
            free(w->pixels);
            free(w);
            return Q2_ERR_IO;
        }
        ShowWindow(w->handle, SW_SHOWNORMAL);
    }
    *out = w;
    return Q2_OK;
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
    MSG msg;
    while (PeekMessageW(&msg, w->handle, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    *out_open = w->open;
    q2_result_t e = w->error;
    w->error = Q2_OK;
    return e;
}
q2_result_t q2_pc_window_click(q2_pc_window_t *w, unsigned x, unsigned y) {
    if (!w || !w->display || x >= w->width || y >= w->height)
        return Q2_ERR_ARGUMENT;
    if (w->handle) {
        LPARAM point = MAKELPARAM(x, y);
        return PostMessageW(w->handle, WM_LBUTTONDOWN, MK_LBUTTON, point) &&
                       PostMessageW(w->handle, WM_LBUTTONUP, 0, point)
                   ? Q2_OK
                   : Q2_ERR_IO;
    }
    q2_result_t e = q2_lvgl_pointer(w->display, (int)x, (int)y, true);
    return e == Q2_OK ? q2_lvgl_pointer(w->display, (int)x, (int)y, false) : e;
}
q2_result_t q2_pc_window_capture(q2_pc_window_t *w, const char *path) {
    if (!w || !path)
        return Q2_ERR_ARGUMENT;
    FILE *f = fopen(path, "wb");
    if (!f)
        return Q2_ERR_IO;
    DWORD bytes = w->width * w->height * 4;
    BITMAPFILEHEADER header = {.bfType = 0x4d42,
                               .bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bytes,
                               .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1 &&
              fwrite(&w->bitmap.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f) == 1 &&
              fwrite(w->pixels, bytes, 1, f) == 1;
    if (fclose(f))
        ok = false;
    return ok ? Q2_OK : Q2_ERR_IO;
}
q2_result_t q2_pc_window_destroy(q2_pc_window_t **ref) {
    if (!ref || !*ref)
        return Q2_ERR_ARGUMENT;
    q2_pc_window_t *w = *ref;
    w->display = NULL;
    if (w->handle)
        DestroyWindow(w->handle);
    free(w->pixels);
    free(w);
    *ref = NULL;
    return Q2_OK;
}
