#ifndef SUPER_LOOP_H
#define SUPER_LOOP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TASK_ID_UART_RX = 0,
    TASK_ID_SENSORS,
    TASK_ID_CAMERA,
    TASK_ID_FLASH_LOG,
    TASK_ID_HEARTBEAT,
    TASK_COUNT
} SuperLoop_TaskID_t;

typedef void (*TaskFunction_t)(void);

typedef struct {
    SuperLoop_TaskID_t id;
    const char*        name;
    TaskFunction_t     function;
    uint32_t           period_ms;
    uint32_t           last_run_ms;
    bool               enabled;
} SuperLoop_Task_t;

void SuperLoop_Init(void);
void SuperLoop_Run(void);
void SuperLoop_EnableTask(SuperLoop_TaskID_t task_id, bool enable);
uint32_t SuperLoop_TaskPeriodMs(SuperLoop_TaskID_t task_id);
void Metrics_OnLoopStart(void);
void Metrics_OnLoopEnd(void);
void Metrics_OnTaskStart(SuperLoop_TaskID_t task_id);
void Metrics_OnTaskEnd(SuperLoop_TaskID_t task_id);

#ifdef __cplusplus
}
#endif

#endif
