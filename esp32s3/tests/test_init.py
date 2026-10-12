"""Compile the real init table with host-only SDK type shims; no panel hardware is simulated."""
from pathlib import Path
import itertools
import subprocess
import tempfile
import unittest

MAIN = Path(__file__).resolve().parents[1] / "main"


class InitBrightnessTests(unittest.TestCase):
    def test_single_brightness_command_and_config_precedence(self):
        for product, am213 in itertools.product((True, False), repeat=2):
            for percent, register in ((0, 0x00), (25, 0x40), (80, 0xCC), (100, 0xFF)):
                with self.subTest(product=product, am213=am213, percent=percent), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    # Only the SDK command descriptor type is stubbed. Values and conversion are production code.
                    (root / "esp_lcd_qspi_amoled.h").write_text(
                        "#include <stdint.h>\n#include <stddef.h>\n"
                        "typedef struct { int cmd; const void *data; size_t data_bytes; unsigned delay_ms; } "
                        "qspi_amoled_lcd_init_cmd_t;\n"
                    )
                    (root / "esp_lcd_co5300.h").write_text(
                        (root / "esp_lcd_qspi_amoled.h").read_text().replace(
                            "qspi_amoled_lcd_init_cmd_t", "co5300_lcd_init_cmd_t"))
                    expected_register = (percent * 255 // 100) if am213 else register
                    (root / "q2_project_config.h").write_text(f"#define Q2_DISPLAY_BRIGHTNESS {percent}u\n#define Q2_DISPLAY_PANEL_AM213 {int(am213)}\n")
                    fallback = 100 if product else percent
                    (root / "sdkconfig.h").write_text(f"#define CONFIG_Q2_AM178_INITIAL_BRIGHTNESS {fallback}\n")
                    (root / "test.c").write_text(
                        '#include "am178_vendor_init.h"\n#include <assert.h>\n'
                        'int main(void) { unsigned found = 0, formats = 0;\n'
                        f'assert(Q2_PANEL_AM213 == {int(am213)});\n'
                        'for (unsigned i=0; i<sizeof(q2_am178_vendor_init_cmds)/sizeof(*q2_am178_vendor_init_cmds); ++i) {\n'
                        'const q2_panel_init_cmd_t *c = &q2_am178_vendor_init_cmds[i];\n'
                        'if (c->cmd == 0x51) { found++; assert(c->data_bytes == 1);\n'
                        f'assert(((const uint8_t *)c->data)[0] == {expected_register}); }}\n'
                        'if (c->cmd == 0x3A) { formats++; assert(c->data_bytes == 1);\n'
                        'assert(((const uint8_t *)c->data)[0] == Q2_AM178_COLMOD); } }\n'
                        'assert(formats == 1);\n'
                        '#if Q2_PANEL_AM213\n'
                        'const uint8_t commands[] = {0xfe,0x11,0xfe,0xc4,0x3a,0x53,0x63,0x2a,0x2b,0x29,0x51,0x58};\n'
                        'const uint8_t sizes[] = {1,0,1,1,1,1,1,4,4,0,1,1};\n'
                        'const uint8_t payload[][4] = {{0},{0},{0},{0x80},{0x55},{0x20},{0xff},\n'
                        '{0,0,1,0xbf},{0,0,1,0x6f},{0},{0},{7}};\n'
                        'const unsigned delays[] = {0,120,0,0,0,0,0,0,0,60,0,10};\n'
                        'assert(sizeof(q2_am178_vendor_init_cmds)/sizeof(*q2_am178_vendor_init_cmds)==12);\n'
                        'for(unsigned i=0;i<12;i++) { const q2_panel_init_cmd_t *c=&q2_am178_vendor_init_cmds[i];\n'
                        'assert(c->cmd==commands[i] && c->data_bytes==sizes[i] && c->delay_ms==delays[i]);\n'
                        'if(c->cmd != 0x51) for(unsigned j=0;j<c->data_bytes;j++)\n'
                        'assert(((const uint8_t*)c->data)[j]==payload[i][j]); }\n'
                        '#endif\n'
                        f'assert(Q2_AM178_COLMOD == {0x55});\n'
                        f'assert(Q2_AM178_PIXEL_BITS == {16});\n'
                        f'assert(found == 1); assert(Q2_AM178_INITIAL_BRIGHTNESS == {percent}); return 0; }}\n'
                    )
                    cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{root}", f"-I{MAIN}"]
                    if am213 and not product:
                        cmd.append("-DCONFIG_Q2_PANEL_AM213=1")
                    if product:
                        cmd.append("-DQ2_USE_PROJECT_CONFIG=1")
                    subprocess.run(cmd + [str(root / "test.c"), "-o", str(root / "test")], check=True)
                    subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
