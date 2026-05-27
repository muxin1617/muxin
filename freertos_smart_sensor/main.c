#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <conio.h>

#define SENSOR_QUEUE_LENGTH     32
#define FILTER_WINDOW_SIZE      5
#define ANOMALY_THRESHOLD       3.0f

#define STACK_SENSOR            256
#define STACK_PROCESSOR         512
#define STACK_MONITOR           512
#define STACK_COMMAND           256

#define PRIORITY_SENSOR         (tskIDLE_PRIORITY + 2)
#define PRIORITY_PROCESSOR      (tskIDLE_PRIORITY + 3)
#define PRIORITY_MONITOR        (tskIDLE_PRIORITY + 1)
#define PRIORITY_COMMAND        (tskIDLE_PRIORITY + 4)

typedef enum { SENSOR_ACCEL = 0, SENSOR_TEMP, SENSOR_LIGHT } SensorType;
typedef struct { SensorType type; uint32_t ts; float v[3]; uint8_t n; } SensorData_t;
typedef struct { SensorType type; uint32_t ts; float filt; float raw; uint8_t anom; } ProcData_t;
typedef struct { uint8_t anom_en; float anom_th; uint32_t mask; } Config_t;

static QueueHandle_t qa, qt, ql, qp;
static SemaphoreHandle_t mtx;
static Config_t cfg = {1, ANOMALY_THRESHOLD, 0x07};
static volatile uint32_t samples = 0, anoms = 0;

static float noise(float b, float a) { return b + (((float)rand()/RAND_MAX - 0.5f)*2*a); }
static float mavg(const float *b, uint8_t n) { float s=0; for(uint8_t i=0;i<n;i++)s+=b[i]; return s/n; }
static float stdev(const float *b, uint8_t n, float m) { float ss=0; for(uint8_t i=0;i<n;i++){float d=b[i]-m;ss+=d*d;} return sqrtf(ss/n); }

static void accel_task(void *pv) {
    TickType_t last = xTaskGetTickCount();
    for(;;) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        uint8_t en = (cfg.mask & (1u<<SENSOR_ACCEL)) != 0;
        xSemaphoreGive(mtx);
        if(en) {
            SensorData_t d = {SENSOR_ACCEL, (uint32_t)xTaskGetTickCount(), {noise(0,0.15f),noise(0,0.1f),noise(9.8f,0.05f)}, 3};
            xQueueSend(qa, &d, 0);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
    }
}

static void temp_task(void *pv) {
    TickType_t last = xTaskGetTickCount();
    static float base = 25.0f;
    for(;;) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        uint8_t en = (cfg.mask & (1u<<SENSOR_TEMP)) != 0;
        xSemaphoreGive(mtx);
        if(en) { base += noise(0,0.01f);
            SensorData_t d = {SENSOR_TEMP, (uint32_t)xTaskGetTickCount(), {noise(base,0.3f)}, 1};
            xQueueSend(qt, &d, 0);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}

static void light_task(void *pv) {
    TickType_t last = xTaskGetTickCount();
    for(;;) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        uint8_t en = (cfg.mask & (1u<<SENSOR_LIGHT)) != 0;
        xSemaphoreGive(mtx);
        if(en) {
            SensorData_t d = {SENSOR_LIGHT, (uint32_t)xTaskGetTickCount(), {noise(500,50)}, 1};
            xQueueSend(ql, &d, 0);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(200));
    }
}

static void proc_task(void *pv) {
    TickType_t last = xTaskGetTickCount();
    float ha[5]={0}, ht[5]={0}, hl[5]={0};
    uint8_t ia=0, it=0, il=0;
    for(;;) {
        SensorData_t r; ProcData_t p;
        xSemaphoreTake(mtx, portMAX_DELAY);
        uint8_t ae = cfg.anom_en; float th = cfg.anom_th;
        xSemaphoreGive(mtx);
        while(xQueueReceive(qa, &r, 0) == pdPASS) {
            float mag = sqrtf(r.v[0]*r.v[0]+r.v[1]*r.v[1]+r.v[2]*r.v[2]);
            ha[ia]=mag; ia=(ia+1)%5; p.type=SENSOR_ACCEL; p.raw=mag;
            p.filt=mavg(ha,5); p.anom=0;
            if(ae){float m=p.filt,s=stdev(ha,5,m); if(fabsf(p.raw-m)>th*s){p.anom=1;anoms++;}}
            samples++; xQueueSend(qp, &p, 0);
        }
        while(xQueueReceive(qt, &r, 0) == pdPASS) {
            ht[it]=r.v[0]; it=(it+1)%5; p.type=SENSOR_TEMP; p.raw=r.v[0];
            p.filt=mavg(ht,5); p.anom=0;
            if(ae){float m=p.filt,s=stdev(ht,5,m); if(fabsf(p.raw-m)>th*s){p.anom=1;anoms++;}}
            samples++; xQueueSend(qp, &p, 0);
        }
        while(xQueueReceive(ql, &r, 0) == pdPASS) {
            hl[il]=r.v[0]; il=(il+1)%5; p.type=SENSOR_LIGHT; p.raw=r.v[0];
            p.filt=mavg(hl,5); p.anom=0;
            if(ae){float m=p.filt,s=stdev(hl,5,m); if(fabsf(p.raw-m)>th*s){p.anom=1;anoms++;}}
            samples++; xQueueSend(qp, &p, 0);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
    }
}

static void mon_task(void *pv) {
    TickType_t last = xTaskGetTickCount();
    for(;;) {
        char sb[512]; vTaskList(sb);
        printf("\n==== [t=%5lus] ====\n%s", (unsigned long)(xTaskGetTickCount()/1000), sb);
        printf("QA=%d QT=%d QL=%d QP=%d | S=%lu A=%lu(%.1f%%)\n",
            (int)uxQueueMessagesWaiting(qa), (int)uxQueueMessagesWaiting(qt),
            (int)uxQueueMessagesWaiting(ql), (int)uxQueueMessagesWaiting(qp),
            (unsigned long)samples, (unsigned long)anoms,
            samples?100.0f*(float)anoms/samples:0.0f);
        ProcData_t pd; int n=0;
        while(xQueueReceive(qp, &pd, 0)==pdPASS) {
            printf(" [%s] raw=%.3f filt=%.3f%s\n",
                pd.type==0?"ACC":pd.type==1?"TMP":"LIT",
                pd.raw, pd.filt, pd.anom?" ***ANOM***":"");
            n++;
        }
        xSemaphoreTake(mtx, portMAX_DELAY);
        printf("CFG: anom=%s th=%.1f mask=0x%02X\n",
            cfg.anom_en?"ON":"OFF", (double)cfg.anom_th, (unsigned)cfg.mask);
        xSemaphoreGive(mtx);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

static void cmd_task(void *pv) {
    printf("CMD: anomaly on/off | th N | sN on/off | status | quit\n");
    char buf[64]; int pos = 0;
    for(;;) {
        if(_kbhit()) {
            char c = (char)_getch();
            if(c == '\r' || c == '\n') {
                buf[pos] = '\0'; pos = 0;
                xSemaphoreTake(mtx, portMAX_DELAY);
                if(!strcmp(buf,"anomaly on")){cfg.anom_en=1;printf(">> Anom ON\n");}
                else if(!strcmp(buf,"anomaly off")){cfg.anom_en=0;printf(">> Anom OFF\n");}
                else if(!strncmp(buf,"th ",3)){float v=(float)atof(buf+3);if(v>0){cfg.anom_th=v;printf(">> Th=%.1f\n",(double)v);}}
                else if(!strncmp(buf,"s",1)&&strchr(buf,' ')){int id=atoi(buf+1);if(id>=0&&id<3){if(strstr(buf,"on")){cfg.mask|=1u<<id;printf(">> S%d ON\n",id);}else{cfg.mask&=~(1u<<id);printf(">> S%d OFF\n",id);}}}
                else if(!strcmp(buf,"status"))printf(">> anom=%s th=%.1f mask=0x%02X\n",cfg.anom_en?"ON":"OFF",(double)cfg.anom_th,(unsigned)cfg.mask);
                else if(!strcmp(buf,"quit")){printf(">> Bye!\n");xSemaphoreGive(mtx);exit(0);}
                else if(pos==0){} else printf(">> ?: %s\n",buf);
                xSemaphoreGive(mtx);
            } else if(c == '\b') { if(pos>0) pos--; }
            else if(pos < 63) { buf[pos++] = c; putchar(c); }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

int main(void) {
    printf("\n+=== FreeRTOS Smart Sensor Hub v1.0 | CQU Intelligent Perception ===+\n\n");
    srand((unsigned)time(NULL));
    qa = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData_t));
    qt = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData_t));
    ql = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorData_t));
    qp = xQueueCreate(64, sizeof(ProcData_t));
    mtx = xSemaphoreCreateMutex();
    xTaskCreate(accel_task, "Accel", STACK_SENSOR, NULL, PRIORITY_SENSOR, NULL);
    xTaskCreate(temp_task,  "Temp",  STACK_SENSOR, NULL, PRIORITY_SENSOR, NULL);
    xTaskCreate(light_task, "Light", STACK_SENSOR, NULL, PRIORITY_SENSOR, NULL);
    xTaskCreate(proc_task,  "Proc",  STACK_PROCESSOR, NULL, PRIORITY_PROCESSOR, NULL);
    xTaskCreate(mon_task,   "Mon",   STACK_MONITOR, NULL, PRIORITY_MONITOR, NULL);
    xTaskCreate(cmd_task,   "Cmd",   STACK_COMMAND, NULL, PRIORITY_COMMAND, NULL);
    printf("Starting scheduler...\n");
    vTaskStartScheduler();
    return 0;
}

void vApplicationMallocFailedHook(void) { printf("MALLOC FAIL!\n"); for(;;){} }
void vApplicationIdleHook(void) {}
void vApplicationStackOverflowHook(TaskHandle_t x, char *n) { printf("STACK OVF: %s!\n", n); for(;;){} }
