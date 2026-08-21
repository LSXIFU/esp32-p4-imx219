/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * IMX219 camera sensor driver for ESP32-P4.
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "imx219_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IMX219 sensor device handle
 */
typedef struct imx219_device_t imx219_device_t;

/**
 * @brief IMX219 configuration
 */
typedef struct {
    int i2c_sda_io_num;             /*!< I2C SDA GPIO number */
    int i2c_scl_io_num;             /*!< I2C SCL GPIO number */
    int i2c_port_num;               /*!< I2C port number (I2C_NUM_0 or I2C_NUM_1) */
    const char *format_name;        /*!< Desired format name string */
} imx219_config_t;

/**
 * @brief IMX219 sensor handle (returned by imx219_init)
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus_handle;     /*!< I2C bus handle */
    i2c_master_dev_handle_t i2c_dev_handle;     /*!< I2C device handle */
    imx219_device_t *sensor;                    /*!< Internal sensor state */
} imx219_handle_t;

/**
 * @brief Initialize IMX219 sensor
 *
 * Creates I2C bus, detects sensor, and configures the requested format.
 *
 * @param[in]  config       IMX219 configuration
 * @param[out] out_handle   Handle to initialized sensor
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_NOT_FOUND: Sensor not detected on I2C bus
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: General failure
 */
esp_err_t imx219_init(const imx219_config_t *config, imx219_handle_t *out_handle);

/**
 * @brief Start IMX219 video streaming
 *
 * @param[in] handle  Sensor handle from imx219_init
 * @return ESP_OK or error
 */
esp_err_t imx219_start_stream(imx219_handle_t handle);

/**
 * @brief Stop IMX219 video streaming
 *
 * @param[in] handle  Sensor handle from imx219_init
 * @return ESP_OK or error
 */
esp_err_t imx219_stop_stream(imx219_handle_t handle);

/**
 * @brief Deinitialize IMX219 sensor and release resources
 *
 * @param[in] handle  Sensor handle from imx219_init
 * @return ESP_OK or error
 */
esp_err_t imx219_deinit(imx219_handle_t handle);

/* 曝光控制 */
esp_err_t imx219_set_exposure(imx219_handle_t handle, uint16_t exposure);
esp_err_t imx219_set_analog_gain(imx219_handle_t handle, uint8_t gain);

/* 朝向/翻转控制 (register 0x0172)
 * orient: IMX219_ORIENT_NORMAL / _HFLIP / _VFLIP / _180
 * 必须在 imx219_start_stream() 之前调用 (sensor standby 状态)
 */
esp_err_t imx219_set_orientation(imx219_handle_t handle, uint8_t orient);

#ifdef __cplusplus
}
#endif
