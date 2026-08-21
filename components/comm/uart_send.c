/*
 * @file uart_send.c
 * @brief USB Serial/JTAG 图传: RGB888 → JPEG 硬件编码 → USB CDC (下载口)
 *
 * 接收 RGB888 全帧 (如 640×480), JPEG 编码器直接取 RGB888,
 * 硬件自带 CSC, 无需软件转换
 */

#include "uart_send.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "uart_snd";

#define FRAME_WIDTH    APP_SENSOR_WIDTH
#define FRAME_HEIGHT   APP_SENSOR_HEIGHT
#define FRAME_BYTES    (FRAME_WIDTH * FRAME_HEIGHT * 3)  /* RGB888 */
#define JPEG_BUF_SIZE  (FRAME_WIDTH * FRAME_HEIGHT)       /* max JPEG */
#define FRAME_MAGIC    0xA5A5A5A5
#define YOLO_RESULT_SIZE ((int)sizeof(usb_yolo_result_t))

/* ─── USB Serial/JTAG 参数 ─── */
#define TX_BUF_SIZE     65536             /* TX 缓冲 64KB */

/* ─── 状态 ─── */
static jpeg_enc_handle_t s_jpeg_enc = NULL;
static uint8_t *s_jpeg_buf = NULL;
static uint8_t *s_tx_buf   = NULL;
static size_t   s_tx_cap   = 0;
static bool     s_inited   = false;

esp_err_t uart_send_init(void)
{
    if (s_inited) return ESP_OK;

    /* ── JPEG 编码器 (RGB888 输入, 硬件 CSC 自动转 YUV) ── */
    jpeg_enc_config_t jpeg_cfg = {
        .width       = FRAME_WIDTH,
        .height      = FRAME_HEIGHT,
        .src_type    = JPEG_PIXEL_FORMAT_RGB888,
        .subsampling = JPEG_SUBSAMPLE_420,
        .quality     = APP_JPEG_QUALITY,
        .rotate      = JPEG_ROTATE_0D,
        .task_enable = false,
    };
    esp_err_t ret = jpeg_enc_open(&jpeg_cfg, &s_jpeg_enc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── 缓冲 (PSRAM) ── */
    s_jpeg_buf = (uint8_t *)heap_caps_aligned_alloc(0x80, JPEG_BUF_SIZE, MALLOC_CAP_DEFAULT);
    s_tx_cap   = 20 + JPEG_BUF_SIZE
                 + 10 * sizeof(usb_face_result_t)
                 + 10 * sizeof(usb_yolo_result_t);
    s_tx_buf   = (uint8_t *)heap_caps_aligned_alloc(0x80, s_tx_cap, MALLOC_CAP_DEFAULT);
    if (!s_jpeg_buf || !s_tx_buf) {
        ESP_LOGE(TAG, "OOM");
        ret = ESP_FAIL;
        goto err;
    }

    /* ── USB Serial/JTAG 驱动安装 ── */
    usb_serial_jtag_driver_config_t usj_cfg = {
        .tx_buffer_size = TX_BUF_SIZE,
        .rx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_cfg));
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(TAG, "USB Serial/JTAG ready (tx_buf=%u)", TX_BUF_SIZE);

    s_inited = true;
    ESP_LOGI(TAG, "Ready (%dx%d RGB888 → JPEG via USB CDC)", FRAME_WIDTH, FRAME_HEIGHT);
    return ESP_OK;

err:
    if (s_tx_buf)   { free(s_tx_buf);   s_tx_buf   = NULL; }
    if (s_jpeg_buf) { free(s_jpeg_buf); s_jpeg_buf = NULL; }
    if (s_jpeg_enc) { jpeg_enc_close(s_jpeg_enc);   s_jpeg_enc = NULL; }
    return ret;
}


esp_err_t uart_send_frame(const void *rgb888_data,
                           uint32_t frame_id,
                           const void *face_results, int face_count,
                           const void *yolo_results, int yolo_count)
{
    if (!s_inited || !s_jpeg_enc) return ESP_ERR_INVALID_STATE;

    /* 1. JPEG 编码 (RGB888 直入, 硬件 CSC 转 YUV) */
    int jpeg_len = 0;
    esp_err_t ret = jpeg_enc_process(s_jpeg_enc,
                                     (const uint8_t*)rgb888_data, FRAME_BYTES,
                                     s_jpeg_buf, JPEG_BUF_SIZE,
                                     &jpeg_len);
    if (ret != ESP_OK || jpeg_len <= 0) {
        ESP_LOGW(TAG, "JPEG fail: %s len=%d", esp_err_to_name(ret), jpeg_len);
        return (ret != ESP_OK) ? ret : ESP_FAIL;
    }

    /* 2. 拼装头 + JPEG + 人脸数据 + YOLO 数据 */
    size_t face_bytes = (face_count > 0 && face_results)
                        ? (size_t)face_count * sizeof(usb_face_result_t) : 0;
    size_t yolo_bytes = (yolo_count > 0 && yolo_results)
                        ? (size_t)yolo_count * sizeof(usb_yolo_result_t) : 0;
    size_t total = 20 + jpeg_len + face_bytes + yolo_bytes;
    if (total > s_tx_cap) {
        ESP_LOGW(TAG, "Frame too big: %zu > %zu", total, s_tx_cap);
        return ESP_FAIL;
    }
    uint32_t header[5] = {
        FRAME_MAGIC, frame_id, (uint32_t)jpeg_len,
        (uint32_t)face_count, (uint32_t)yolo_count
    };
    uint8_t *p = s_tx_buf;
    memcpy(p, header, sizeof(header)); p += sizeof(header);
    memcpy(p, s_jpeg_buf, jpeg_len);  p += jpeg_len;
    if (face_bytes > 0 && face_results) {
        memcpy(p, face_results, face_bytes); p += face_bytes;
    }
    if (yolo_bytes > 0 && yolo_results) {
        memcpy(p, yolo_results, yolo_bytes); p += yolo_bytes;
    }

    /* 3. USB Serial/JTAG 发送 */
    int written = usb_serial_jtag_write_bytes(s_tx_buf, total, pdMS_TO_TICKS(5000));
    if (written < 0) {
        ESP_LOGE(TAG, "USJ write failed: %d", written);
        return ESP_FAIL;
    }
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(5000));

    ESP_LOGD(TAG, "TX frame #%lu: jpeg=%dB usj=%dB",
             frame_id, jpeg_len, written);
    return ESP_OK;
}


void uart_send_deinit(void)
{
    if (s_jpeg_enc)  { jpeg_enc_close(s_jpeg_enc); s_jpeg_enc = NULL; }
    if (s_tx_buf)    { free(s_tx_buf);   s_tx_buf   = NULL; }
    if (s_jpeg_buf)  { free(s_jpeg_buf); s_jpeg_buf = NULL; }
    usb_serial_jtag_driver_uninstall();
    s_inited = false;
    ESP_LOGI(TAG, "Deinit");
}
