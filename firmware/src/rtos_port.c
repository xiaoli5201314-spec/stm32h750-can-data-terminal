/*
 * rtos_port.c
 * ---------------------------------------------------------------------------
 * 线程/事件抽象层实现（确定性协作式调度器）。
 *
 * 调度模型：每个 tick（1ms）按优先级顺序让所有就绪线程各执行"一步"，
 * 每步内部不做阻塞等待，因此流水线的时序在 PC 上完全可复现。
 * 目标构建时把 rtos_thread_create 映射为 RT-Thread 的线程创建、把
 * rtos_run_ticks 交给 SysTick 驱动。
 */
#include <string.h>
#include "rtos_port.h"
#include "hal_stub.h"

static rtos_thread_t s_threads[RTOS_MAX_THREADS];
static uint32_t      s_thread_count;
static uint32_t      s_ticks;
static uint8_t       s_started;

int rtos_init(void)
{
    (void)memset(s_threads, 0, sizeof(s_threads));
    s_thread_count = 0u;
    s_ticks = 0u;
    s_started = 0u;
    return 0;
}

int rtos_thread_create(const char *name, rtos_thread_fn fn, void *arg,
                       uint8_t priority, uint32_t stack_bytes)
{
    rtos_thread_t *t;
    if ((fn == 0) || (s_thread_count >= RTOS_MAX_THREADS)) {
        return -1;
    }
    t = &s_threads[s_thread_count];
    t->name = (name != 0) ? name : "thr";
    t->fn = fn;
    t->arg = arg;
    t->priority = priority;
    t->stack_bytes = stack_bytes;
    t->used = 1u;
    t->ready = 1u;
    t->runs = 0u;
    t->steps = 0u;
    t->max_steps_per_tick = 0u;
    s_thread_count++;
    return (int)(s_thread_count - 1u);
}

void rtos_thread_set_ready(uint32_t index, uint8_t ready)
{
    if (index < s_thread_count) {
        s_threads[index].ready = ready;
    }
}

void rtos_start(void)
{
    s_started = 1u;
}

void rtos_yield(void)
{
    /* 协作式调度下无额外动作；目标构建映射为 rt_thread_yield() */
}

/* 按优先级选择下一个可运行线程（数值小者优先；同优先级按索引轮转） */
static int pick_next(uint32_t ran_mask)
{
    uint32_t i;
    uint8_t best_prio = 0xFFu;
    int best = -1;

    for (i = 0u; i < s_thread_count; i++) {
        const rtos_thread_t *t = &s_threads[i];
        if ((t->used == 0u) || (t->ready == 0u)) {
            continue;
        }
        if ((ran_mask & (1u << i)) != 0u) {
            continue;              /* 本 tick 已执行过 */
        }
        if (t->priority < best_prio) {
            best_prio = t->priority;
            best = (int)i;
        }
    }
    return best;
}

uint32_t rtos_run_ticks(uint32_t ticks)
{
    uint32_t executed = 0u;
    uint32_t k;

    if (s_started == 0u) {
        return 0u;
    }
    for (k = 0u; k < ticks; k++) {
        uint32_t ran_mask = 0u;
        uint32_t steps_this_tick = 0u;
        int idx;

        /* 一个 tick 内让每个就绪线程各执行一步，按优先级从高到低 */
        while ((idx = pick_next(ran_mask)) >= 0) {
            rtos_thread_t *t = &s_threads[idx];
            t->fn(t->arg);
            t->runs++;
            t->steps++;
            ran_mask |= (1u << (uint32_t)idx);
            executed++;
            steps_this_tick++;
            if (steps_this_tick >= RTOS_MAX_THREADS) {
                break;
            }
        }
        s_ticks++;
        hal_delay_ms(RTOS_TICK_MS);   /* 推进 1ms 时基（目标上为等待到下一 tick） */
    }
    return executed;
}

uint32_t rtos_thread_count(void)
{
    return s_thread_count;
}

const rtos_thread_t *rtos_thread_at(uint32_t index)
{
    if (index >= s_thread_count) {
        return 0;
    }
    return &s_threads[index];
}

const rtos_thread_t *rtos_thread_by_name(const char *name)
{
    uint32_t i;
    if (name == 0) {
        return 0;
    }
    for (i = 0u; i < s_thread_count; i++) {
        if ((s_threads[i].name != 0) && (strncmp(s_threads[i].name, name, 16u) == 0)) {
            return &s_threads[i];
        }
    }
    return 0;
}

uint32_t rtos_ticks(void)
{
    return s_ticks;
}

uint32_t rtos_total_steps(void)
{
    uint32_t i;
    uint32_t n = 0u;
    for (i = 0u; i < s_thread_count; i++) {
        n += s_threads[i].steps;
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * 事件
 * ------------------------------------------------------------------------- */
void rtos_event_init(rtos_event_t *e)
{
    if (e == 0) {
        return;
    }
    e->bits = 0u;
    e->pending = 0u;
    e->sets = 0u;
    e->waits = 0u;
}

void rtos_event_set(rtos_event_t *e, uint32_t bits)
{
    if (e == 0) {
        return;
    }
    e->bits |= bits;
    e->pending |= bits;
    e->sets++;
}

uint32_t rtos_event_recv(rtos_event_t *e, uint32_t mask, int clear)
{
    uint32_t got;
    if (e == 0) {
        return 0u;
    }
    e->waits++;
    got = e->bits & mask;
    if ((got != 0u) && (clear != 0)) {
        e->bits &= ~got;
    }
    return got;
}

uint32_t rtos_event_peek(const rtos_event_t *e, uint32_t mask)
{
    if (e == 0) {
        return 0u;
    }
    return e->bits & mask;
}

void rtos_event_clear(rtos_event_t *e, uint32_t bits)
{
    if (e == 0) {
        return;
    }
    e->bits &= ~bits;
}
