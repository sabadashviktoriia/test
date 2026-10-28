#include "metrics.h"

void metrics_evt_log(int id) { (void)id; }
void metrics_stats(void) {}
void metrics_stats_reset(void) {}
void metrics_evt_dump(void) {}
void metrics_evt_reset(void) {}
void metrics_stack_report(void) {}
void metrics_heap_report(void) {}
void metrics_load_report(void) {}
void metrics_probe_set(int enable) { (void)enable; }
int metrics_probe_enabled(void) { return 0; }