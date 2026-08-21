/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * IMX219 camera sensor driver for ESP32-P4.
 * Based on Linux kernel driver (drivers/media/i2c/imx219.c).
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "imx219.h"
#include "imx219_regs.h"
#include "imx219_settings.h"

static const char *TAG = "imx219";

/* Internal device structure */
struct imx219_device_t {
    i2c_master_dev_handle_t i2c_dev;   /* I2C device handle */
    const imx219_format_info_t *fmt;   /* Current format */
};

/* ======================================================
 * I2C register access primitives
 * ====================================================== */

static esp_err_t imx219_write_reg(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t val)
{
    uint8_t buf[3] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        val
    };
    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

static esp_err_t imx219_read_reg(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t *val)
{
    uint8_t addr[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF)
    };
    esp_err_t ret = i2c_master_transmit(dev, addr, sizeof(addr), -1);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_receive(dev, val, 1, -1);
}

static esp_err_t imx219_read_reg16(i2c_master_dev_handle_t dev, uint16_t reg, uint16_t *val)
{
    uint8_t addr[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF)
    };
    uint8_t buf[2];
    esp_err_t ret = i2c_master_transmit(dev, addr, sizeof(addr), -1);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_master_receive(dev, buf, sizeof(buf), -1);
    if (ret == ESP_OK) {
        *val = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return ret;
}

/* Write a register table to the sensor */
static esp_err_t imx219_write_regs(i2c_master_dev_handle_t dev,
                                   const imx219_reginfo_t *regs,
                                   uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        esp_err_t ret = imx219_write_reg(dev, regs[i].reg, regs[i].val);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reg(0x%04x)=0x%02x write failed: %s",
                     regs[i].reg, regs[i].val, esp_err_to_name(ret));
            return ret;
        }
        if (regs[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(regs[i].delay_ms));
        }
    }
    return ESP_OK;
}

/* ======================================================
 * Sensor detection
 * ====================================================== */

static esp_err_t imx219_detect(i2c_master_dev_handle_t dev)
{
    uint16_t chip_id = 0;
    esp_err_t ret = imx219_read_reg16(dev, IMX219_REG_CHIP_ID, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID (I2C error: %s)", esp_err_to_name(ret));
        return ret;
    }
    if (chip_id != IMX219_CHIP_ID) {
        ESP_LOGE(TAG, "Chip ID mismatch: read 0x%04x, expected 0x%04x",
                 chip_id, IMX219_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Detected IMX219 sensor, chip ID = 0x%04x", chip_id);
    return ESP_OK;
}

/* ======================================================
 * Find format by name
 * ====================================================== */

static const imx219_format_info_t *imx219_find_format(const char *name)
{
    for (int i = 0; i < IMX219_NUM_FORMATS; i++) {
        if (strcmp(imx219_supported_formats[i].name, name) == 0) {
            return &imx219_supported_formats[i];
        }
    }
    return NULL;
}

/* ======================================================
 * Public API
 * ====================================================== */

esp_err_t imx219_init(const imx219_config_t *config, imx219_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_handle, 0, sizeof(*out_handle));

    /* Find requested format */
    const imx219_format_info_t *fmt = imx219_find_format(config->format_name);
    if (fmt == NULL) {
        ESP_LOGE(TAG, "Unsupported format: %s", config->format_name);
        return ESP_ERR_INVALID_ARG;
    }

    /* Create I2C bus */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = config->i2c_port_num,
        .sda_io_num = config->i2c_sda_io_num,
        .scl_io_num = config->i2c_scl_io_num,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &out_handle->i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Add IMX219 device to the bus */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IMX219_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    ret = i2c_master_bus_add_device(out_handle->i2c_bus_handle, &dev_config, &out_handle->i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(out_handle->i2c_bus_handle);
        out_handle->i2c_bus_handle = NULL;
        return ret;
    }

    /* Detect sensor */
    ret = imx219_detect(out_handle->i2c_dev_handle);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(out_handle->i2c_dev_handle);
        i2c_del_master_bus(out_handle->i2c_bus_handle);
        out_handle->i2c_dev_handle = NULL;
        out_handle->i2c_bus_handle = NULL;
        return ret;
    }

    /* Allocate internal state */
    imx219_device_t *sensor = (imx219_device_t *)calloc(1, sizeof(imx219_device_t));
    if (sensor == NULL) {
        i2c_master_bus_rm_device(out_handle->i2c_dev_handle);
        i2c_del_master_bus(out_handle->i2c_bus_handle);
        out_handle->i2c_dev_handle = NULL;
        out_handle->i2c_bus_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    sensor->i2c_dev = out_handle->i2c_dev_handle;
    sensor->fmt = fmt;
    out_handle->sensor = sensor;

    /* Write register init sequence (sensor stays in standby after this) */
    ESP_LOGI(TAG, "Configuring format: %s (%dx%d %dfps %s)",
             fmt->name, fmt->width, fmt->height, fmt->fps,
             fmt->bpp == 8 ? "RAW8" : "RAW10");

    ret = imx219_write_regs(out_handle->i2c_dev_handle,
                            fmt->regs,
                            fmt->regs_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register init failed: %s", esp_err_to_name(ret));
        imx219_deinit(*out_handle);
        return ret;
    }

    ESP_LOGI(TAG, "IMX219 initialized successfully");
    return ESP_OK;
}

esp_err_t imx219_set_exposure(imx219_handle_t handle, uint16_t exposure)
{
    if (handle.i2c_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = imx219_write_reg(handle.i2c_dev_handle, IMX219_REG_EXPOSURE,     (uint8_t)(exposure >> 8));
    if (ret != ESP_OK) return ret;
    return imx219_write_reg(handle.i2c_dev_handle, IMX219_REG_EXPOSURE + 1, (uint8_t)(exposure & 0xFF));
}

esp_err_t imx219_set_analog_gain(imx219_handle_t handle, uint8_t gain)
{
    if (handle.i2c_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    return imx219_write_reg(handle.i2c_dev_handle, IMX219_REG_ANALOG_GAIN, gain);
}

/* ── 朝向/翻转控制 ── */

esp_err_t imx219_set_orientation(imx219_handle_t handle, uint8_t orient)
{
    if (handle.i2c_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    return imx219_write_reg(handle.i2c_dev_handle,
                            IMX219_REG_ORIENTATION,
                            orient & 0x03);
}

esp_err_t imx219_start_stream(imx219_handle_t handle)
{
    if (handle.sensor == NULL || handle.i2c_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = imx219_write_reg(handle.i2c_dev_handle,
                                     IMX219_REG_MODE_SELECT,
                                     IMX219_MODE_STREAMING);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Streaming started");
        /* Wait for sensor PLL to stabilize and MIPI PHY to lock */
        vTaskDelay(pdMS_TO_TICKS(30));
    } else {
        ESP_LOGE(TAG, "Failed to start streaming: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t imx219_stop_stream(imx219_handle_t handle)
{
    if (handle.sensor == NULL || handle.i2c_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = imx219_write_reg(handle.i2c_dev_handle,
                                     IMX219_REG_MODE_SELECT,
                                     IMX219_MODE_STANDBY);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Streaming stopped");
    } else {
        ESP_LOGE(TAG, "Failed to stop streaming: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t imx219_deinit(imx219_handle_t handle)
{
    if (handle.sensor) {
        /* Stop streaming if active */
        if (handle.i2c_dev_handle) {
            imx219_write_reg(handle.i2c_dev_handle,
                             IMX219_REG_MODE_SELECT,
                             IMX219_MODE_STANDBY);
        }
        free(handle.sensor);
    }

    if (handle.i2c_dev_handle) {
        i2c_master_bus_rm_device(handle.i2c_dev_handle);
    }
    if (handle.i2c_bus_handle) {
        i2c_del_master_bus(handle.i2c_bus_handle);
    }

    return ESP_OK;
}
