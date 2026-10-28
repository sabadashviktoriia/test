#include "main.h"
#include "metrics.h"
#include "super_loop.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

static uint32_t task_cnt[5];
static uint32_t task_min[5];
static uint32_t task_max[5];
static uint64_t task_sum[5];
static const char *task_n[5] = { "uart_rx", "sensors", "camera", "flash_log", "heartbeat" };
static uint32_t task_period_ms[5] = { 0, 100, 50, 500, 500 };

static uint32_t loop_cnt = 0;
static uint32_t loop_min = 0;
static uint32_t loop_max = 0;
static uint64_t loop_sum = 0;

static uint32_t btn_cnt = 0;
static uint32_t btn_min = 0;
static uint32_t btn_max = 0;
static uint64_t btn_sum = 0;

static uint32_t last_cyc = 0;
static uint64_t us_base = 0;
static uint32_t us_rem = 0;

static uint64_t task_start_us[5];
static int32_t jit_min[5];
static int32_t jit_max[5];
static uint32_t jit_miss[5];

static uint32_t busy = 0;
static uint32_t load_win = 0;
static uint32_t load_pct = 0;
static uint32_t load_avg = 0;
static uint32_t load_mx = 0;
static uint32_t load_n = 0;

static volatile uint32_t btn_t0 = 0;
static volatile uint8_t btn_flag = 0;

static uint32_t evt_t[128];
static uint8_t evt_id[128];
static uint32_t evt_wr = 0;
static uint32_t evt_rd = 0;
static uint32_t evt_drop = 0;

static const char *evt_n[8] = { "btn_press", "btn_react", "cmd_rx", "cmd_done",
    "capture_start", "capture_done", "flash_start", "flash_done" };

static int probe_en = 0;

static uint32_t task_t0[5];
static uint32_t loop_t0;

void perf_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t perf_now(void)
{
    return DWT->CYCCNT;
}

static void add_stat(int id, uint32_t cyc)
{
    if (task_cnt[id] == 0 || cyc < task_min[id]) task_min[id] = cyc;
    if (task_cnt[id] == 0 || cyc > task_max[id]) task_max[id] = cyc;
    task_sum[id] += cyc;
    task_cnt[id]++;
}

static uint32_t cyc_to_us(uint32_t cyc)
{
    return cyc / (SystemCoreClock / 1000000u);
}

static void print_stat(const char *name, uint32_t cnt, uint32_t mn, uint32_t mx, uint64_t sum)
{
    uint32_t avg = 0;
    if (cnt > 0) avg = (uint32_t)(sum / cnt);
    printf("STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n",
        name, (unsigned long)cnt, (unsigned long)cyc_to_us(mn),
        (unsigned long)cyc_to_us(avg), (unsigned long)cyc_to_us(mx));
}

uint64_t metrics_now_us(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    uint32_t raw = DWT->CYCCNT;
    uint32_t delta = raw - last_cyc;
    last_cyc = raw;
    us_rem += delta;
    us_base += us_rem / (SystemCoreClock / 1000000u);
    us_rem %= (SystemCoreClock / 1000000u);
    if (pm == 0) __enable_irq();
    return us_base;
}

void Metrics_OnTaskStart(SuperLoop_TaskID_t task_id)
{
    uint64_t now;
    int32_t period;
    int id = (int)task_id;

    if (id < 0 || id > 4) return;

    task_t0[id] = perf_now();
    now = metrics_now_us();
    period = task_period_ms[id] * 1000;

    if (task_start_us[id] != 0 && period > 0) {
        int32_t dt = (int32_t)(now - task_start_us[id]);
        int32_t j = dt - period;
        if (j < jit_min[id]) jit_min[id] = j;
        if (j > jit_max[id]) jit_max[id] = j;
        if (dt > 2 * period) jit_miss[id]++;
    }
    task_start_us[id] = now;

    if (probe_en && id < 4) probe_on(id);
}

void Metrics_OnTaskEnd(SuperLoop_TaskID_t task_id)
{
    uint32_t cyc;
    int id = (int)task_id;

    if (id < 0 || id > 4) return;

    cyc = perf_now() - task_t0[id];

    add_stat(id, cyc);
    busy += cyc;

    if (probe_en && id < 4) probe_off(id);
}

void Metrics_OnLoopStart(void)
{
    loop_t0 = perf_now();
}

void Metrics_OnLoopEnd(void)
{
    uint32_t cyc = perf_now() - loop_t0;
    if (loop_cnt == 0 || cyc < loop_min) loop_min = cyc;
    if (loop_cnt == 0 || cyc > loop_max) loop_max = cyc;
    loop_sum += cyc;
    loop_cnt++;
    metrics_poll();
}

void metrics_poll(void)
{
    uint32_t w;

    (void)metrics_now_us();

    w = perf_now() - load_win;
    if (w >= SystemCoreClock) {
        load_pct = (uint32_t)((busy * 100u) / w);
        load_avg += load_pct;
        load_n++;
        if (load_pct > load_mx) load_mx = load_pct;
        busy = 0;
        load_win = perf_now();
    }

    if (btn_flag) {
        uint32_t d = perf_now() - btn_t0;
        btn_flag = 0;
        if (btn_cnt == 0 || d < btn_min) btn_min = d;
        if (btn_cnt == 0 || d > btn_max) btn_max = d;
        btn_sum += d;
        btn_cnt++;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == B1_Pin) {
        btn_t0 = perf_now();
        btn_flag = 1;
        metrics_evt_log(METRICS_EVT_BTN_PRESS);
    }
}

void metrics_evt_log(int id)
{
    uint32_t pm;
    uint64_t t;
    uint32_t slot;

    if (id < 0 || id > 7) return;

    pm = __get_PRIMASK();
    __disable_irq();
    t = metrics_now_us();
    slot = evt_wr % 128;
    evt_t[slot] = (uint32_t)t;
    evt_id[slot] = (uint8_t)id;
    evt_wr++;
    if (evt_wr - evt_rd > 128) {
        evt_rd++;
        evt_drop++;
    }
    if (pm == 0) __enable_irq();
}

void metrics_evt_dump(void)
{
    uint32_t i;

    printf("--- EVT n=%lu dropped=%lu ---\r\n",
        (unsigned long)(evt_wr - evt_rd), (unsigned long)evt_drop);
    for (i = evt_rd; i != evt_wr; i++) {
        uint32_t slot = i % 128;
        printf("EVT t_us=%lu name=%s\r\n",
            (unsigned long)evt_t[slot], evt_n[evt_id[slot]]);
    }
    printf("--- EVT end ---\r\n");
}

void metrics_evt_reset(void)
{
    evt_rd = evt_wr;
    evt_drop = 0;
}

static GPIO_TypeDef *probe_port[4] = { GPIOA, GPIOB, GPIOB, GPIOB };
static uint16_t probe_pin[4] = { GPIO_PIN_4, GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_10 };

void probe_on(int ch)
{
    if (ch >= 0 && ch < 4) {
        probe_port[ch]->BSRR = probe_pin[ch];
    }
}

void probe_off(int ch)
{
    if (ch >= 0 && ch < 4) {
        probe_port[ch]->BSRR = ((uint32_t)probe_pin[ch]) << 16;
    }
}

void metrics_probe_set(int enable)
{
    probe_en = enable ? 1 : 0;
}

int metrics_probe_enabled(void)
{
    return probe_en;
}

extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;
extern uint32_t _Min_Heap_Size;

static uint32_t *stack_bottom(void)
{
    return (uint32_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size);
}

void stack_paint(void)
{
    uint32_t *p = stack_bottom();
    uint32_t sp = __get_MSP();

    while (p < (uint32_t *)(sp - 16u)) {
        *p = 0xDEADBEEFu;
        p++;
    }
}

uint32_t metrics_stack_used_bytes(void)
{
    uint32_t *p = stack_bottom();

    while (p < &_estack && *p == 0xDEADBEEFu) {
        p++;
    }
    return (uint32_t)&_estack - (uint32_t)p;
}

void metrics_stack_report(void)
{
    uint32_t reserved = (uint32_t)&_Min_Stack_Size;
    uint32_t used = metrics_stack_used_bytes();

    printf("STACK reserved=%lu used=%lu free=%lu\r\n",
        (unsigned long)reserved, (unsigned long)used,
        (unsigned long)(reserved - used));
}

void metrics_heap_report(void)
{
    uint32_t reserved = (uint32_t)&_Min_Heap_Size;

    printf("HEAP reserved=%lu used=0\r\n", (unsigned long)reserved);
}

void metrics_stats(void)
{
    int i;

    printf("--- METRICS fw=%s clk=%lu Hz ---\r\n",
        FW_VERSION, (unsigned long)SystemCoreClock);

    for (i = 0; i < 5; i++) {
        if (task_cnt[i] > 0) {
            print_stat(task_n[i], task_cnt[i], task_min[i], task_max[i], task_sum[i]);
        }
    }

    if (loop_cnt > 0) {
        print_stat("loop", loop_cnt, loop_min, loop_max, loop_sum);
    }

    if (btn_cnt > 0) {
        print_stat("btn_latency", btn_cnt, btn_min, btn_max, btn_sum);
    }

    for (i = 0; i < 5; i++) {
        if (task_period_ms[i] > 0) {
            printf("JITTER name=%s period_ms=%lu min_us=%ld max_us=%ld missed=%lu\r\n",
                task_n[i], (unsigned long)task_period_ms[i],
                (long)jit_min[i], (long)jit_max[i], (unsigned long)jit_miss[i]);
        }
    }
}

void metrics_stats_reset(void)
{
    int i;

    for (i = 0; i < 5; i++) {
        task_cnt[i] = 0;
        task_min[i] = 0;
        task_max[i] = 0;
        task_sum[i] = 0;
        task_start_us[i] = 0;
        jit_min[i] = INT32_MAX;
        jit_max[i] = INT32_MIN;
        jit_miss[i] = 0;
    }
    loop_cnt = 0;
    loop_min = 0;
    loop_max = 0;
    loop_sum = 0;
    btn_cnt = 0;
    btn_min = 0;
    btn_max = 0;
    btn_sum = 0;
    busy = 0;
    load_pct = 0;
    load_avg = 0;
    load_mx = 0;
    load_n = 0;
    load_win = perf_now();
}

void metrics_load_report(void)
{
    uint32_t avg = 0;
    if (load_n > 0) avg = load_avg / load_n;

    printf("LOAD pct=%lu avg=%lu max=%lu n=%lu\r\n",
        (unsigned long)load_pct, (unsigned long)avg,
        (unsigned long)load_mx, (unsigned long)load_n);
}

void metrics_init(void)
{
    int i;
    GPIO_InitTypeDef g;

    memset(&g, 0, sizeof(g));
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    for (i = 0; i < 4; i++) {
        g.Pin = probe_pin[i];
        HAL_GPIO_Init(probe_port[i], &g);
        HAL_GPIO_WritePin(probe_port[i], probe_pin[i], GPIO_PIN_RESET);
    }

    for (i = 0; i < 5; i++) {
        jit_min[i] = INT32_MAX;
        jit_max[i] = INT32_MIN;
    }

    (void)metrics_now_us();
    load_win = perf_now();

    printf("METRICS fw=%s clk=%lu Hz probes=PA4,PB0,PB1,PB10\r\n",
        FW_VERSION, (unsigned long)SystemCoreClock);
}