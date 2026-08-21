#pragma once

/**
 * @file cam_Pipeline.h
 * @brief Camera pipeline — dual-buffer frame management
 *
 * 双缓冲 + 最新帧优先模式：
 * - 摄像头始终以最高帧率采集
 * - 消费端（图传/处理）取最新完成的那一帧
 * - 消费慢时自动丢帧（永远不阻塞采集）
 */

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_cam_ctlr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque pipeline handle */
typedef struct cam_Pipeline_Context *cam_Pipeline_Handle;

/** Pipeline configuration */
typedef struct {
    int h_res;              /**< Horizontal resolution */
    int v_res;              /**< Vertical resolution */
    int bytes_per_pixel;    /**< 1=RAW8, 2=RGB565/YUV422, 3=RGB888 */
    int queue_items;        /**< CSI internal queue depth (>= 2 for ping-pong) */
} cam_Pipeline_Config;

/**
 * @brief Initialize pipeline, allocate double frame buffers
 * @param config  Pipeline parameters
 * @param handle  [out] Opaque handle
 * @return ESP_OK on success
 */
esp_err_t cam_Pipeline_Init(cam_Pipeline_Config *config, cam_Pipeline_Handle *handle);

/**
 * @brief Get the latest completed frame
 *
 * Blocks until a new frame is available (or timeout).
 * Always returns the most recent frame — if multiple frames arrived
 * while you were busy, you only get the latest one (older ones dropped).
 *
 * @param handle   Pipeline handle
 * @param frame    [out] Pointer to frame data (RAW8 Bayer)
 * @param size     [out] Frame size in bytes
 * @param timeout  Max wait (use portMAX_DELAY for infinite)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no frame within timeout
 */
esp_err_t cam_Pipeline_GetFrame(cam_Pipeline_Handle handle, void **frame, size_t *size, TickType_t timeout);

/**
 * @brief Register CSI event callbacks (connect pipeline to CSI controller)
 *
 * @param handle     Pipeline handle
 * @param csi_handle CSI controller handle (already created)
 * @return ESP_OK on success
 */
esp_err_t cam_Pipeline_BindCSI(cam_Pipeline_Handle handle, esp_cam_ctlr_handle_t csi_handle);

/**
 * @brief Free all resources
 */
esp_err_t cam_Pipeline_Deinit(cam_Pipeline_Handle handle);

#ifdef __cplusplus
}
#endif
