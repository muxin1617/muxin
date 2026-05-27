# STM32CubeMX + Keil — 一步生成 FreeRTOS 工程

> 工具：STM32CubeMX (E:\CubeMX\) + Keil uVision5 ｜ 板子：STM32F103C8T6

---

## 第一步：CubeMX 创建工程（5分钟）

### 1.1 新建工程

打开 `E:\CubeMX\STM32CubeMX.exe`

**File → New Project** → 切到 **MCU Selector** 标签 → 搜索框输入 `STM32F103C8` → 双击选中

### 1.2 配置引脚和外设

在 **Pinout & Configuration** 页面：

| 标签 | 操作 | 说明 |
|------|------|------|
| **SYS** | Debug → Serial Wire | 让 ST-Link 能调试 |
| **RCC** | HSE → Crystal/Ceramic Resonator | 外部晶振（如果板子有 8MHz 晶振）。没有就跳过，用 HSI |
| **USART1** | Mode → Asynchronous | PA9=TX, PA10=RX 自动配好 |

### 1.3 使能 FreeRTOS

**Middleware → FREERTOS** → Interface 选 **CMSIS_V2** → 点 OK

> CMSIS_V2 是 ST 官方封装的 FreeRTOS API，跟标准 FreeRTOS 差不多但多了 HAL 集成，不用手动改中断。

### 1.4 配置时钟

切到 **Clock Configuration** 标签页：

- HCLK 输入 `72` 回车 → 系统自动算出 PLL 分频参数
- 最终确认 **HCLK = 72 MHz，APB1 = 36 MHz，APB2 = 72 MHz**

### 1.5 生成代码

**Project Manager** 标签页：
- Project Name：`SmartSensor`
- Project Location：`E:\projects\stm32_freertos\cubemx`
- Toolchain / IDE：**MDK-ARM**（即 Keil）
- 勾选 **Generate peripheral initialization as a pair of .c/.h files**

点右上角 **GENERATE CODE** → 等生成完成

---

## 第二步：Keil 打开工程（1分钟）

打开 `E:\projects\stm32_freertos\cubemx\SmartSensor\MDK-ARM\SmartSensor.uvprojx`

Keil 会自动打开工程，左边的文件树应该包含：
```
Application/User/
  ├── main.c
  ├── freertos.c
  ├── usart.c / gpio.c 等外设文件
Drivers/STM32F1xx_HAL_Driver/
Middlewares/FreeRTOS/
```

直接 **F7 编译** → 应该 0 错误 0 警告。

---

## 第三步：替换 main.c 为智能传感器代码（2分钟）

CubeMX 生成的 `main.c` 里已经有一个默认的 FreeRTOS 任务 `StartDefaultTask`。

**在 Keil 左侧找到 `Application/User/main.c`，全部替换为下面的代码：**

```c
#include "main.h"
#include "cmsis_os.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ===== printf 重定向到 USART1 ===== */
int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}

/* ===== 传感器数据结构和配置（跟 Windows 版一样） ===== */
#define SENSOR_QUEUE_LENGTH     32
#define FILTER_WINDOW_SIZE      5
#define ANOMALY_THRESHOLD       3.0f

typedef enum { SENSOR_ACCEL = 0, SENSOR_TEMP, SENSOR_LIGHT } SensorType;
typedef struct { SensorType type; uint32_t ts; float v[3]; uint8_t n; } SensorData_t;
typedef struct { SensorType type; uint32_t ts; float filt; float raw; uint8_t anom; } ProcData_t;
typedef struct { uint8_t anom_en; float anom_th; uint32_t mask; } Config_t;

static osMessageQueueId_t qa, qt, ql, qp;
static osMutexId_t mtx;
static Config_t cfg = {1, ANOMALY_THRESHOLD, 0x07};
static volatile uint32_t samples = 0, anoms = 0;

static float noise(float b, float a) { return b + (((float)rand()/RAND_MAX - 0.5f)*2*a); }
static float mavg(const float *b, uint8_t n) { float s=0; for(uint8_t i=0;i<n;i++)s+=b[i]; return s/n; }
static float stdev(const float *b, uint8_t n, float m) { float ss=0; for(uint8_t i=0;i<n;i++){float d=b[i]-m;ss+=d*d;} return sqrtf(ss/n); }

/* ===== 加速度计任务 (20Hz) ===== */
void AccelTask(void *arg) {
    for(;;) {
        osMutexAcquire(mtx, osWaitForever);
        uint8_t en = (cfg.mask & (1u<<SENSOR_ACCEL)) != 0;
        osMutexRelease(mtx);
        if(en) {
            SensorData_t d = {SENSOR_ACCEL, osKernelGetTickCount(), {noise(0,0.15f),noise(0,0.1f),noise(9.8f,0.05f)}, 3};
            osMessageQueuePut(qa, &d, 0, 0);
        }
        osDelay(50);
    }
}

/* ===== 温度任务 (10Hz) ===== */
void TempTask(void *arg) {
    static float base = 25.0f;
    for(;;) {
        osMutexAcquire(mtx, osWaitForever);
        uint8_t en = (cfg.mask & (1u<<SENSOR_TEMP)) != 0;
        osMutexRelease(mtx);
        if(en) { base += noise(0,0.01f);
            SensorData_t d = {SENSOR_TEMP, osKernelGetTickCount(), {noise(base,0.3f)}, 1};
            osMessageQueuePut(qt, &d, 0, 0);
        }
        osDelay(100);
    }
}

/* ===== 光照任务 (5Hz) ===== */
void LightTask(void *arg) {
    for(;;) {
        osMutexAcquire(mtx, osWaitForever);
        uint8_t en = (cfg.mask & (1u<<SENSOR_LIGHT)) != 0;
        osMutexRelease(mtx);
        if(en) {
            SensorData_t d = {SENSOR_LIGHT, osKernelGetTickCount(), {noise(500,50)}, 1};
            osMessageQueuePut(ql, &d, 0, 0);
        }
        osDelay(200);
    }
}

/* ===== 数据处理任务 ===== */
void ProcTask(void *arg) {
    float ha[5]={0}, ht[5]={0}, hl[5]={0};
    uint8_t ia=0, it=0, il=0;
    for(;;) {
        SensorData_t r; ProcData_t p;
        osMutexAcquire(mtx, osWaitForever);
        uint8_t ae = cfg.anom_en; float th = cfg.anom_th;
        osMutexRelease(mtx);
        while(osMessageQueueGet(qa, &r, NULL, 0) == osOK) {
            float mag = sqrtf(r.v[0]*r.v[0]+r.v[1]*r.v[1]+r.v[2]*r.v[2]);
            ha[ia]=mag; ia=(ia+1)%5; p.type=SENSOR_ACCEL; p.raw=mag;
            p.filt=mavg(ha,5); p.anom=0;
            if(ae){float m=p.filt,s=stdev(ha,5,m); if(fabsf(p.raw-m)>th*s){p.anom=1;anoms++;}}
            samples++; osMessageQueuePut(qp, &p, 0, 0);
        }
        while(osMessageQueueGet(qt, &r, NULL, 0) == osOK) {
            ht[it]=r.v[0]; it=(it+1)%5; p.type=SENSOR_TEMP; p.raw=r.v[0];
            p.filt=mavg(ht,5); p.anom=0;
            if(ae){float m=p.filt,s=stdev(ht,5,m); if(fabsf(p.raw-m)>th*s){p.anom=1;anoms++;}}
            samples++; osMessageQueuePut(qp, &p, 0, 0);
        }
        while(osMessageQueueGet(ql, &r, NULL, 0) == osOK) {
            hl[il]=r.v[0]; il=(il+1)%5; p.type=SENSOR_LIGHT; p.raw=r.v[0];
            p.filt=mavg(hl,5); p.anom=0;
            if(ae){float m=p.filt,s=stdev(hl,5,m); if(fabsf(p.raw-m)>th*s){p.anom=1;anoms++;}}
            samples++; osMessageQueuePut(qp, &p, 0, 0);
        }
        osDelay(50);
    }
}

/* ===== 监控任务 (1Hz) ===== */
void MonTask(void *arg) {
    char sb[1024];
    for(;;) {
        osThreadList(sb);
        printf("\n==== [t=%5lus] ====\n%s", osKernelGetTickCount()/1000, sb);
        printf("QA=%d QT=%d QL=%d QP=%d | S=%lu A=%lu(%.1f%%)\n",
            (int)osMessageQueueGetCount(qa), (int)osMessageQueueGetCount(qt),
            (int)osMessageQueueGetCount(ql), (int)osMessageQueueGetCount(qp),
            (unsigned long)samples, (unsigned long)anoms,
            samples?100.0f*(float)anoms/samples:0.0f);
        ProcData_t pd;
        while(osMessageQueueGet(qp, &pd, NULL, 0) == osOK)
            printf(" [%s] raw=%.3f filt=%.3f%s\n",
                pd.type==0?"ACC":pd.type==1?"TMP":"LIT",
                pd.raw, pd.filt, pd.anom?" ***ANOM***":"");
        osDelay(1000);
    }
}

/* ===== main：创建队列、互斥锁、启动任务 ===== */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    printf("\n+=== FreeRTOS Smart Sensor Hub | STM32F103 CubeMX ===+\n\n");
    srand(12345);

    /* 创建队列和互斥锁 */
    qa   = osMessageQueueNew(32, sizeof(SensorData_t), NULL);
    qt   = osMessageQueueNew(32, sizeof(SensorData_t), NULL);
    ql   = osMessageQueueNew(32, sizeof(SensorData_t), NULL);
    qp   = osMessageQueueNew(64, sizeof(ProcData_t), NULL);
    mtx  = osMutexNew(NULL);

    /* 创建任务 */
    const osThreadAttr_t attr = {.stack_size = 256, .priority = osPriorityNormal};
    osThreadNew(AccelTask, NULL, &attr);
    osThreadNew(TempTask,  NULL, &attr);
    osThreadNew(LightTask, NULL, &attr);
    osThreadNew(ProcTask,  NULL, &attr);
    osThreadNew(MonTask,   NULL, &attr);

    printf("Tasks created. Starting kernel...\n");
    osKernelStart();
    while(1) {}
}
```

---

## 第四步：编译下载

**F7 编译** → **F8 下载** → 串口助手（115200 波特率）看输出

如果报错 `osThreadList` 未定义，需要启用 FreeRTOS 的 `configUSE_TRACE_FACILITY` 和 `configUSE_STATS_FORMATTING_FUNCTIONS`。在 CubeMX 里：

**Middleware → FREERTOS → Config parameters** → 搜索 `trace` 和 `stats` → 把 `USE_TRACE_FACILITY`、`USE_STATS_FORMATTING_FUNCTIONS` 设为 Enabled → 重新生成代码。

---

## CubeMX vs 手动 SPL 对比

| | 手动 SPL（之前） | CubeMX + HAL（现在） |
|---|---|---|
| FreeRTOS 集成 | 手动加文件、改中断 | 勾一个框自动生成 |
| 时钟配置 | 手写 SystemClock_Config | 图形界面拖拽 |
| 外设初始化 | 手写 GPIO/USART 代码 | 勾选自动生成 |
| API 风格 | `xQueueCreate/xSemaphoreTake` | `osMessageQueueNew/osMutexAcquire`（CMSIS_V2 封装） |
| 学习曲线 | 陡 | 平缓 |
