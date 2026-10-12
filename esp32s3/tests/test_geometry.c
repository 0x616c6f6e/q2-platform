#include "am178_geometry.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(q2_am178_rgb565(0xff0000) == 0xf800);
    assert(q2_am178_rgb565(0x00ff00) == 0x07e0);
    assert(q2_am178_rgb565(0x0000ff) == 0x001f);
    assert(q2_am178_rgb565(0xffffff) == 0xffff);
    unsigned checked = 0;
    for (unsigned orientation = 0; orientation < 4; ++orientation) {
        unsigned rotation = orientation * 90;
        unsigned w = orientation % 2 ? Q2_AM178_HEIGHT : Q2_AM178_WIDTH,
                 h = orientation % 2 ? Q2_AM178_WIDTH : Q2_AM178_HEIGHT;
        /* Independent corner expectations guard against a mutually inverse but wrong orientation. */
#if Q2_PANEL_AM213
        const unsigned corners[4][4][2] = {
            {{0, 0}, {409, 0}, {0, 501}, {409, 501}},
            {{0, 501}, {0, 0}, {409, 501}, {409, 0}},
            {{409, 501}, {0, 501}, {409, 0}, {0, 0}},
            {{409, 0}, {409, 501}, {0, 0}, {0, 501}}
        };
#else
        const unsigned corners[4][4][2] = {
            {{0, 0}, {367, 0}, {0, 447}, {367, 447}},
            {{0, 447}, {0, 0}, {367, 447}, {367, 0}},
            {{367, 447}, {0, 447}, {367, 0}, {0, 0}},
            {{367, 0}, {367, 447}, {0, 0}, {0, 447}}
        };
#endif
        for (unsigned corner = 0; corner < 4; ++corner) {
            unsigned nx, ny;
            assert(q2_am178_native(w, h, rotation, corner % 2 ? w - 1 : 0,
                                    corner / 2 ? h - 1 : 0, &nx, &ny));
            assert(nx == corners[orientation][corner][0] && ny == corners[orientation][corner][1]);
        }
        bool visited[Q2_AM178_WIDTH * Q2_AM178_HEIGHT] = {0};
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                unsigned nx, ny;
                int tx, ty;
                assert(q2_am178_native(w, h, rotation, x, y, &nx, &ny));
                assert(nx < Q2_AM178_WIDTH && ny < Q2_AM178_HEIGHT);
                assert(!visited[ny * Q2_AM178_WIDTH + nx]);
                visited[ny * Q2_AM178_WIDTH + nx] = true;
                uint8_t b[Q2_TOUCH_REPORT_BYTES] = {0};
#if Q2_PANEL_AM213
                b[2] = 1; b[3] = nx >> 8; b[4] = nx; b[5] = ny >> 8; b[6] = ny;
#else
                b[0] = 1 | ((nx >> 8) << 6) | ((ny >> 8) << 7); b[1] = nx; b[2] = ny;
#endif
                assert(q2_am178_contact(w, h, rotation, b, &tx, &ty));
                assert(tx == (int)x && ty == (int)y);
                ++checked;
            }
        unsigned nx, ny;
        int x, y;
        uint8_t release[Q2_TOUCH_REPORT_BYTES] = {0};
        uint8_t multi[Q2_TOUCH_REPORT_BYTES] = {0}, bad[Q2_TOUCH_REPORT_BYTES] = {0};
#if Q2_PANEL_AM213
        multi[2] = 2;
        bad[2] = 1; bad[3] = 1; bad[4] = 154; /* X=410, just outside the display. */
#else
        multi[0] = 2; bad[0] = 0xc1; bad[1] = 255; bad[2] = 255;
#endif
        assert(!q2_am178_contact(w, h, rotation, release, &x, &y));
        assert(!q2_am178_contact(w, h, rotation, multi, &x, &y));
        assert(!q2_am178_contact(w, h, rotation, bad, &x, &y));
        assert(!q2_am178_native(w, h, rotation, w, 0, &nx, &ny));
        assert(!q2_am178_native(w, h, rotation, 0, h, &nx, &ny));
    }
    assert(!q2_am178_dimensions(502, 410, 0));
    assert(!q2_am178_dimensions(368, 448, 90));
    assert(!q2_am178_dimensions(448, 368, 180));
    assert(!q2_am178_dimensions(368, 448, 360));
    unsigned nx, ny;
    int x, y;
    uint8_t report[Q2_TOUCH_REPORT_BYTES] = {1, 0, 0};
    assert(!q2_am178_native(368, 448, 45, 0, 0, &nx, &ny));
    assert(!q2_am178_native(368, 448, 0, 0, 0, NULL, &ny));
    assert(!q2_am178_contact(368, 448, 45, report, &x, &y));
    assert(!q2_am178_contact(368, 448, 0, NULL, &x, &y));
    printf("PASS: %u native/touch round trips, RGB565 and bounds\n", checked);
}
