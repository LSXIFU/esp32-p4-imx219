# esp32-p4-imx219

**ESP32-P4 + IMX219 摄像头最小可用示例** — IMX219 → CSI → ISP (RGB888) → JPEG 硬件编码 → USB 图传。

> 目标读者：想在 **ESP32-P4 上点亮 IMX219** 的人。这是比赛验证过的精简版：无推理、无无线，纯摄像头采集 + 图传，外加踩坑记录。

| Supported Targets | ESP32-P4 |
| ----------------- | -------- |

## 特性

- **自写 IMX219 驱动**（MIPI-CSI 2-lane，640×480 RAW8 60fps），含曝光/增益控制、寄存器硬件翻转
- **完整 ISP 管线**：WBG 白平衡 → CCM 色彩校正 → Color 饱和/对比 → AE 自动曝光 → RGB888 输出
- **双缓冲管线**（`cam_pipeline`）：采集永远不阻塞，消费慢时自动丢帧
- **USB 图传**：JPEG 硬件编码（RGB888 直入，硬件 CSC）→ USB Serial/JTAG（Type-C 下载口），PC 端 `tools/uart_viewer.py` 预览
- 传感器朝向一键配置：寄存器翻转 + ISP Bayer 相位自动同步

## 硬件

- ESP32-P4 开发板（示例按 FireBeetle 2 配置：I2C GPIO7/8，LDO CH3 2500mV，见 `components/common/app_config.h`）
- IMX219 摄像头模块（MIPI-CSI 排线）
- 按板子改：I2C 引脚、LDO 通道/电压

## 快速开始

```bash
# 依赖: ESP-IDF v5.x (esp32p4 target)
idf.py set-target esp32p4
idf.py build flash monitor
```

PC 端看图：

```bash
pip install pyserial pillow
python tools/uart_viewer.py --port COMx
```

所有调参都在 `components/common/app_config.h`：分辨率、I2C 引脚、LDO、朝向、JPEG 质量、发送间隔。

## 项目结构

```
esp32-p4-imx219/
├── main/main.c               # IMX219→CSI→ISP→采集→图传
├── components/
│   ├── imx219/               # IMX219 传感器驱动 (自写)
│   ├── cam_pipeline/         # 双缓冲帧管线
│   ├── comm/uart_send.c      # USB 图传 (RGB888→JPEG→USB CDC)
│   └── common/app_config.h   # 集中配置
└── tools/uart_viewer.py      # PC 端 USB 图传查看器
```

## IMX219 踩坑记录

### ① 硬件翻转与软件翻转互斥（重复翻转）

IMX219 **寄存器本身就支持硬件翻转**（`0x0170` 水平/垂直翻转，`0x0171` 位深）。如果已用寄存器配置翻转，**必须关闭软件翻转**（`APP_ENABLE_SOFTWARE_VFLIP=0`），否则画面翻转两次（转回原样），还白费 CPU。

### ② 翻转后必须同步 ISP Bayer 相位（颜色异常）

传感器硬件翻转后，**Bayer 顺序跟着变**，必须同步 ISP 的 `bayer_order`，否则颜色全乱：

| 朝向 | IMX219 寄存器 | ISP Bayer |
|:---|:---|:---|
| 正常 | 无 | BGGR |
| 水平镜像 | 0x0170 H-flip | GBRG |
| 垂直翻转 | 0x0170 V-flip | GRBG |
| 180° | 0x0170 H+V | RGGB |

本示例用 `APP_SENSOR_ORIENTATION` 一键配置，两者自动同步（见 `main.c` 的 `s_orient_to_bayer`）。

### ③ 自写驱动只注册了 640×480 模式

默认驱动**只注册 640×480 一个模式**。加分辨率（1920×1080、1280×720…）必须配齐 **PLL 倍频、binning、crop 窗口、输出尺寸** 四组寄存器，漏一个就是花屏/黑屏/帧率错。

IMX219 原生常见模式：

| 分辨率 | 帧率 |
|:---|:---:|
| 3280×2464 | 15fps |
| 1920×1080 | 30fps |
| 1640×1232 | 30fps |
| 1280×720 | 60fps |
| 640×480 | 60–206fps |

### ④ 早期 P4 (rev < v3.0) ISP 不支持硬件 crop

部分早期 ESP32-P4（v3.0 之前）调用 ISP crop 会报 `Crop is not supported on ESP32P4 chips prior than v3.0`。方案：保持传感器原始分辨率，在图像处理管线里做软件裁剪/缩放。

### ⑤ 网上流传的驱动来源要辨别

大量"IMX219 驱动"的底料是 **NVIDIA Jetson/Tegra imx219 驱动**（GPL-2.0，tegra186 设备树）改给树莓派用。**不是** Linux 主线 `torvalds/linux` 的 imx219.c。引用外部驱动先看寄存器写法与主线是否一致，别混用。

## USB 图传协议

```
[4B magic 0xA5A5A5A5][4B frame_id][4B jpeg_len][4B face_count][4B yolo_count]
[jpeg_len B JPEG data][face_count × 32B][yolo_count × 16B]
```

本示例 `face_count/yolo_count` 恒为 0。帧头保留计数域是为了兼容原项目推理版协议。

## 许可

Apache-2.0（`LICENSE`）。`components/imx219` 驱动基于 Linux 主线 `imx219.c` 参考重写（GPL-2.0 上游），示例代码结构参考 ESP-IDF 官方 `esp_cam` 例程。
