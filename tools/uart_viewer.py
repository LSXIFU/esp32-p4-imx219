#!/usr/bin/env python3
"""
USB Serial/JTAG 图传可视化 —— 从 USB CDC (COM6) 接收 JPEG 帧 + 人脸框。

板子通过 Type-C 下载口 (USB Serial/JTAG) 发送混合数据流:
  - ESP_LOGI 文本日志 (ASCII)
  - JPEG 帧 + 人脸结果 (二进制, 以 0xA5A5A5A5 魔数开头)

当前管线: 416×416 RGB888 → JPEG→USB → 本脚本解码 + 画框

用法:
    python uart_viewer.py [COM端口] [波特率]

示例:
    python uart_viewer.py COM6 115200
"""

import sys
import struct
import datetime
import cv2
import numpy as np
import serial
import serial.tools.list_ports
import time
import collections

FRAME_MAGIC = 0xA5A5A5A5
HEADER_SIZE = 20       # 5 × uint32 (magic + fid + jpeg_len + face_cnt + yolo_cnt)
FACE_SIZE   = 32       # float score + 4×int16 box + 10×int16 keypoint
YOLO_SIZE   = 16       # int32 class_id + float score + 4×int16 box

frame_count = 0
fps_timer = cv2.getTickCount()
fps_frames = 0
fps_display = 0


def log_msg(msg):
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {msg}", file=sys.stderr, flush=True)


def auto_find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = f"{p.description or ''} {p.product or ''} {p.manufacturer or ''}".lower()
        if 'cp2102' in desc or 'silicon labs' in desc:
            return p.device
    for p in ports:
        if 'usb' in (p.description or '').lower():
            return p.device
    return ports[0].device if ports else None


def display_loop(ser):
    global frame_count, fps_timer, fps_frames, fps_display

    buf = bytearray()
    cv2.namedWindow("P4 Face Detection (USB)", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("P4 Face Detection (USB)", 640, 640)

    while True:
        try:
            chunk = ser.read(65536)
        except serial.SerialException as e:
            log_msg(f"串口异常: {e}")
            raise

        if not chunk:
            continue

        buf.extend(chunk)

        # ── 在缓冲区里搜魔数 ──
        while True:
            magic_bytes = struct.pack('<I', FRAME_MAGIC)
            idx = buf.find(magic_bytes)

            if idx < 0:
                # 无魔数 → 纯文本
                try:
                    text = bytes(buf).decode('utf-8', errors='replace')
                    if text.strip():
                        print(text, end='', flush=True)
                except Exception:
                    pass
                buf.clear()
                break

            # 魔数前的字节 → 文本日志
            if idx > 0:
                log_text = bytes(buf[:idx]).decode('utf-8', errors='replace')
                if log_text.strip():
                    print(log_text, end='', flush=True)

            if idx + HEADER_SIZE > len(buf):
                buf = buf[idx:]
                break

            magic, fid, jpeg_len, face_cnt, yolo_cnt = struct.unpack_from('<IIIII', buf, idx)
            if magic != FRAME_MAGIC:
                buf = buf[idx + 1:]
                continue

            # 合法性检查
            if jpeg_len <= 0 or jpeg_len > 400000 or face_cnt < 0 or face_cnt > 50 or yolo_cnt < 0 or yolo_cnt > 50:
                buf = buf[idx + 4:]
                continue

            total = HEADER_SIZE + jpeg_len + face_cnt * FACE_SIZE + yolo_cnt * YOLO_SIZE
            if idx + total > len(buf):
                buf = buf[idx:]
                break

            # ── 完整帧到达 ──
            jpeg_data = bytes(buf[idx + HEADER_SIZE : idx + HEADER_SIZE + jpeg_len])

            img = cv2.imdecode(np.frombuffer(jpeg_data, dtype=np.uint8), cv2.IMREAD_COLOR)
            if img is not None:
                off = idx + HEADER_SIZE + jpeg_len
                for f in range(face_cnt):
                    vals = struct.unpack_from('<f4h10h', buf, off + f * FACE_SIZE)
                    score = vals[0]
                    x1, y1, x2, y2 = vals[1:5]
                    kp = list(vals[5:])  # [le_x,le_y, lm_x,lm_y, n_x,n_y, re_x,re_y, rm_x,rm_y]

                    print(f"  > 人脸[{f}]: 置信度={score:.3f}  框=({x1},{y1})-({x2},{y2})", flush=True)

                    # 人脸框
                    cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    cv2.putText(img, f"{score:.2f}", (x1, y1 - 8),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

                    # 特征点 — 模型输出顺序: [左眼, 左嘴, 鼻, 右眼, 右嘴]
                    colors = [(0, 255, 0), (255, 0, 0), (0, 0, 255),
                              (0, 255, 255), (255, 0, 255)]
                    names = ["LE", "LM", "N", "RE", "RM"]
                    kp_names = ["左眼", "左嘴", "鼻", "右眼", "右嘴"]
                    log_kp = []
                    for pi in range(5):
                        px, py = kp[pi*2], kp[pi*2+1]
                        if px > 0 and py > 0:
                            cv2.circle(img, (px, py), 3, colors[pi], -1)
                            cv2.putText(img, names[pi], (px + 4, py),
                                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, colors[pi], 1)
                            log_kp.append(f"{kp_names[pi]}=({px},{py})")
                    if log_kp:
                        print(f"  face[{f}]: {'  '.join(log_kp)}", flush=True)

                fps_frames += 1
                now_tick = cv2.getTickCount()

                # ── YOLO 检测框 ──
                yolo_off = off + face_cnt * FACE_SIZE
                for yi in range(yolo_cnt):
                    class_id, score, x1, y1, x2, y2 = struct.unpack_from('<if4h', buf, yolo_off + yi * YOLO_SIZE)
                    label = "FIRE" if class_id == 0 else "FALLEN"
                    color = (0, 0, 255) if class_id == 0 else (255, 0, 0)  # 红=火, 蓝=摔倒
                    cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
                    cv2.putText(img, f"{label} {score:.2f}", (x1, y1 - 8),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

                dt = (now_tick - fps_timer) / cv2.getTickFrequency()
                if dt >= 1.0:
                    fps_display = fps_frames / dt
                    fps_frames = 0
                    fps_timer = now_tick
                cv2.putText(img, f"FPS: {fps_display:.1f}  Faces: {face_cnt}  Frame: {fid}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                cv2.imshow("P4 Face Detection (USB)", img)
            else:
                dump_path = f"frame_{fid}_{datetime.datetime.now():%H%M%S}.jpg"
                with open(dump_path, "wb") as f:
                    f.write(jpeg_data)
                log_msg(f"帧 {fid}: decode failed, dumped → {dump_path}")

            frame_count += 1
            buf = buf[idx + total:]

        key = cv2.waitKey(1) & 0xFF
        if key == ord('q') or key == 27:
            log_msg("用户退出")
            raise KeyboardInterrupt


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 460800

    log_msg(f"=== uart_viewer ===  {port} @ {baud}")
    print("P4 Face Detection — USB Viewer", flush=True)
    print("按 q 或 ESC 退出", flush=True)

    ser = serial.Serial(port, baud, timeout=0.02)
    log_msg(f"已连接 (timeout={ser.timeout})")
    time.sleep(0.1)
    ser.reset_input_buffer()

    try:
        display_loop(ser)
    except KeyboardInterrupt:
        log_msg("退出")
    finally:
        ser.close()
        cv2.destroyAllWindows()


if __name__ == '__main__':
    main()
