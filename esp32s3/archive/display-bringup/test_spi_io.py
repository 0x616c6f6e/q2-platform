"""Host contract test for the actual single-transaction transport, with an instrumented SPI stub."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_SUPPORTED 3
#define SPI_TRANS_MODE_QIO 0x10
#define SPI_DEVICE_HALFDUPLEX 0x20
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define IRAM_ATTR
#define portYIELD_FROM_ISR() (++yields)
typedef int esp_err_t;
typedef int esp_lcd_spi_bus_handle_t;
typedef int spi_host_device_t;
typedef void *spi_device_handle_t;
typedef struct esp_lcd_panel_io_t esp_lcd_panel_io_t;
typedef esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef struct {int unused;} esp_lcd_panel_io_event_data_t;
typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t *,void *);
typedef struct {esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;} esp_lcd_panel_io_callbacks_t;
struct esp_lcd_panel_io_t {
 int (*rx_param)(esp_lcd_panel_io_t *,int,void *,size_t);
 int (*tx_param)(esp_lcd_panel_io_t *,int,const void *,size_t);
 int (*tx_color)(esp_lcd_panel_io_t *,int,const void *,size_t);
 int (*del)(esp_lcd_panel_io_t *);
 int (*register_event_callbacks)(esp_lcd_panel_io_t *,const esp_lcd_panel_io_callbacks_t *,void *);
};
typedef struct {
 int cs_gpio_num,dc_gpio_num,spi_mode,pclk_hz,lcd_cmd_bits,lcd_param_bits;
 unsigned cs_ena_pretrans,cs_ena_posttrans;
 struct {bool quad_mode,octal_mode,sio_mode,lsb_first,cs_high_active,psram_dma_direct;} flags;
 esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;void *user_ctx;
} esp_lcd_panel_io_spi_config_t;
typedef struct {uint16_t cmd;uint64_t addr;size_t length;const void *tx_buffer;unsigned flags;void *user;} spi_transaction_t;
typedef struct {
 unsigned command_bits,address_bits,dummy_bits,mode,clock_speed_hz,spics_io_num,queue_size,flags;
 unsigned cs_ena_pretrans,cs_ena_posttrans;void (*post_cb)(spi_transaction_t *);
} spi_device_interface_config_t;
extern unsigned yields;
void *heap_caps_calloc(size_t,size_t,unsigned);
int spi_bus_get_max_transaction_len(int,size_t *);
int spi_bus_add_device(int,const spi_device_interface_config_t *,void **);
int spi_bus_remove_device(void *);
int spi_device_transmit(void *,spi_transaction_t *);
"""
TEST = r"""
#include "qspi_single_io.h"
#include <stdio.h>
unsigned yields;
static unsigned transfers,notices;
static unsigned live_devices, removals;
static int fail_alloc,fail_get,fail_add,fail_send,fail_remove;
static bool in_isr;
static spi_device_interface_config_t device;
static spi_transaction_t captured;
static uint8_t sent[12000];
static void *expected_ctx;
void *heap_caps_calloc(size_t n,size_t s,unsigned flags) {
 assert(flags==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));return fail_alloc?NULL:calloc(n,s);
}
int spi_bus_get_max_transaction_len(int host,size_t *out) {assert(host==1);
 if (!live_devices) return ESP_ERR_INVALID_ARG;
 *out=12000;return fail_get;}
int spi_bus_add_device(int host,const spi_device_interface_config_t *cfg,void **out) {
 assert(host==1);
 if(fail_add)return fail_add;
 assert(!live_devices);live_devices++;device=*cfg;*out=&device;return 0;
}
int spi_bus_remove_device(void *dev) {assert(dev==&device && live_devices==1);
 if(fail_remove)return fail_remove;
 live_devices--;removals++;return 0;}
int spi_device_transmit(void *dev,spi_transaction_t *t) {
 assert(dev==&device);transfers++;captured=*t;
 assert(!(t->length%8));assert(t->length/8<=sizeof(sent));
 if(t->length) memcpy(sent,t->tx_buffer,t->length/8);
 if(fail_send)return fail_send;
 in_isr=true;device.post_cb(t);in_isr=false;return 0;
}
static bool done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *ctx) {
 assert(in_isr && io && !event && ctx==expected_ctx);notices++;return true;
}
int main(void) {
 esp_lcd_panel_io_spi_config_t c={.cs_gpio_num=16,.dc_gpio_num=-1,.spi_mode=0,.pclk_hz=10000000,
   .lcd_cmd_bits=32,.lcd_param_bits=8,.flags.quad_mode=true,.on_color_trans_done=done};
 expected_ctx=&c;c.user_ctx=&c;
 esp_lcd_panel_io_handle_t io=(void *)1;
 assert(esp_lcd_new_panel_io_qspi_single(1,NULL,&io)==ESP_ERR_INVALID_ARG && !io);
 fail_alloc=1;assert(esp_lcd_new_panel_io_qspi_single(1,&c,&io)==ESP_ERR_NO_MEM && !io);fail_alloc=0;
 fail_get=42;assert(esp_lcd_new_panel_io_qspi_single(1,&c,&io)==42 && !io);fail_get=0;
 assert(live_devices==0 && removals==1);
 fail_add=43;assert(esp_lcd_new_panel_io_qspi_single(1,&c,&io)==43 && !io);fail_add=0;
 assert(live_devices==0 && removals==1);
 c.flags.lsb_first=true;assert(esp_lcd_new_panel_io_qspi_single(1,&c,&io)==ESP_ERR_INVALID_ARG);c.flags.lsb_first=false;
 assert(esp_lcd_new_panel_io_qspi_single(1,&c,&io)==0);
 assert(device.command_bits==8 && device.address_bits==24 && device.dummy_bits==0);
 assert(device.flags==SPI_DEVICE_HALFDUPLEX && device.queue_size==1 && device.mode==0);
 assert(io->tx_param(io,0x02001100,NULL,0)==0);
 assert(captured.cmd==2 && captured.addr==0x1100 && captured.length==0 && !captured.flags && !notices);
 uint8_t window[]={0,0xb0,0,0xdf};
 assert(io->tx_param(io,0x02002a00,window,4)==0);
 assert(captured.cmd==2 && captured.addr==0x2a00 && captured.length==32 && !captured.flags);
 assert(memcmp(window,sent,4)==0 && !notices);
 uint8_t rgb[1440];
 for(unsigned level=0;level<256;level++) {
  for(unsigned i=0;i<sizeof(rgb);i++)rgb[i]=(uint8_t)((i%3)==0?level:((i%3)==1?0:0xf8));
  unsigned before=transfers;
  assert(io->tx_color(io,0x32002c00,rgb,sizeof(rgb))==0 && transfers==before+1);
  assert(captured.cmd==0x32 && captured.addr==0x2c00 && captured.length==sizeof(rgb)*8);
  assert(captured.flags==SPI_TRANS_MODE_QIO && memcmp(rgb,sent,sizeof(rgb))==0);
 }
 assert(notices==256 && yields==256);
 unsigned before=transfers;
 assert(io->tx_color(io,0x32002c00,NULL,1)==ESP_ERR_INVALID_ARG);
 assert(io->tx_color(io,0x32002c00,rgb,12001)==ESP_ERR_INVALID_ARG);
 assert(io->tx_color(io,-1,rgb,1)==ESP_ERR_INVALID_ARG);
 assert(io->tx_color(io,0x32012c00,rgb,1)==ESP_ERR_INVALID_ARG);
 assert(io->tx_param(io,0x32002c00,rgb,1)==ESP_ERR_INVALID_ARG && before==transfers);
 fail_send=99;assert(io->tx_color(io,0x32002c00,rgb,12)==99);fail_send=0;
 assert(notices==256);
 assert(io->rx_param(io,0,NULL,0)==ESP_ERR_NOT_SUPPORTED);
 assert(io->register_event_callbacks(io,NULL,NULL)==ESP_ERR_INVALID_ARG);
 esp_lcd_panel_io_callbacks_t cb={0};assert(io->register_event_callbacks(io,&cb,NULL)==0);
 assert(io->tx_color(io,0x32002c00,rgb,12)==0 && notices==256);
 fail_remove=77;assert(io->del(io)==77);fail_remove=0;assert(io->del(io)==0);
 assert(live_devices==0 && removals==2);
 puts("PASS: lazy SPI initialization order and failed-create cleanup, cmd8/addr24/single QIO transaction, 256 byte levels, unchanged payload, ISR callbacks/yield, errors, bounds and cleanup");
}
"""
with tempfile.TemporaryDirectory(prefix="qspi-single-test-") as folder:
    tmp = Path(folder)
    (tmp / "stubs.h").write_text(STUBS)
    for name in ["esp_lcd_io_spi.h", "esp_lcd_panel_io_interface.h", "esp_heap_caps.h",
                 "esp_attr.h", "freertos/FreeRTOS.h", "freertos/task.h"]:
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("")
    (tmp / "test.c").write_text(TEST)
    component = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "components/qspi_single_io"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-include", str(tmp / "stubs.h"), "-I" + str(tmp), "-I" + str(component / "include"),
                    str(component / "qspi_single_io.c"), str(tmp / "test.c"), "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test")], check=True)
