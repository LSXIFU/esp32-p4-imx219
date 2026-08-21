/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * IMX219 initialization register settings for supported modes.
 * Register values derived from Linux kernel driver (drivers/media/i2c/imx219.c).
 */

#pragma once

#include "imx219_regs.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Register entry: {reg_addr, value, delay_ms} */
typedef struct {
    uint16_t reg;
    uint8_t val;
    uint16_t delay_ms;
} imx219_reginfo_t;

/*
 * 640x480 RAW8 2-lane 60fps mode (2x2 analog binning)
 *
 * Derived from Linux driver:
 *   - imx219_common_regs[] common registers
 *   - imx219_2lane_regs[] 2-lane PLL/CSI config
 *   - imx219_set_framefmt(): 640x480 crop from 1280x960 @2x2 analog bin
 *   - FLL = 1707, LLP = 3560 (binned minimum)
 *
 * Crop window calculation:
 *   bin_h = min(3280/640, 2) = 2
 *   bin_v = min(2464/480, 2) = 2
 *   crop_w = 640 * 2 = 1280
 *   crop_h = 480 * 2 = 960
 *   x_start = (3296 - 1280) / 2 - 8 = 1000
 *   x_end   = 1000 + 1280 - 1 = 2279
 *   y_start = (2480 - 960) / 2 - 8 = 752
 *   y_end   = 752 + 960 - 1 = 1711
 */
static const imx219_reginfo_t imx219_init_MIPI_2lane_24Minput_RAW8_640x480_60fps[] = {
    /* === Common registers (from imx219_common_regs[]) === */
    /* Put sensor in standby first */
    {IMX219_REG_MODE_SELECT,     IMX219_MODE_STANDBY,        10},

    /* Access high register space (0x3000-0x5fff) */
    {0x30eb,                    0x05,                         0},
    {0x30eb,                    0x0c,                         0},
    {0x300a,                    0xff,                         0},
    {0x300b,                    0xff,                         0},
    {0x30eb,                    0x05,                         0},
    {0x30eb,                    0x09,                         0},

    /* Undocumented registers from Linux driver */
    // Removed — PoC doesn't use these and produces MIPI data
    //{0x455e,                    0x00,                         0},

    /* Frame Bank Group A - odd increment = 1 (no skipping) */
    {IMX219_REG_X_ODD_INC_A,    0x01,                         0},
    {IMX219_REG_Y_ODD_INC_A,    0x01,                         0},

    /* DPHY auto timing */
    {IMX219_REG_DPHY_CTRL,      0x01,                         0},  /* Continuous clock mode */

    /* EXCK frequency: 24MHz -> 24 * 256 = 6144 = 0x1800 (16-bit reg) */
    {IMX219_REG_EXCK_FREQ,      0x18,                         0},
    {IMX219_REG_EXCK_FREQ + 1,  0x00,                         0},

    /* === 2-lane PLL registers (from imx219_2lane_regs[]) === */
    {IMX219_REG_VTPXCK_DIV,     0x05,                         0},
    {IMX219_REG_VTSYCK_DIV,     0x01,                         0},
    {IMX219_REG_PREPLLCK_VT_DIV,0x03,                         0},  /* AUTO set */
    {IMX219_REG_PREPLLCK_OP_DIV,0x03,                         0},  /* AUTO set */
    /* PLL_VT_MPY = 57 = 0x0039 */
    {IMX219_REG_PLL_VT_MPY,     0x00,                         0},
    {IMX219_REG_PLL_VT_MPY + 1, 0x39,                         0},
    {IMX219_REG_OPSYCK_DIV,     0x01,                         0},
    /* PLL_OP_MPY = 114 = 0x0072 */
    {IMX219_REG_PLL_OP_MPY,     0x00,                         0},
    {IMX219_REG_PLL_OP_MPY + 1, 0x72,                         0},

    /* CSI lane mode - 2-lane */
    {IMX219_REG_CSI_LANE_MODE,  IMX219_CSI_2_LANE,           0},

    /* === Format-specific registers (640x480 RAW8 60fps) === */

    /* X crop start = 1008 = 0x03F0 */
    {IMX219_REG_X_ADD_STA_A,    0x03,                         0},
    {IMX219_REG_X_ADD_STA_A + 1,0xF0,                         0},
    /* X crop end = 2287 = 0x08EF */
    {IMX219_REG_X_ADD_END_A,    0x08,                         0},
    {IMX219_REG_X_ADD_END_A + 1,0xEF,                         0},
    /* Y crop start = 760 = 0x02F8 */
    {IMX219_REG_Y_ADD_STA_A,    0x02,                         0},
    {IMX219_REG_Y_ADD_STA_A + 1,0xF8,                         0},
    /* Y crop end = 1719 = 0x06B7 */
    {IMX219_REG_Y_ADD_END_A,    0x06,                         0},
    {IMX219_REG_Y_ADD_END_A + 1,0xB7,                         0},

    /* 2x2 analog binning */
    {IMX219_REG_BINNING_MODE_H, IMX219_BINNING_X2_ANALOG,     0},
    {IMX219_REG_BINNING_MODE_V, IMX219_BINNING_X2_ANALOG,     0},

    /* Output size: 640 x 480 */
    {IMX219_REG_X_OUTPUT_SIZE,  0x02,                         0},
    {IMX219_REG_X_OUTPUT_SIZE + 1, 0x80,                     0},
    {IMX219_REG_Y_OUTPUT_SIZE,  0x01,                         0},
    {IMX219_REG_Y_OUTPUT_SIZE + 1, 0xE0,                     0},

    /* CSI data format: RAW8 -> bpp=8, value=(8<<8)|8 = 0x0808 */
    {IMX219_REG_CSI_DATA_FORMAT_A,    0x08,                   0},
    {IMX219_REG_CSI_DATA_FORMAT_A + 1,0x08,                   0},

    /* OPPXCK_DIV = bpp = 8 */
    {IMX219_REG_OPPXCK_DIV,     0x08,                         0},

    /* Frame length (FLL) = 1707 = 0x06AB */
    {IMX219_REG_FRM_LENGTH_A,   0x06,                         0},
    {IMX219_REG_FRM_LENGTH_A + 1, 0xAB,                      0},

    /* Line length (LLP) = 3560 = 0x0DE8 (binned minimum) */
    {IMX219_REG_LINE_LENGTH_A,  0x0D,                         0},
    {IMX219_REG_LINE_LENGTH_A + 1, 0xE8,                      0},

    /* Default exposure & gain (like PoC) */
    {0x0157,                    0xE8,                         0},  // Analog gain
    {0x015A,                    0x06,                         0},  // Exposure high
    {0x015B,                    0xD6,                         0},  // Exposure low
};

/* 640x480 RAW8 test pattern mode — full init + color bars enable */
static const imx219_reginfo_t imx219_test_MIPI_2lane_24Minput_RAW8_640x480_60fps[] = {
    /* Full init from normal mode */
    {IMX219_REG_MODE_SELECT,     IMX219_MODE_STANDBY,        10},

    {0x30eb,                    0x05,                         0},
    {0x30eb,                    0x0c,                         0},
    {0x300a,                    0xff,                         0},
    {0x300b,                    0xff,                         0},
    {0x30eb,                    0x05,                         0},
    {0x30eb,                    0x09,                         0},
    {0x455e,                    0x00,                         0},
    {0x471e,                    0x4b,                         0},
    {0x4767,                    0x0f,                         0},
    {0x4750,                    0x14,                         0},
    {0x4540,                    0x00,                         0},
    {0x47b4,                    0x14,                         0},
    {0x4713,                    0x30,                         0},
    {0x478b,                    0x10,                         0},
    {0x478f,                    0x10,                         0},
    {0x4793,                    0x10,                         0},
    {0x4797,                    0x0e,                         0},
    {0x479b,                    0x0e,                         0},
    {IMX219_REG_X_ODD_INC_A,    0x01,                         0},
    {IMX219_REG_Y_ODD_INC_A,    0x01,                         0},
    {IMX219_REG_DPHY_CTRL,      0x01,                         0},  /* Continuous clock mode */
    {IMX219_REG_EXCK_FREQ,      0x18,                         0},
    {IMX219_REG_EXCK_FREQ + 1,  0x00,                         0},

    {IMX219_REG_VTPXCK_DIV,     0x05,                         0},
    {IMX219_REG_VTSYCK_DIV,     0x01,                         0},
    {IMX219_REG_PREPLLCK_VT_DIV,0x03,                         0},
    {IMX219_REG_PREPLLCK_OP_DIV,0x03,                         0},
    {IMX219_REG_PLL_VT_MPY,     0x00,                         0},
    {IMX219_REG_PLL_VT_MPY + 1, 0x39,                         0},
    {IMX219_REG_OPSYCK_DIV,     0x01,                         0},
    {IMX219_REG_PLL_OP_MPY,     0x00,                         0},
    {IMX219_REG_PLL_OP_MPY + 1, 0x72,                         0},
    {IMX219_REG_CSI_LANE_MODE,  IMX219_CSI_2_LANE,           0},

    /* Crop window */
    {IMX219_REG_X_ADD_STA_A,    0x03,                         0},
    {IMX219_REG_X_ADD_STA_A + 1,0xF0,                         0},
    {IMX219_REG_X_ADD_END_A,    0x08,                         0},
    {IMX219_REG_X_ADD_END_A + 1,0xEF,                         0},
    {IMX219_REG_Y_ADD_STA_A,    0x02,                         0},
    {IMX219_REG_Y_ADD_STA_A + 1,0xF8,                         0},
    {IMX219_REG_Y_ADD_END_A,    0x06,                         0},
    {IMX219_REG_Y_ADD_END_A + 1,0xB7,                         0},

    /* Binning */
    {IMX219_REG_BINNING_MODE_H, IMX219_BINNING_X2_ANALOG,     0},
    {IMX219_REG_BINNING_MODE_V, IMX219_BINNING_X2_ANALOG,     0},

    /* Output size */
    {IMX219_REG_X_OUTPUT_SIZE,  0x02,                         0},
    {IMX219_REG_X_OUTPUT_SIZE + 1, 0x80,                     0},
    {IMX219_REG_Y_OUTPUT_SIZE,  0x01,                         0},
    {IMX219_REG_Y_OUTPUT_SIZE + 1, 0xE0,                     0},

    /* CSI data format: RAW8 */
    {IMX219_REG_CSI_DATA_FORMAT_A,    0x08,                   0},
    {IMX219_REG_CSI_DATA_FORMAT_A + 1,0x08,                   0},
    {IMX219_REG_OPPXCK_DIV,     0x08,                         0},

    /* Timing */
    {IMX219_REG_FRM_LENGTH_A,   0x06,                         0},
    {IMX219_REG_FRM_LENGTH_A + 1, 0xAB,                      0},
    {IMX219_REG_LINE_LENGTH_A,  0x0D,                         0},
    {IMX219_REG_LINE_LENGTH_A + 1, 0xE8,                      0},

    /* Enable color bar test pattern (at the end, after all config) */
    {IMX219_REG_TEST_PATTERN,   0x00,                         0},
    {IMX219_REG_TEST_PATTERN + 1, IMX219_TEST_PATTERN_COLOR_BARS, 0},
};

/* Supported format descriptions */
typedef struct {
    const char *name;           /* Format name string */
    uint16_t width;             /* Output width */
    uint16_t height;            /* Output height */
    uint16_t fps;               /* Frames per second */
    uint8_t bpp;                /* Bits per pixel (8 = RAW8, 10 = RAW10) */
    const imx219_reginfo_t *regs;   /* Register init table */
    uint16_t regs_size;         /* Number of entries in regs */
} imx219_format_info_t;

static const imx219_format_info_t imx219_supported_formats[] = {
    {
        .name = "MIPI_2lane_24Minput_RAW8_640x480_60fps",
        .width = 640,
        .height = 480,
        .fps = 60,
        .bpp = 8,
        .regs = imx219_init_MIPI_2lane_24Minput_RAW8_640x480_60fps,
        .regs_size = ARRAY_SIZE(imx219_init_MIPI_2lane_24Minput_RAW8_640x480_60fps),
    },
    {
        .name = "MIPI_2lane_24Minput_RAW8_640x480_TP",
        .width = 640,
        .height = 480,
        .fps = 60,
        .bpp = 8,
        .regs = imx219_test_MIPI_2lane_24Minput_RAW8_640x480_60fps,
        .regs_size = ARRAY_SIZE(imx219_test_MIPI_2lane_24Minput_RAW8_640x480_60fps),
    },
};

#define IMX219_NUM_FORMATS ARRAY_SIZE(imx219_supported_formats)

#ifdef __cplusplus
}
#endif
