/**
 * @file cam_Pipeline.c
 * @brief Camera pipeline implementation
 *
 * == 三缓冲设计 ==
 *
 * 三块 buffer 轮转，CSI 写一块，消费端读一块，各不冲突。
 * 消费慢时 CSI 仍有空闲 buffer，不会覆盖正在读取的数据。
 *
 * ┌─────────────────────────────────────────────┐
 * │  CSI采集     buf[A] buf[B] buf[C] buf[A] .. │  ← 始终全速
 * │  消费端      read latest completed          │  ← 取最新帧
 * │  消费慢时    自动丢帧，不阻塞采集           │
 * └─────────────────────────────────────────────┘
 *
 * == 对齐要求 ==
 * P4 L2 cache line = 128B, buffer 128B 对齐
 *
 * == FreeRTOS 同步 ==
 * on_trans_finished (ISR) → xSemaphoreGiveFromISR
 * cam_Pipeline_GetFrame  → xSemaphoreTake (阻塞等帧)
 */

#include "cam_Pipeline.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "sdkconfig.h"

static const char *TAG = "cam_pipe";

/* ── 内部数据结构 ── */

/* P4 L2 cache line size — 和 sdkconfig.defaults.esp32p4 一致 */
#if CONFIG_CACHE_L2_CACHE_LINE_128B
#define CAM_PIPELINE_ALIGN  128
#else
#define CAM_PIPELINE_ALIGN  64
#endif

/* 3 缓冲: 足够避免 CSI 写覆盖消费端读 */
#define CAM_PIPELINE_NUM_BUFS (3)

struct cam_Pipeline_Context {
    uint8_t *buf[CAM_PIPELINE_NUM_BUFS];   /* 帧缓冲区 (SPIRAM), 128B对齐 */
    size_t   buf_size;
    int      h_res;
    int      v_res;

    /* 索引管理 */
    volatile int fill_idx;      /* CSI 当前填充的 buffer 索引 (ISR 写) */
    volatile int ready_idx;     /* 最新完成帧的 buffer 索引 (ISR 写) */
    volatile int consumer_idx;  /* 消费端正在读的 buffer 索引 (-1 = 无) */

    SemaphoreHandle_t sem;      /* 帧完成信号量 (binary) */

    esp_cam_ctlr_handle_t csi;  /* CSI 控制器 */
    esp_cam_ctlr_trans_t trans[CAM_PIPELINE_NUM_BUFS];

    bool deinited;
};

/* ── CSI 回调函数 (ISR 上下文!) ── */

/**
 * @brief CSI 请求一个新 buffer 来填充
 *
 * 返回最久未使用且没有被消费端持有的 buffer。
 * 如果所有 buffer 都被占用（消费端极慢时），仍然返回一个 —
 * 此时会覆盖数据（即丢帧），但不阻塞采集。
 *
 * @return false = 无需等待，直接用这个 buffer
 */
static bool cb_GetNewTrans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    cam_Pipeline_Handle pipe = (cam_Pipeline_Handle)user_data;
    int idx = pipe->fill_idx % CAM_PIPELINE_NUM_BUFS;

    /* 如果下一个 buffer 正被消费端读，跳过一个 */
    if (idx == pipe->consumer_idx) {
        pipe->fill_idx++;
        idx = pipe->fill_idx % CAM_PIPELINE_NUM_BUFS;
    }

    trans->buffer = pipe->buf[idx];
    trans->buflen = pipe->buf_size;
    return false;
}

/**
 * @brief CSI 完成一帧填充
 *
 * 更新 ready_idx，发出信号量通知 consumer。
 * 切换到下一个 fill buffer 让 CSI 继续写。
 */
static bool cb_TransFinished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    cam_Pipeline_Handle pipe = (cam_Pipeline_Handle)user_data;

    /* 记录最新完成帧 */
    pipe->ready_idx = pipe->fill_idx % CAM_PIPELINE_NUM_BUFS;
    pipe->fill_idx++;

    /* 通知 consumer */
    BaseType_t higher_woken = pdFALSE;
    xSemaphoreGiveFromISR(pipe->sem, &higher_woken);

    return (higher_woken == pdTRUE);
}

/* ── 公开 API ── */

esp_err_t cam_Pipeline_Init(cam_Pipeline_Config *config, cam_Pipeline_Handle *handle)
{
    cam_Pipeline_Handle pipe = heap_caps_calloc(1, sizeof(struct cam_Pipeline_Context), MALLOC_CAP_INTERNAL);
    if (!pipe) {
        return ESP_ERR_NO_MEM;
    }

    pipe->h_res = config->h_res;
    pipe->v_res = config->v_res;
    int bpp = (config->bytes_per_pixel > 0) ? config->bytes_per_pixel : 1;
    pipe->buf_size = config->h_res * config->v_res * bpp;

    /* 分配帧缓冲 (SPIRAM), L2 cache line 对齐 */
    for (int i = 0; i < CAM_PIPELINE_NUM_BUFS; i++) {
        pipe->buf[i] = (uint8_t *)heap_caps_aligned_alloc(
            CAM_PIPELINE_ALIGN, pipe->buf_size, MALLOC_CAP_SPIRAM);
        if (!pipe->buf[i]) {
            ESP_LOGE(TAG, "buf[%d] alloc failed (%zu bytes)", i, pipe->buf_size);
            for (int j = 0; j < i; j++) free(pipe->buf[j]);
            free(pipe);
            return ESP_ERR_NO_MEM;
        }
    }

    /* 创建二进制信号量 */
    pipe->sem = xSemaphoreCreateBinary();
    if (!pipe->sem) {
        for (int i = 0; i < CAM_PIPELINE_NUM_BUFS; i++) free(pipe->buf[i]);
        free(pipe);
        return ESP_ERR_NO_MEM;
    }

    pipe->fill_idx     = 0;
    pipe->ready_idx    = -1;
    pipe->consumer_idx = -1;

    *handle = pipe;
    ESP_LOGI(TAG, "Pipeline init OK (%dx%d, %d buf x %zuB, %dB align)",
             config->h_res, config->v_res, CAM_PIPELINE_NUM_BUFS,
             pipe->buf_size, CAM_PIPELINE_ALIGN);
    return ESP_OK;
}

esp_err_t cam_Pipeline_BindCSI(cam_Pipeline_Handle pipe, esp_cam_ctlr_handle_t csi_handle)
{
    pipe->csi = csi_handle;
    pipe->fill_idx = 0;

    /* 注册回调，user_data = pipeline context */
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = cb_GetNewTrans,
        .on_trans_finished = cb_TransFinished,
    };
    return esp_cam_ctlr_register_event_callbacks(csi_handle, &cbs, pipe);
}

esp_err_t cam_Pipeline_GetFrame(cam_Pipeline_Handle pipe, void **frame, size_t *size, TickType_t timeout)
{
    /* 等新帧信号量 */
    if (xSemaphoreTake(pipe->sem, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* 此时 ready_idx 指向最新完成帧 */
    int idx = pipe->ready_idx;
    if (idx < 0 || idx >= CAM_PIPELINE_NUM_BUFS) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 标记此 buffer 被消费端持有（防止 CSI 覆盖） */
    pipe->consumer_idx = idx;

    /* P4 上尝试 cache sync 可能导致内存损坏，暂时跳过
     * 只有第一次读需要，后续 CSI 持续写 PSRAM 而 CPU 读 cache line 
     * 实测 RAW8 数据正确性不受影响 */
#if 0
    esp_cache_msync(pipe->buf[idx], pipe->buf_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
#endif

    *frame = pipe->buf[idx];
    *size  = pipe->buf_size;
    return ESP_OK;
}

esp_err_t cam_Pipeline_Deinit(cam_Pipeline_Handle pipe)
{
    if (!pipe || pipe->deinited) return ESP_ERR_INVALID_STATE;
    pipe->deinited = true;

    if (pipe->sem) vSemaphoreDelete(pipe->sem);
    for (int i = 0; i < CAM_PIPELINE_NUM_BUFS; i++) {
        if (pipe->buf[i]) free(pipe->buf[i]);
    }
    free(pipe);
    return ESP_OK;
}
