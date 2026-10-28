#include "super_loop.h"
#include "main.h"

__attribute__((weak)) void Metrics_OnLoopStart(void) {}
__attribute__((weak)) void Metrics_OnLoopEnd(void) {}
__attribute__((weak)) void Metrics_OnTaskStart(SuperLoop_TaskID_t task_id) { (void)task_id; }
__attribute__((weak)) void Metrics_OnTaskEnd(SuperLoop_TaskID_t task_id) { (void)task_id; }

static void Task_ProcessUART(void) {
}

static void Task_ReadSensors(void) {
}

static void Task_CameraPoll(void) {
}

static void Task_FlashFlush(void) {
}

static void Task_HeartbeatLED(void) {
#ifdef LD2_GPIO_Port
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
#endif
}

static SuperLoop_Task_t g_tasks[TASK_COUNT] = {
    [TASK_ID_UART_RX]   = { TASK_ID_UART_RX,   "UART_Process", Task_ProcessUART,   0,   0, true },
    [TASK_ID_SENSORS]   = { TASK_ID_SENSORS,   "Sensors_Read", Task_ReadSensors,   100, 0, true },
    [TASK_ID_CAMERA]    = { TASK_ID_CAMERA,    "Camera_Poll",  Task_CameraPoll,    50,  0, true },
    [TASK_ID_FLASH_LOG] = { TASK_ID_FLASH_LOG, "Flash_Flush",  Task_FlashFlush,    500, 0, true },
    [TASK_ID_HEARTBEAT] = { TASK_ID_HEARTBEAT, "Heartbeat",    Task_HeartbeatLED,  500, 0, true },
};

void SuperLoop_Init(void) {
    uint32_t current_time = HAL_GetTick();
    
    for (int i = 0; i < TASK_COUNT; i++) {
        g_tasks[i].last_run_ms = current_time;
    }
}

void SuperLoop_EnableTask(SuperLoop_TaskID_t task_id, bool enable) {
    if (task_id < TASK_COUNT) {
        g_tasks[task_id].enabled = enable;
    }
}

uint32_t SuperLoop_TaskPeriodMs(SuperLoop_TaskID_t task_id) {
    return (task_id < TASK_COUNT) ? g_tasks[task_id].period_ms : 0;
}

void SuperLoop_Run(void) {
    while (1) {
        Metrics_OnLoopStart();
        
        uint32_t now = HAL_GetTick();

        for (int i = 0; i < TASK_COUNT; i++) {
            SuperLoop_Task_t *task = &g_tasks[i];

            if (!task->enabled) {
                continue;
            }

            if (task->period_ms == 0 || (now - task->last_run_ms >= task->period_ms)) {
                Metrics_OnTaskStart(task->id);
                
                task->function();
                
                Metrics_OnTaskEnd(task->id);

                task->last_run_ms = now;
            }
        }

        Metrics_OnLoopEnd();
    }
}
