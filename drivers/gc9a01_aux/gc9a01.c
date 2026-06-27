// Based on the gc9a01 library by Nadyrshin Ruslan / Liyanboy74.
//
// Copyright (c) 2021, liyanboy74
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <stdint.h>
#include <string.h>

#include "py/mphal.h"

#include "gc9a01.h"

#define Cmd_SLPOUT 0x11
#define Cmd_INVON 0x21
#define Cmd_DISPON 0x29
#define Cmd_CASET 0x2A
#define Cmd_RASET 0x2B
#define Cmd_RAMWR 0x2C   // Frame memory write
#define Cmd_TEON 0x35    // Tearing effect line ON
#define Cmd_MADCTL 0x36  // Memory data access control
#define Cmd_COLMOD 0x3A  // Pixel format set

#define Cmd_DisplayFunctionControl 0xB6
#define Cmd_PWCTR1 0xC1  // Power control 1
#define Cmd_PWCTR2 0xC3  // Power control 2
#define Cmd_PWCTR3 0xC4  // Power control 3
#define Cmd_PWCTR4 0xC9  // Power control 4

#define Cmd_FRAMERATE 0xE8

#define Cmd_GAMMA1 0xF0  // Set gamma 1
#define Cmd_GAMMA2 0xF1  // Set gamma 2
#define Cmd_GAMMA3 0xF2  // Set gamma 3
#define Cmd_GAMMA4 0xF3  // Set gamma 4

#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_BGR 0x08

// A full-framebuffer 'blit' operation descriptor. Keeps track of number of
// underlying DMA SPI transactions left until blit is done.
typedef struct {
    flow3r_bsp_gc9a01_t *gc9a01;
    const uint8_t *fb;
    size_t left; // bytes left to blit
    size_t off;  // current pixel offset in blit
} flow3r_bsp_gc9a01_blit_t;

/*
 The LCD needs a bunch of command/argument values to be initialized. They are
 stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes;  // No of data in data; bit 7 = delay after set; 0xFF =
                        // end of cmds.
} flow3r_bsp_gc9a01_init_cmd_t;

static const flow3r_bsp_gc9a01_init_cmd_t flow3r_bsp_gc9a01_init_cmds[] = {
    { 0xef, { 0 }, 0 },
    { 0xeb, { 0x14 }, 1 },
    { 0xfe, { 0 }, 0 },
    { 0xef, { 0 }, 0 },
    { 0xeb, { 0x14 }, 1 },
    { 0x84, { 0x40 }, 1 },
    { 0x85, { 0xff }, 1 },
    { 0x86, { 0xff }, 1 },
    { 0x87, { 0xff }, 1 },
    { 0x88, { 0x0a }, 1 },
    { 0x89, { 0x21 }, 1 },
    { 0x8a, { 0x00 }, 1 },
    { 0x8b, { 0x80 }, 1 },
    { 0x8c, { 0x01 }, 1 },
    { 0x8d, { 0x01 }, 1 },
    { 0x8e, { 0xff }, 1 },
    { 0x8f, { 0xff }, 1 },
    { Cmd_DisplayFunctionControl,
      { 0x00, 0x20 }, 2 },  // Scan direction S360 -> S1
    { 0x90, { 0x08, 0x08, 0x08, 0x08 }, 4 },
    { 0xbd, { 0x06 }, 1 },
    { 0xbc, { 0x00 }, 1 },
    { 0xff, { 0x60, 0x01, 0x04 }, 3 },
    { Cmd_PWCTR2, { 0x13 }, 1 },
    { Cmd_PWCTR3, { 0x13 }, 1 },
    { Cmd_PWCTR4, { 0x22 }, 1 },
    { 0xbe, { 0x11 }, 1 },
    { 0xe1, { 0x10, 0x0e }, 2 },
    { 0xdf, { 0x21, 0x0c, 0x02 }, 3 },
    { Cmd_GAMMA1, { 0x45, 0x09, 0x08, 0x08, 0x26, 0x2a }, 6 },
    { Cmd_GAMMA2, { 0x43, 0x70, 0x72, 0x36, 0x37, 0x6f }, 6 },
    { Cmd_GAMMA3, { 0x45, 0x09, 0x08, 0x08, 0x26, 0x2a }, 6 },
    { Cmd_GAMMA4, { 0x43, 0x70, 0x72, 0x36, 0x37, 0x6f }, 6 },
    { 0xed, { 0x1b, 0x0b }, 2 },
    { 0xae, { 0x77 }, 1 },
    { 0xcd, { 0x63 }, 1 },
    { 0x70, { 0x07, 0x07, 0x04, 0x0e, 0x0f, 0x09, 0x07, 0x08, 0x03 }, 9 },
    { Cmd_FRAMERATE, { 0x34 }, 1 },  // 4 dot inversion
    { 0x62,
      { 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70 },
      12 },
    { 0x63,
      { 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70 },
      12 },
    { 0x64, { 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07 }, 7 },
    { 0x66, { 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00 }, 10 },
    { 0x67, { 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98 }, 10 },
    { 0x74, { 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00 }, 7 },
    { 0x98, { 0x3e, 0x07 }, 2 },
    { Cmd_TEON, { 0 }, 0 },  // Tearing effect line
    { Cmd_MADCTL, { MADCTL_MX | MADCTL_MY | MADCTL_BGR }, 1 },  // Memory access control
    { Cmd_COLMOD, { 0x05 }, 1 },  // 16bit colour MCU interface
    { Cmd_INVON, { 0 }, 0 },  // Inversion mode
    { Cmd_SLPOUT, { 0 }, 0x80 },
    { Cmd_DISPON, { 0 }, 0x80 },
    { 0, { 0 }, 0xff },      // END
};

/* Send a command to the LCD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
static esp_err_t flow3r_bsp_gc9a01_cmd_sync(flow3r_bsp_gc9a01_t *gc9a01, uint8_t cmd) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    t.length = 8;
    t.tx_buffer = &cmd;

    // Low for commands
    mp_hal_pin_write(gc9a01->config->pin_dc, 0);
    esp_err_t res = spi_device_polling_transmit(gc9a01->spi, &t);
    return res;
}

/* Send data to the LCD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
static esp_err_t flow3r_bsp_gc9a01_data_sync(flow3r_bsp_gc9a01_t *gc9a01,
                                             const uint8_t *data, int len) {
    if (len == 0) {
        return ESP_OK;
    }

    // High for data
    mp_hal_pin_write(gc9a01->config->pin_dc, 1);

    /*
    On certain MC's the max SPI DMA transfer length might be smaller than the
    buffer. We then have to split the transmissions.
    */
    int offset = 0;
    do {
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));  // Zero out the transaction

        int tx_len = ((len - offset) < SPI_MAX_DMA_LEN) ? (len - offset)
                                                        : SPI_MAX_DMA_LEN;
        // Len is in bytes, transaction length is in bits.
        t.length = tx_len * 8;
        t.tx_buffer = data + offset;

        // Transmit!
        esp_err_t ret = spi_device_polling_transmit(gc9a01->spi, &t);
        if (ret != ESP_OK) {
            return ret;
        }
        offset += tx_len;
    } while (offset < len);

    return ESP_OK;
}

static esp_err_t flow3r_bsp_gc9a01_data_byte_sync(flow3r_bsp_gc9a01_t *gc9a01,
                                                  const uint8_t data) {
    return flow3r_bsp_gc9a01_data_sync(gc9a01, &data, 1);
}

static esp_err_t flow3r_bsp_gc9a01_column_set(flow3r_bsp_gc9a01_t *gc9a01,
                                              uint16_t start, uint16_t end) {
    esp_err_t ret;
    ret = flow3r_bsp_gc9a01_cmd_sync(gc9a01, Cmd_CASET);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = flow3r_bsp_gc9a01_data_byte_sync(gc9a01, start >> 8);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = flow3r_bsp_gc9a01_data_byte_sync(gc9a01, start & 0xFF);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = flow3r_bsp_gc9a01_data_byte_sync(gc9a01, end >> 8);
    if (ret != ESP_OK) {
        return ret;
    }
    return flow3r_bsp_gc9a01_data_byte_sync(gc9a01, end & 0xFF);
}

static esp_err_t flow3r_bsp_gc9a01_row_set(flow3r_bsp_gc9a01_t *gc9a01,
                                           uint16_t start, uint16_t end) {
    esp_err_t ret;
    ret = flow3r_bsp_gc9a01_cmd_sync(gc9a01, Cmd_RASET);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = flow3r_bsp_gc9a01_data_byte_sync(gc9a01, start >> 8);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = flow3r_bsp_gc9a01_data_byte_sync(gc9a01, start & 0xFF);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = flow3r_bsp_gc9a01_data_byte_sync(gc9a01, end >> 8);
    if (ret != ESP_OK) {
        return ret;
    }
    return flow3r_bsp_gc9a01_data_byte_sync(gc9a01, end & 0xFF);
}

esp_err_t gc9a01_init(flow3r_bsp_gc9a01_t *gc9a01,
                                 flow3r_bsp_gc9a01_config_t *config) {
    memset(gc9a01, 0, sizeof(flow3r_bsp_gc9a01_t));
    gc9a01->config = config;

    // Configure DC pin.
    gpio_config_t gpiocfg = {
        .pin_bit_mask = ((uint64_t)1UL << gc9a01->config->pin_dc),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t res = gpio_config(&gpiocfg);
    if (res != ESP_OK) {
        return res;
    }
    mp_hal_pin_write(gc9a01->config->pin_dc, 0);

    // Configure SPI bus.
    spi_bus_config_t buscfg = {
        .mosi_io_num = gc9a01->config->pin_mosi,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = gc9a01->config->pin_sck,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 250 * 250 * 2,
    };

    // Configure SPI device on bus.
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 80 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = gc9a01->config->pin_cs,
        .queue_size = 7,
    };

    esp_err_t ret =
        spi_bus_initialize(gc9a01->config->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = spi_bus_add_device(gc9a01->config->host, &devcfg, &gc9a01->spi);
    if (ret != ESP_OK) {
        goto cleanup_spi_bus;
    }

    // Send initialization commands.
    int ix = 0;
    while (flow3r_bsp_gc9a01_init_cmds[ix].databytes != 0xff) {
        const flow3r_bsp_gc9a01_init_cmd_t *cmd =
            &flow3r_bsp_gc9a01_init_cmds[ix];
        flow3r_bsp_gc9a01_cmd_sync(gc9a01, cmd->cmd);
        flow3r_bsp_gc9a01_data_sync(gc9a01, cmd->data, cmd->databytes & 0x1F);
        if (cmd->databytes & 0x80) {
            mp_hal_delay_ms(150);
        }
        ix++;
    }

    // We always write the entire framebuffer at once.
    ret = flow3r_bsp_gc9a01_column_set(gc9a01, 0, 239);
    if (ret != ESP_OK) {
        goto cleanup_spi_device;
    }
    ret = flow3r_bsp_gc9a01_row_set(gc9a01, 0, 239);
    if (ret != ESP_OK) {
        goto cleanup_spi_device;
    }

    return ret;

cleanup_spi_device:
    spi_bus_remove_device(gc9a01->spi);
cleanup_spi_bus:
    spi_bus_free(gc9a01->config->host);
    return ret;
}

esp_err_t gc9a01_deinit(flow3r_bsp_gc9a01_t *gc9a01) {
    // Might fail if the device was already freed or was never added,
    // but in those cases we don't care and can continue to free the bus
    spi_bus_remove_device(gc9a01->spi);

    return spi_bus_free(gc9a01->config->host);
}

static inline esp_err_t gc9a01_blit_next(
    flow3r_bsp_gc9a01_blit_t *blit) {
    size_t size = blit->left;
    if (size > SPI_MAX_DMA_LEN) {
        size = SPI_MAX_DMA_LEN;
    }
    const uint8_t *buffer = &blit->fb[blit->off * 2];
    blit->off += size / 2;
    blit->left -= size;
    return flow3r_bsp_gc9a01_data_sync(blit->gc9a01, buffer, size);
}

static esp_err_t gc9a01_blit_start(flow3r_bsp_gc9a01_t *gc9a01,
                                              flow3r_bsp_gc9a01_blit_t *blit,
                                              const uint16_t *fb) {
    memset(blit, 0, sizeof(flow3r_bsp_gc9a01_blit_t));
    blit->gc9a01 = gc9a01;
    blit->fb = (const uint8_t *)fb;
    blit->left = 2 * 240 * 240;  // left in native bytes (16bpp)

    return flow3r_bsp_gc9a01_cmd_sync(gc9a01, Cmd_RAMWR);
}

esp_err_t gc9a01_blit(flow3r_bsp_gc9a01_t *gc9a01, const void *fb) {
    flow3r_bsp_gc9a01_blit_t blit;
    esp_err_t res = gc9a01_blit_start(
        gc9a01, &blit, fb);
    if (res != ESP_OK) {
        return res;
    }

    while (blit.left != 0) {
        res = gc9a01_blit_next(&blit);
        if (res != ESP_OK) {
            return res;
        }
    }
    return ESP_OK;
}
