/*
 * @file uart_send.h
 * @brief USB Serial/JTAG 图传: RGB888 → JPEG 硬件编码 → USB CDC (Type-C 口)
 *
 * 协议 (与 usb_stream 兼容):
 *   [4B magic 0xA5A5A5A5]
 *   [4B frame_id (LE)]
 *   [4B jpeg_len (LE)]
 *   [4B face_count (LE)]
 *   [jpeg_len B JPEG data]
 *
 * 硬件:
 *   FireBeetle 2 Type-C 下载口 = USB Serial/JTAG CDC
 *   日志 (ESP_LOG) 和 JPEG 数据混在同一通道,
 *   PC viewer 通过 magic 0xA5A5A5A5 区分。
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 人脸检测结果 (与 face_detect.hpp 对齐，兼容 usb_stream 协议) */
typedef struct {
    float   score;
    int16_t box[4];       /* x1, y1, x2, y2 (640×480 空间) */
    int16_t keypoint[10]; /* [左眼,左嘴,鼻,右眼,右嘴] */
} __attribute__((packed)) usb_face_result_t;

/* YOLO 检测结果 */
typedef struct {
    int32_t class_id;     /* 0=FIRE, 1=FALLEN */
    float   score;
    int16_t box[4];       /* x1, y1, x2, y2 (640×480 空间) */
} __attribute__((packed)) usb_yolo_result_t;

esp_err_t uart_send_init(void);
esp_err_t uart_send_frame(const void *rgb888_data, uint32_t frame_id,
                          const void *face_results, int face_count,
                          const void *yolo_results, int yolo_count);
void      uart_send_deinit(void);

#ifdef __cplusplus
}
#endif
