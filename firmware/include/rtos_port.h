/*
 * rtos_port.h
 * ---------------------------------------------------------------------------
 * 线程/事件抽象层。
 *
 * 目标构建：本层是 RT-Thread 的适配层（rt_thread_create / rt_event_* /
 *           rt_mb_* 的映射见 docs 的"线程与任务表"），由硬件中断、
 *           SysTick 与线程调度器驱动。
 * 仿真构建：提供确定性的协作式调度器，每个 tick 让就绪线程各执行一步，
 *           便于在 PC 上复现整条采集-解析-上报流水线并统计 CPU 占用。
 */
#ifndef H750_RTOS_PORT_H
#define H750_RTOS_PORT_H

#include <stdint.h>

#define RTOS_MAX_THREADS    8u
#define RTOS_TICK_MS        1u

typedef void (*rtos_thread_fn)(void *arg);

typedef struct {
    const char  *name;
    rtos_thread_fn fn;
    void        *arg;
    uint8_t      priority;     /* 数值越小优先级越高 */
    uint32_t     stack_bytes;  /* 栈深度（配置值） */
    uint8_t      used;
    uint8_t      ready;
    uint32_t     runs;         /* 被执行次数 */
    uint32_t     steps;        /* 线程内步进计数（用于 CPU 占用估算） */
    uint32_t     max_steps_per_tick;
} rtos_thread_t;

typedef struct {
    uint32_t bits;       /* 当前置位的事件 */
    uint32_t pending;    /* 未被消费的置位累计 */
    uint32_t sets;
    uint32_t waits;
} rtos_event_t;

int      rtos_init(void);
int      rtos_thread_create(const char *name, rtos_thread_fn fn, void *arg,
                            uint8_t priority, uint32_t stack_bytes);
void     rtos_thread_set_ready(uint32_t index, uint8_t ready);
void     rtos_start(void);
uint32_t rtos_run_ticks(uint32_t ticks);
void     rtos_yield(void);
uint32_t rtos_thread_count(void);
const rtos_thread_t *rtos_thread_at(uint32_t index);
const rtos_thread_t *rtos_thread_by_name(const char *name);
uint32_t rtos_ticks(void);
uint32_t rtos_total_steps(void);

void     rtos_event_init(rtos_event_t *e);
void     rtos_event_set(rtos_event_t *e, uint32_t bits);
uint32_t rtos_event_recv(rtos_event_t *e, uint32_t mask, int clear);
uint32_t rtos_event_peek(const rtos_event_t *e, uint32_t mask);
void     rtos_event_clear(rtos_event_t *e, uint32_t bits);

#endif /* H750_RTOS_PORT_H */
