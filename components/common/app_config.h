/**
 * @file app_config.h
 * @brief ESP32-P4 + IMX219 — 集中配置头文件
 *
 * 所有调参都在这里改，不用翻各个 c 文件。
 * 模块开关: 1 = 启用, 0 = 关闭 (关闭的模块编译时排除)
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ════════════════════════════════════════════
 * 模块开关
 * ════════════════════════════════════════════ */
#define APP_ENABLE_JPEG_ENCODE         1   // JPEG 硬件编码 (图传需要)
#define APP_ENABLE_USB_STREAM          1   // USB Serial/JTAG 图传
#define APP_ENABLE_SOFTWARE_VFLIP      0   // 软件垂直翻转 (传感器硬件翻转时保持 0!)

/* ════════════════════════════════════════════
 * 传感器参数 (IMX219)
 * ════════════════════════════════════════════ */
#define APP_SENSOR_WIDTH               640
#define APP_SENSOR_HEIGHT              480
#define APP_SENSOR_FORMAT              "MIPI_2lane_24Minput_RAW8_640x480_60fps"
#define APP_SENSOR_BPP                 3       // ISP 输出: RGB888
#define APP_SENSOR_MIPI_BITRATE_MBPS   912     // IMX219 2-lane

/* 传感器朝向 (0=正常, 1=水平镜像, 2=垂直翻转, 3=180°旋转)
 * 同时设置 IMX219 寄存器翻转 + ISP Bayer 相位，自动同步。
 * 注意: 这里配置了硬件翻转就不要开 APP_ENABLE_SOFTWARE_VFLIP, 否则翻转两次 */
#define APP_SENSOR_ORIENTATION         0

/* I2C (SCCB) 引脚 — FireBeetle 2 DFR1172 */
#define APP_SENSOR_I2C_SCL_IO          (8)
#define APP_SENSOR_I2C_SDA_IO          (7)

/* LDO: MIPI PHY 供电 (板级参数, 按你的板子改) */
#define APP_LDO_MIPI_PHY_CHAN          3       // FireBeetle 2
#define APP_LDO_MIPI_PHY_MV            2500

/* ════════════════════════════════════════════
 * ISP + 管线参数
 * ════════════════════════════════════════════ */
#define APP_PIPELINE_QUEUE_ITEMS       3       // 帧缓冲队列深度
#define APP_PIPELINE_ISP_CLK_SRC       ISP_CLK_SRC_DEFAULT

/* ════════════════════════════════════════════
 * 任务优先级
 * ════════════════════════════════════════════ */
#define APP_PRIO_FRAME_TASK            3       // 取帧 (最高, 不被图传阻塞)
#define APP_PRIO_SEND_TASK             1       // 图传 (最低, 可被抢占)

/* ════════════════════════════════════════════
 * JPEG 编码参数
 * ════════════════════════════════════════════ */
#define APP_JPEG_QUALITY               80      // 1~100

/* ════════════════════════════════════════════
 * USB 图传参数
 * ════════════════════════════════════════════ */
#define APP_USB_BAUDRATE               460800  // 保留兼容 (实际走 USB CDC)
#define APP_USB_SEND_INTERVAL          5       // 每 N 帧发送一帧

#ifdef __cplusplus
}
#endif
