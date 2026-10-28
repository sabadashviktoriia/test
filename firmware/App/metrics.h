#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    METRICS_EVT_BTN_PRESS = 0,
    METRICS_EVT_BTN_REACT,
    METRICS_EVT_CMD_RX,
    METRICS_EVT_CMD_DONE,
    METRICS_EVT_CAPTURE_START,
    METRICS_EVT_CAPTURE_DONE,
    METRICS_EVT_FLASH_START,
    METRICS_EVT_FLASH_DONE,
    METRICS_EVT_USER0,
    METRICS_EVT_USER1,
    METRICS_EVT_COUNT
} metrics_evt_id_t;

void perf_init(void);
uint32_t perf_now(void);
void metrics_init(void);
void metrics_poll(void);
void metrics_stats(void);
void metrics_stats_reset(void);
void metrics_evt_log(int id);
void metrics_evt_dump(void);
void metrics_evt_reset(void);
void stack_paint(void);
void metrics_stack_report(void);
void metrics_heap_report(void);
void metrics_load_report(void);
void probe_on(int ch);
void probe_off(int ch);
void metrics_probe_set(int enable);
int metrics_probe_enabled(void);

#ifdef __cplusplus
}
#endif

#endif