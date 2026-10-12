"""Compile the actual vendor command/bitmap functions against fault-injecting IO stubs."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
PRE='\n#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stddef.h>\n#include <string.h>\n#define ESP_OK 0\n#define LCD_CMD_CASET 0x2a\n#define LCD_CMD_RASET 0x2b\n#define LCD_CMD_RAMWR 0x2c\n#define LCD_OPCODE_WRITE_CMD 0x02ULL\n#define LCD_OPCODE_WRITE_COLOR 0x32ULL\n#define __containerof(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))\n#define ESP_RETURN_ON_ERROR(expr,...) do {int r=(expr);if(r)return r;}while(0)\ntypedef int esp_err_t;\ntypedef void *esp_lcd_panel_io_handle_t;\ntypedef struct {int unused;} esp_lcd_panel_t;\ntypedef struct {esp_lcd_panel_t base;void *io;int x_gap,y_gap;uint8_t fb_bits_per_pixel;struct {unsigned use_qspi_interface:1;} flags;} qspi_amoled_panel_t;\nstatic unsigned command[3],count;\nstatic uint8_t params[2][4];\nstatic size_t sent_size;\nstatic int fail_color;\nstatic int esp_lcd_panel_io_tx_param(void *io,int cmd,const void *data,size_t len) {\n    (void)io;assert(count<2 && len==4);command[count]=cmd;memcpy(params[count],data,4);count++;return 0;\n}\nstatic int esp_lcd_panel_io_tx_color(void *io,int cmd,const void *data,size_t len) {\n    (void)io;(void)data;assert(count==2);command[count++]=cmd;sent_size=len;return fail_color;\n}\n\n#define ESP_LOGE(...) ((void)0)\n'
POST='int main(void) {\n    qspi_amoled_panel_t p={.x_gap=16,.fb_bits_per_pixel=24,.flags.use_qspi_interface=1};\n    uint8_t pixel[1440]={0};\n    assert(panel_qspi_amoled_draw_bitmap(&p.base,160,200,208,210,pixel)==0);\n    assert(count==3 && command[0]==0x02002a00 && command[1]==0x02002b00 && command[2]==0x32002c00);\n    const uint8_t x[]={0,0xb0,0,0xdf}, y[]={0,0xc8,0,0xd1};\n    assert(memcmp(x,params[0],4)==0 && memcmp(y,params[1],4)==0 && sent_size==1440);\n    count=0;fail_color=0x103;\n    assert(panel_qspi_amoled_draw_bitmap(&p.base,160,200,208,210,pixel)==0x103);\n    puts("PASS vendor commands/windows/1440-byte payload; injected tx_color error 0x103 propagates to caller");\n}\n'
source=(ROOT/'components/osptek_qspi_amoled/esp_lcd_qspi_amoled.c').read_text()
functions=[]
for name in ['tx_param','tx_color','panel_qspi_amoled_draw_bitmap']:
    matches=list(re.finditer(r'static esp_err_t '+name+r'\([^;]*?\)\n\{.*?\n\}',source,re.S))
    assert len(matches)==1,name
    functions.append(matches[0].group())
with tempfile.TemporaryDirectory(prefix='vendor-error-test-') as folder:
    p=Path(folder)
    (p/'test.c').write_text(PRE+'\n'.join(functions)+POST)
    subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
