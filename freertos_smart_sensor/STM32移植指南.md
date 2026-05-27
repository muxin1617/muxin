# STM32F103C8T6 FreeRTOS 移植指南

> 硬件：STM32F103C8T6 + ST-Link ｜ 软件：Keil uVision5 + STM32F1 Pack

---

## 前置检查（5分钟）

### ① 确认 Keil 装了 F1 芯片包

打开 Keil → 点工具栏的 **Pack Installer**（绿色芯片图标）→ 左侧搜 `STM32F1` → 确认 `Keil::STM32F1xx_DFP` 是 **Installed**（绿色）。

没装的话点 Install，等它下载完。

### ② 确认 ST-Link 驱动正常

插上 ST-Link → 设备管理器 → 通用串行总线设备 → 看到 `ST-Link Debug` 或 `STMicroelectronics STLink`

### ③ 确认板子能下载程序

打开 Keil → Project → New → 选 STM32F103C8 → 写个简单 main.c：

```c
#include "stm32f1xx_hal.h"
int main(void) {
    HAL_Init();
    while(1) {
        // 先不管，只要编译下载成功就行
    }
}
```

点 **Build（F7）** → **Download（F8）**。能下载进去就说明环境通了。

---

## Stage 1：裸机 LED 闪烁（30min）

> 目标：让 PC13 上的 LED 闪烁。这是嵌入式界的 Hello World。

### 创建 Keil 工程

1. **Project → New uVision Project** → 选 `E:\projects\stm32_freertos\stage1` → 芯片选 **STM32F103C8**
2. 弹窗选 CMSIS：勾选 **CMSIS → CORE**，勾选 **Device → Startup**
3. 弹窗 Manage Run-Time Environment 直接点 OK

### 写代码

把 main.c 替换为：

```c
#include "stm32f1xx_hal.h"

void SystemClock_Config(void);

int main(void) {
    HAL_Init();
    SystemClock_Config();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    while(1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);  // 500ms 闪烁
    }
}

void SystemClock_Config(void) {
    // 用 HSI 8MHz 内部时钟，不需要配置外部晶振
    // 大部分 F103C8T6 最小系统默认就是 HSI
}
```

### 编译下载

- **F7** 编译 → **F8** 下载
- PC13 的 LED 开始闪烁 → ✅ Stage 1 完成

---

## Stage 2：添加 FreeRTOS（30min）

> 目标：同一个 LED 闪烁，但用 FreeRTOS 任务替代 HAL_Delay

### 拷贝 FreeRTOS 内核文件

```
E:\projects\stm32_freertos\
├── stage1\              ← 刚才的裸机工程
├── stage2_freertos\     ← 新建这个
│   ├── src\
│   │   └── main.c
│   └── freertos\
│       ├── tasks.c      ← 从 freertos_kernel 拷贝
│       ├── list.c
│       ├── queue.c
│       ├── timers.c
│       ├── event_groups.c
│       ├── heap_4.c     ← 从 portable/MemMang/
│       ├── port.c       ← 从 portable/RVDS/ARM_CM3/
│       ├── FreeRTOSConfig.h  ← 新建
│       └── include\
│           ├── FreeRTOS.h
│           ├── task.h
│           ├── queue.h
│           ├── semphr.h
│           ├── timers.h
│           ├── event_groups.h
│           ├── portable.h
│           ├── StackMacros.h
│           ├── list.h
│           ├── mpu_wrappers.h
│           ├── projdefs.h
│           ├── atomic.h
│           ├── portable.h
│           ├── message_buffer.h
│           ├── stream_buffer.h
│           ├── croutine.h
│           ├── deprecated_definitions.h
│           └── newlib\n\     ← 从 portable/RVDS/ARM_CM3/ 拷贝 portmacro.h
│               └── portmacro.h
```

你不需要手动拷——我帮你：

在 CMD 里执行：

```cmd
mkdir E:\projects\stm32_freertos\freertos_kernel
robocopy E:\github\freertos_kernel E:\projects\stm32_freertos\freertos_kernel /E
```

### Keil 工程设置

1. 按 Stage 1 的方法创建新工程在 `stage2_freertos`
2. 把 `freertos_kernel/*.c` 和 `freertos_kernel/portable/RVDS/ARM_CM3/port.c` 和 `freertos_kernel/portable/MemMang/heap_4.c` 添加到工程
3. 添加 include 路径：`freertos_kernel/include` 和 `freertos_kernel/portable/RVDS/ARM_CM3`

### FreeRTOSConfig.h

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ              72000000
#define configTICK_RATE_HZ              1000
#define configMAX_PRIORITIES            7
#define configMINIMAL_STACK_SIZE        128
#define configTOTAL_HEAP_SIZE           ((size_t)(15 * 1024))
#define configMAX_TASK_NAME_LEN         16
#define configUSE_PREEMPTION            1
#define configIDLE_SHOULD_YIELD         1
#define configUSE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES   1
#define configUSE_RECURSIVE_MUTEXES     1
#define configUSE_TASK_NOTIFICATIONS    1
#define configUSE_TRACE_FACILITY        1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH        10
#define configTIMER_TASK_STACK_DEPTH    (configMINIMAL_STACK_SIZE * 2)
#define configQUEUE_REGISTRY_SIZE       10
#define configSUPPORT_STATIC_ALLOCATION 0

#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskDelay              1

#define configASSERT(x)
#define configCHECK_FOR_STACK_OVERFLOW  0

#endif
```

### main.c（FreeRTOS 版 LED 闪烁）

```c
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

void SystemClock_Config(void);

void vLEDTask(void *pv) {
    for(;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    xTaskCreate(vLEDTask, "LED", 128, NULL, 1, NULL);
    vTaskStartScheduler();

    while(1) {}  // never reach
}

void SystemClock_Config(void) {
    // HSI 8MHz
}
```

### 中断处理

在 `stm32f1xx_it.c` 里找到这两个函数，替换为：

```c
void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void PendSV_Handler(void) {
    xPortPendSVHandler();
}

void SVC_Handler(void) {
    vPortSVCHandler();
}
```

### 编译下载

- **F7** 编译 → 没报错的话 **F8** 下载
- LED 仍然闪烁 → ✅ Stage 2 完成

---

## Stage 3：移植智能传感器项目（30min）

> 目标：把我们 Windows 上跑通的 6 任务系统搬到 STM32 真机上

### 改动点

对比 Windows 版 main.c，STM32 版只有两处不同：

| 项目 | Windows 版 | STM32 版 |
|------|-----------|----------|
| 输出 | `printf(...)` | `printf(...)` 重定向到 USART1 |
| 初始化 | 只调 `vTaskStartScheduler()` | 先 `HAL_Init()` + 时钟 + USART 初始化 |

### USART printf 重定向

在 main.c 开头加：

```c
#include "stm32f1xx_hal.h"

// 重定向 printf 到 USART1（插上串口模块，波特率 115200）
int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}

UART_HandleTypeDef huart1;

void USART1_Init(void) {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // PA9 = TX, PA10 = RX
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&huart1);
}
```

### 完整 main.c 骨架

```c
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

void SystemClock_Config(void);
void USART1_Init(void);
UART_HandleTypeDef huart1;

int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}

// ===== 从这里往下，跟 Windows 版 main.c 一模一样 =====
// 把 Desktop\freertos_smart_sensor\main.c 的内容全粘过来
// 只需要改两行：
//   main() { 里的 srand(time(NULL));
//   改为    { 里先调 HAL_Init(); SystemClock_Config(); USART1_Init();
// ===== 以上，跟 Windows 版 main.c 一模一样 =====

int main(void) {
    HAL_Init();
    SystemClock_Config();
    USART1_Init();

    // 下面跟 Windows 版完全一样
    printf("\n+=== FreeRTOS Smart Sensor Hub v1.0 | CQU Intelligent Perception ===+\n\n");
    srand(HAL_GetTick());  // 改用 HAL tick

    // ... 其余全部照抄 ...
}

void SystemClock_Config(void) {
    // HSI 8MHz
}

// vApplicationMallocFailedHook 等保持不变
```

### 硬件连接

- ST-Link → 电脑（下载调试用）
- USB 转 TTL 模块 → PA9(TX) + PA10(RX) + GND（看串口输出用）
- 波特率：115200

### 串口查看

下载 **SSCOM** 或使用任意串口助手，选对 COM 口，波特率 115200，就能看到和 Windows 版一样的 Monitor 输出了。

---

## 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 编译报 `stm32f1xx_hal.h` not found | Keil 没加 HAL 库路径 | Options → C/C++ → Include Paths 加 `Drivers/STM32F1xx_HAL_Driver/Inc` |
| 下载后没反应 | 没按复位键 | 按一下板子上的 RESET 按钮 |
| 串口乱码 | 波特率不对 | 确认 115200，或改用 9600 |
| FreeRTOS 卡死 | 堆太小 | configTOTAL_HEAP_SIZE 改大（15KB→20KB） |
| HardFault | 中断没注册 | 检查 stm32f1xx_it.c 里的 SysTick/PendSV/SVC 替换了没 |

---

## 完成标志

串口助手输出：
```
+=== FreeRTOS Smart Sensor Hub v1.0 | CQU Intelligent Perception ===+

Starting scheduler...

==== [t=   1s] ====
Accel           B   2   253   1
Temp            B   2   253   2
...
QA=1 QT=1 QL=1 QP=35 | S=35 A=0(0.0%)
```

Windows 模拟器上能看到的东西，STM32 真机上完全一样——这就是移植成功的标志。面试时拿出双平台运行截图，比一万句话都有说服力。
