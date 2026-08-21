/**
 * @file main.c
 * @brief ESP32-P4 + IMX219 — 摄像头采集 → USB 图传 (最小可用示例)
 *
 * 管线:
 *   IMX219 → CSI → ISP (WBG/CCM/Color/AE) → cam_pipeline (双缓冲)
 *   → [可选软件翻转] → JPEG 硬件编码 → USB Serial/JTAG 发送
 *
 * 无推理 / 无无线, 纯摄像头采集 + 图传, 作为 IMX219 使用的最小示例。
 * 配置请改 app_config.h。
 *
 * 任务架构: 取帧(prio 3) 与 图传(prio 1) 分离, 图传不阻塞取帧。
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "driver/i2c_master.h"
#include "driver/isp.h"
#include "driver/isp_wbg.h"
#include "driver/isp_ae.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "app_config.h"
#include "imx219.h"
#include "cam_Pipeline.h"
#include "uart_send.h"
#include "hal/color_types.h"

static const char *TAG = "capture";

/* ════════════════════════════════════════════
 * 传感器朝向 → ISP Bayer 相位映射
 * APP_SENSOR_ORIENTATION: 0=正常,1=H-flip,2=V-flip,3=180°
 * IMX219 出厂默认 Bayer = BGGR，翻转后对应关系:
 *   正常 → BGGR, H-flip → GBRG, V-flip → GRBG, 180° → RGGB
 * ════════════════════════════════════════════ */
#if APP_SENSOR_ORIENTATION < 0 || APP_SENSOR_ORIENTATION > 3
#  error "APP_SENSOR_ORIENTATION must be 0(Normal),1(H-flip),2(V-flip),3(180°)"
#endif
static const color_raw_element_order_t s_orient_to_bayer[] = {
    COLOR_RAW_ELEMENT_ORDER_BGGR,  /* 0 = Normal */
    COLOR_RAW_ELEMENT_ORDER_GBRG,  /* 1 = H-flip  */
    COLOR_RAW_ELEMENT_ORDER_GRBG,  /* 2 = V-flip  */
    COLOR_RAW_ELEMENT_ORDER_RGGB,  /* 3 = 180°    */
};

#if APP_ENABLE_SOFTWARE_VFLIP
/* ════════════════════════════════════════════
 * 软件垂直翻转: 交换上下两半行 (不碰像素内部, 不影响颜色)
 * 仅在未用寄存器硬件翻转时使用 (见 README 踩坑记录)
 * ════════════════════════════════════════════ */
static void vflip(uint8_t *buf, int w, int h)
{
    int pitch = w * 3;
    uint8_t *tmp = malloc(pitch);
    if (!tmp) return;
    for (int y = 0; y < h / 2; y++) {
        int bot = h - 1 - y;
        memcpy(tmp, buf + y     * pitch, pitch);
        memcpy(buf + y     * pitch, buf + bot * pitch, pitch);
        memcpy(buf + bot * pitch, tmp, pitch);
    }
    free(tmp);
}
#endif /* APP_ENABLE_SOFTWARE_VFLIP */

/* — 共享缓冲 (frame task 写, send task 读) — */
#define SEND_FRAME_BYTES  (APP_SENSOR_WIDTH * APP_SENSOR_HEIGHT * 3)
#define SEND_QUEUE_LEN    2                               /* 最多 backlog 2 帧 */
static QueueHandle_t     s_send_queue = NULL;             /* 传递 uint8_t* (帧拷贝, 用完 free) */
static uint32_t          s_send_fid    = 0;

/* ════════════════════════════════════════════
 * send 任务 (Core 0, prio 1): 等信号 → JPEG 编码 → USB 发送
 * 被 frame 任务(prio 3) 抢占, 不阻塞取帧
 * ════════════════════════════════════════════ */
static void send_task_run(void *arg)
{
    (void)arg;
    while (1) {
        uint8_t *frame_copy = NULL;
        xQueueReceive(s_send_queue, &frame_copy, portMAX_DELAY);
        if (!frame_copy) continue;

        uint32_t fid = s_send_fid;
        uart_send_frame(frame_copy, fid, NULL, 0, NULL, 0);
        free(frame_copy);  /* 用完释放 */
    }
}

void app_main(void)
{
    /* ── 1. LDO: MIPI PHY 供电 (板级, 见 app_config.h) ── */
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = APP_LDO_MIPI_PHY_CHAN,
        .voltage_mv = APP_LDO_MIPI_PHY_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));

    /* ── 2. I2C: IMX219 初始化 ── */
    imx219_handle_t imx219_handle = { 0 };
    imx219_config_t cam_config = {
        .i2c_port_num = I2C_NUM_0,
        .i2c_sda_io_num = APP_SENSOR_I2C_SDA_IO,
        .i2c_scl_io_num = APP_SENSOR_I2C_SCL_IO,
        .format_name = APP_SENSOR_FORMAT,
    };
    ESP_ERROR_CHECK(imx219_init(&cam_config, &imx219_handle));

    /* ── 2b. 传感器朝向（APP_SENSOR_ORIENTATION）── */
    /* 必须在 start_stream 之前设置（sensor standby 状态） */
#if APP_SENSOR_ORIENTATION != 0
    ESP_LOGI(TAG, "Sensor orientation: %d (reg=0x%02x, ISP bayer=%d)",
             APP_SENSOR_ORIENTATION,
             APP_SENSOR_ORIENTATION,
             s_orient_to_bayer[APP_SENSOR_ORIENTATION]);
    ESP_ERROR_CHECK(imx219_set_orientation(imx219_handle, APP_SENSOR_ORIENTATION));
#endif

    /* ── 3. CSI 控制器 ── */
    const int sensor_w = APP_SENSOR_WIDTH;
    const int sensor_h = APP_SENSOR_HEIGHT;

    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = sensor_w,
        .v_res = sensor_h,
        .lane_bit_rate_mbps = APP_SENSOR_MIPI_BITRATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RAW8,
        .data_lane_num = 2,
        .byte_swap_en = false,
        .queue_items = APP_PIPELINE_QUEUE_ITEMS,
    };
    esp_cam_ctlr_handle_t cam_handle = NULL;
    ESP_ERROR_CHECK(esp_cam_new_csi_ctlr(&csi_config, &cam_handle));

    /* ── 4. ISP 处理器 ── */
    isp_proc_handle_t isp_proc = NULL;
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = 80 * 1000 * 1000,
        .clk_src = APP_PIPELINE_ISP_CLK_SRC,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB888,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = sensor_w,
        .v_res = sensor_h,
        .bayer_order = s_orient_to_bayer[APP_SENSOR_ORIENTATION],
    };
    ESP_ERROR_CHECK(esp_isp_new_processor(&isp_config, &isp_proc));
    ESP_ERROR_CHECK(esp_isp_enable(isp_proc));

    /* 4b. WBG 白平衡增益 */
    esp_isp_wbg_config_t wbg_cfg = { .flags.update_once_configured = true };
    esp_err_t wbg_ret = esp_isp_wbg_configure(isp_proc, &wbg_cfg);
    if (wbg_ret == ESP_OK) {
        ESP_ERROR_CHECK(esp_isp_wbg_enable(isp_proc));
        esp_isp_wbg_set_wb_gain(isp_proc, (isp_wbg_gain_t){
            .gain_r = 435, .gain_g = 256, .gain_b = 435,
        });
        ESP_LOGI(TAG, "WBG enabled (R:1.7 G:1.0 B:1.7)");
    } else {
        ESP_LOGW(TAG, "WBG not supported on this chip rev, using software correction");
    }

    /* 4c. CCM 色彩校正 */
    esp_isp_ccm_config_t ccm_cfg = {
        .matrix = {
            { 1.3, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 1.3 },
        },
        .saturation = false,
    };
    ESP_ERROR_CHECK(esp_isp_ccm_configure(isp_proc, &ccm_cfg));
    ESP_ERROR_CHECK(esp_isp_ccm_enable(isp_proc));
    ESP_LOGI(TAG, "CCM configured (R:1.3 G:1.0 B:1.3)");

    /* 4d. Color 色彩控制器 */
    esp_isp_color_config_t color_cfg = {
        .color_saturation = { .integer = 1, .decimal = 40 },
        .color_contrast   = { .integer = 1, .decimal = 5 },
        .color_hue        = 0,
        .color_brightness = 0,
    };
    ESP_ERROR_CHECK(esp_isp_color_configure(isp_proc, &color_cfg));
    ESP_ERROR_CHECK(esp_isp_color_enable(isp_proc));

    /* 4e. AE 自动曝光 */
    isp_ae_ctlr_t ae_ctlr = NULL;
    esp_isp_ae_config_t ae_cfg = {
        .sample_point = ISP_AE_SAMPLE_POINT_AFTER_DEMOSAIC,
        .window = {
            .top_left  = { .x = 0, .y = 0 },
            .btm_right = { .x = sensor_w - 1, .y = sensor_h - 1 },
        },
    };
    ESP_ERROR_CHECK(esp_isp_new_ae_controller(isp_proc, &ae_cfg, &ae_ctlr));
    ESP_ERROR_CHECK(esp_isp_ae_controller_enable(ae_ctlr));

    /* ── 5. Pipeline: 双缓冲帧管理 ── */
    cam_Pipeline_Handle pipe = NULL;
    cam_Pipeline_Config pipe_config = {
        .h_res = sensor_w,
        .v_res = sensor_h,
        .bytes_per_pixel = APP_SENSOR_BPP,
        .queue_items = APP_PIPELINE_QUEUE_ITEMS,
    };
    ESP_ERROR_CHECK(cam_Pipeline_Init(&pipe_config, &pipe));
    ESP_ERROR_CHECK(cam_Pipeline_BindCSI(pipe, cam_handle));

    /* ── 6. USB 图传初始化 ── */
    ESP_ERROR_CHECK(uart_send_init());
    ESP_LOGI(TAG, "USB stream ready");

    /* ── 7. 启动采集 ── */
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(cam_handle));
    ESP_ERROR_CHECK(esp_cam_ctlr_start(cam_handle));
    ESP_ERROR_CHECK(imx219_start_stream(imx219_handle));
    ESP_LOGI(TAG, "[管线] 启动 (%dx%d)", sensor_w, sensor_h);

    /* ── 8. TASK_WDT 配置 (仅 Core 0 喂狗) ── */
    esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0x01,
        .trigger_panic = false,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_cfg));

    /* ── 9. 图传任务 (prio 1) + 取帧优先级提升至 3 ── */
    s_send_queue = xQueueCreate(SEND_QUEUE_LEN, sizeof(uint8_t *));
    xTaskCreatePinnedToCore(send_task_run, "send", 4 * 1024,
                            NULL, APP_PRIO_SEND_TASK, NULL, 0);
    vTaskPrioritySet(NULL, APP_PRIO_FRAME_TASK);

    /* ── 10. 主循环: 采集 → (翻转) → AE → 定时图传 ── */
    uint32_t frame_count = 0;
    const int STATS_INTERVAL = 60;
    int64_t t_start = esp_timer_get_time();
    uint16_t ae_exposure = 1750;
    uint8_t  ae_gain = 0xE8;
    const uint16_t AE_MAX_EXPOSURE = 20000;
    const uint8_t  AE_MAX_GAIN = 0xE8;
    const uint8_t  AE_MIN_GAIN = 0x40;
    const int AE_TARGET_LUM = 120;
    const int AE_DEAD_ZONE = 12;
    bool ae_last_dir_up = false;
    int last_ae_lum = 0;

    while (1) {
        void *frame = NULL;
        size_t frame_size = 0;

        esp_err_t err = cam_Pipeline_GetFrame(pipe, &frame, &frame_size,
                                              pdMS_TO_TICKS(5000));
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "W Frame timeout at count=%lu", frame_count);
            if (frame_count > 10) break;
            continue;
        }
        ESP_ERROR_CHECK(err);

        frame_count++;

        /* ═══ 软件垂直翻转 (全帧原地, 可选) ═══ */
#if APP_ENABLE_SOFTWARE_VFLIP
        vflip((uint8_t*)frame, sensor_w, sensor_h);
#endif

        /* ═══ AE 自动曝光 ═══ */
        {
            isp_ae_result_t ae_res;
            esp_err_t ae_err = esp_isp_ae_controller_get_oneshot_statistics(ae_ctlr, 2000, &ae_res);
            if (ae_err == ESP_OK) {
                int sum = 0;
                for (int x = 0; x < ISP_AE_BLOCK_X_NUM; x++)
                    for (int y = 0; y < ISP_AE_BLOCK_Y_NUM; y++)
                        sum += ae_res.luminance[x][y];
                int avg = sum / (ISP_AE_BLOCK_X_NUM * ISP_AE_BLOCK_Y_NUM);
                int err_ae = AE_TARGET_LUM - avg;
                last_ae_lum = avg;
                if (abs(err_ae) > AE_DEAD_ZONE) {
                    bool dir_up = (err_ae > 0);
                    int abs_err_ae = abs(err_ae);
                    int step_gain = (abs_err_ae > 60) ? 8 : (abs_err_ae > 30) ? 4 : 2;
                    int step_exp  = (abs_err_ae > 60) ? 200 : (abs_err_ae > 30) ? 60 : 20;
                    if (dir_up != ae_last_dir_up) {
                        step_gain = (step_gain > 2) ? step_gain / 2 : 1;
                        step_exp  = (step_exp  > 8) ? step_exp  / 2 : 4;
                    }
                    ae_last_dir_up = dir_up;
                    if (dir_up) {
                        if (ae_exposure < AE_MAX_EXPOSURE) {
                            int new_exp = ae_exposure + step_exp;
                            ae_exposure = (new_exp > AE_MAX_EXPOSURE) ? AE_MAX_EXPOSURE : new_exp;
                            imx219_set_exposure(imx219_handle, ae_exposure);
                        } else if (ae_gain < AE_MAX_GAIN) {
                            int new_gain = ae_gain + step_gain;
                            ae_gain = (new_gain > AE_MAX_GAIN) ? AE_MAX_GAIN : (uint8_t)new_gain;
                            imx219_set_analog_gain(imx219_handle, ae_gain);
                        }
                    } else {
                        if (ae_gain > AE_MIN_GAIN) {
                            int new_gain = ae_gain - step_gain;
                            ae_gain = (new_gain < AE_MIN_GAIN) ? AE_MIN_GAIN : (uint8_t)new_gain;
                            imx219_set_analog_gain(imx219_handle, ae_gain);
                        } else if (ae_exposure > 100) {
                            int new_exp = ae_exposure - step_exp;
                            ae_exposure = (new_exp < 100) ? 100 : new_exp;
                            imx219_set_exposure(imx219_handle, ae_exposure);
                        }
                    }
                }
            }
        }

        /* ═══ 定时投递 JPEG 图传 (每 N 帧) ═══ */
        if (s_send_queue && (frame_count % APP_USB_SEND_INTERVAL == 0)) {
            uint8_t *copy = (uint8_t *)heap_caps_malloc(SEND_FRAME_BYTES, MALLOC_CAP_SPIRAM);
            if (copy) {
                memcpy(copy, (const uint8_t*)frame, SEND_FRAME_BYTES);
                s_send_fid = frame_count;
                if (xQueueSend(s_send_queue, &copy, 0) != pdTRUE) {
                    heap_caps_free(copy);   /* queue 满自动丢帧 */
                }
            }
        }

        /* ═══ 统计输出 ═══ */
        if (frame_count % STATS_INTERVAL == 0) {
            int64_t now = esp_timer_get_time();
            float elapsed = (now - t_start) / 1e6f;
            float fps = elapsed > 0 ? frame_count / elapsed : 0;
            ESP_LOGI(TAG, "[统计] %lus F%lu FPS=%.1f AE: lum=%d exp=%u gain=0x%02X",
                     (unsigned long)elapsed, frame_count, fps,
                     last_ae_lum, ae_exposure, ae_gain);
        }
    }

    /* ── 11. 清理 ── */
    esp_cam_ctlr_stop(cam_handle);
    imx219_stop_stream(imx219_handle);
    esp_cam_ctlr_disable(cam_handle);
    esp_cam_ctlr_del(cam_handle);
    esp_isp_disable(isp_proc);
    esp_isp_ae_controller_disable(ae_ctlr);
    esp_isp_del_ae_controller(ae_ctlr);
    esp_isp_del_processor(isp_proc);
    cam_Pipeline_Deinit(pipe);
    uart_send_deinit();
    esp_ldo_release_channel(ldo_mipi_phy);
    ESP_LOGI(TAG, "Done! Total frames: %lu", frame_count);
}
