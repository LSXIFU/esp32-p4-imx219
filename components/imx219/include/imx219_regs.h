#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C address (7-bit) */
#define IMX219_I2C_ADDR             (0x10)

/* Chip ID */
#define IMX219_REG_CHIP_ID          (0x0000)
#define IMX219_CHIP_ID              (0x0219)

/* Mode select */
#define IMX219_REG_MODE_SELECT      (0x0100)
#define IMX219_MODE_STANDBY         (0x00)
#define IMX219_MODE_STREAMING       (0x01)

/* CSI lane mode */
#define IMX219_REG_CSI_LANE_MODE    (0x0114)
#define IMX219_CSI_2_LANE           (0x01)

/* DPHY control */
#define IMX219_REG_DPHY_CTRL        (0x0128)
#define IMX219_DPHY_TIMING_AUTO     (0x00)

/* External clock frequency */
#define IMX219_REG_EXCK_FREQ        (0x012a)

/* Analog gain */
#define IMX219_REG_ANALOG_GAIN      (0x0157)

/* Digital gain (16-bit) */
#define IMX219_REG_DIGITAL_GAIN     (0x0158)

/* Exposure (16-bit) */
#define IMX219_REG_EXPOSURE         (0x015a)

/* Frame length (16-bit) */
#define IMX219_REG_FRM_LENGTH_A     (0x0160)

/* Line length (16-bit) */
#define IMX219_REG_LINE_LENGTH_A    (0x0162)

/* Crop window (all 16-bit) */
#define IMX219_REG_X_ADD_STA_A      (0x0164)
#define IMX219_REG_X_ADD_END_A      (0x0166)
#define IMX219_REG_Y_ADD_STA_A      (0x0168)
#define IMX219_REG_Y_ADD_END_A      (0x016a)

/* Output size (16-bit) */
#define IMX219_REG_X_OUTPUT_SIZE    (0x016c)
#define IMX219_REG_Y_OUTPUT_SIZE    (0x016e)

/* Odd increment */
#define IMX219_REG_X_ODD_INC_A      (0x0170)
#define IMX219_REG_Y_ODD_INC_A      (0x0171)

/* Binning */
#define IMX219_REG_BINNING_MODE_H   (0x0174)
#define IMX219_REG_BINNING_MODE_V   (0x0175)
#define IMX219_BINNING_X2_ANALOG    (0x03)

/* Orientation / flip control (8-bit) */
#define IMX219_REG_ORIENTATION      (0x0172)
#define IMX219_ORIENT_NORMAL        0x00    /* No flip */
#define IMX219_ORIENT_HFLIP         0x01    /* Horizontal mirror */
#define IMX219_ORIENT_VFLIP         0x02    /* Vertical flip */
#define IMX219_ORIENT_180           0x03    /* H + V = 180° rotation */

/* CSI data format (16-bit) */
#define IMX219_REG_CSI_DATA_FORMAT_A (0x018c)

/* PLL */
#define IMX219_REG_VTPXCK_DIV       (0x0301)
#define IMX219_REG_VTSYCK_DIV       (0x0303)
#define IMX219_REG_PREPLLCK_VT_DIV  (0x0304)
#define IMX219_REG_PREPLLCK_OP_DIV  (0x0305)
#define IMX219_REG_PLL_VT_MPY       (0x0306)
#define IMX219_REG_OPPXCK_DIV       (0x0309)
#define IMX219_REG_OPSYCK_DIV       (0x030b)
#define IMX219_REG_PLL_OP_MPY       (0x030c)

/* Test pattern (16-bit) */
#define IMX219_REG_TEST_PATTERN     (0x0600)
#define IMX219_TEST_PATTERN_COLOR_BARS  (2)

/* XCLK */
#define IMX219_XCLK_FREQ            (24000000)

/* PoC verified register table: 1536x1232 RAW10 2-lane 30fps */
typedef struct {
    uint16_t reg;
    uint8_t val;
} imx219_reg_t;

static const imx219_reg_t imx219_init_1536x1232_30fps[] = {
    {0x0100, 0x00}, // Standby
    {0x30EB, 0x05}, {0x30EB, 0x0C}, {0x300A, 0xFF}, {0x300B, 0xFF},
    {0x30EB, 0x05}, {0x30EB, 0x09},
    {0x0301, 0x05}, {0x0303, 0x01}, {0x0304, 0x03}, {0x0305, 0x03},
    {0x0306, 0x00}, {0x0307, 0x39},
    {0x0309, 0x0A}, {0x030B, 0x01},
    {0x030C, 0x00}, {0x030D, 0x72},
    {0x0114, 0x01},
    {0x0128, 0x01},
    {0x012A, 0x18}, {0x012B, 0x00},
    {0x0160, 0x06}, {0x0161, 0xE3},
    {0x0162, 0x0D}, {0x0163, 0x78},
    {0x0164, 0x00}, {0x0165, 0x00},
    {0x0166, 0x0C}, {0x0167, 0xCF},
    {0x0168, 0x00}, {0x0169, 0x00},
    {0x016A, 0x09}, {0x016B, 0x9F},
    {0x016C, 0x06}, {0x016D, 0x00},
    {0x016E, 0x04}, {0x016F, 0xD0},
    {0x0174, 0x03}, {0x0175, 0x03},
    {0x018C, 0x0A}, {0x018D, 0x0A},
    {0x0157, 0xE8},
    {0x015A, 0x06}, {0x015B, 0xD6},
};

#ifdef __cplusplus
}
#endif
