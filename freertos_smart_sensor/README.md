# FreeRTOS 智能传感器多任务采集系统

基于 **FreeRTOS Windows Simulator** 构建的多传感器数据采集与异常检测系统。模拟 STM32 嵌入式环境下的多任务实时处理架构。

## 项目概述

模拟一套智能传感器集线器，具备三个独立任务：

| 任务 | 优先级 | 功能 |
|------|--------|------|
| SensorTask | 中 | 周期性采集加速度、温度、光照三类传感器数据（模拟） |
| ProcessorTask | 高 | 滑动窗口均值滤波 + 异常值检测，通过队列接收原始数据 |
| MonitorTask | 低 | 轮询系统状态、任务栈高水位、队列水位，发现异常报警 |
| CommandTask | 最高 | 接收键盘指令，动态调整配置（异常阈值、传感器开关） |

## 技术要点

- **RTOS 多任务设计**：抢占式调度，根据实时性需求分配 4 级优先级
- **任务间通信**：使用 FreeRTOS Queue 传递传感器数据，任务通知触发处理
- **内存管理**：`heap_4.c` 首次适应算法，支持碎片合并
- **实时监控**：任务栈高水位检测（`uxTaskGetStackHighWaterMark`）、队列水位监控

## 快速运行

```bash
# 需要 MinGW-w64
build.bat         # 编译
build.bat run     # 编译 + 运行
```

或手动编译：

```bash
gcc -O0 -g3 -D_WIN32_WINNT=0x0601 \
    -I. -I../freertos_kernel/include -I../freertos_kernel/portable/MSVC-MingW \
    -o build/smart_sensor.exe \
    main.c \
    ../freertos_kernel/tasks.c ../freertos_kernel/list.c ../freertos_kernel/queue.c \
    ../freertos_kernel/timers.c ../freertos_kernel/event_groups.c ../freertos_kernel/stream_buffer.c \
    ../freertos_kernel/portable/MSVC-MingW/port.c ../freertos_kernel/portable/MemMang/heap_4.c \
    -lwinmm
```

## 运行效果

```
=== FreeRTOS Smart Sensor Hub ===
[Sensor]  Accel  | x=-0.12 y=+9.81 z=+0.03
[Process] Accel  | filt=+9.81 raw=+9.81 OK
[Sensor]  Temp   | val=36.5 C
[Process] Temp   | filt=36.3 raw=36.5 OK
[Monitor] Sys OK | Q:12% Stack:(320/256/512/256)
```

## 项目文件

```
freertos_smart_sensor/
├── main.c                    # 主程序（多任务逻辑）
├── FreeRTOSConfig.h          # FreeRTOS 内核配置
├── build.bat                 # MinGW 编译脚本
├── CubeMX工程指南.md          # STM32CubeMX + FreeRTOS 配置笔记
├── STM32移植指南.md           # ARM Compiler 5→6 移植经验
├── 基础知识补习.md             # FreeRTOS 核心概念笔记
├── 面试材料包.md              # STAR 项目描述 + Q&A
└── build/                    # 编译输出（需自行编译）
```

## 相关项目

- STM32F103 硬件版本：基于 STM32F103C8T6 + Keil MDK + HAL 库，通过 CubeMX 生成，已适配 ARM Compiler 6 (Clang)
- FreeRTOS 内核：需自行下载至 `../freertos_kernel/`

## 作者

- GitHub: [muxin1617](https://github.com/muxin1617)
- 重庆大学 · 智能感知专业 · 2024级
