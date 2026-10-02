/*
 * diag_log.c
 * ---------------------------------------------------------------------------
 * 诊断日志实现。无动态内存、无 printf：文本导出使用自带的小型格式化器。
 */
#include <string.h>
#include "diag_log.h"

static diag_log_entry_t *s_ring;
static uint32_t          s_capacity;
static uint32_t          s_head;      /* 下一个写入位置 */
static uint32_t          s_count;
static uint32_t          s_seq;
static uint32_t          s_dropped;
static uint8_t           s_level = H750_DIAG_LOG_LEVEL;
static diag_stats_t      s_stats;

int diag_log_init(void *storage, uint32_t entries)
{
    if ((storage == 0) || (entries == 0u)) {
        return -1;
    }
    s_ring = (diag_log_entry_t *)storage;
    s_capacity = entries;
    s_head = 0u;
    s_count = 0u;
    s_seq = 0u;
    s_dropped = 0u;
    (void)memset(s_ring, 0, (size_t)entries * sizeof(diag_log_entry_t));
    (void)memset(&s_stats, 0, sizeof(s_stats));
    s_stats.capacity = entries;
    return 0;
}

void diag_log_set_level(uint8_t level)
{
    s_level = level;
}

uint8_t diag_log_get_level(void)
{
    return s_level;
}

void diag_log_write(uint8_t level, uint16_t module, uint16_t code,
                    const void *payload, uint8_t len)
{
    diag_log_entry_t *e;

    if (s_ring == 0) {
        return;
    }
    s_stats.total++;
    if (level > s_level) {
        s_stats.dropped++;
        s_dropped++;
        return;
    }
    if (len > sizeof(e->payload)) {
        len = (uint8_t)sizeof(e->payload);
    }
    e = &s_ring[s_head];
    (void)memset(e, 0, sizeof(*e));
    e->seq = s_seq++;
    e->module = module;
    e->code = code;
    e->level = level;
    e->len = len;
    if ((payload != 0) && (len > 0u)) {
        (void)memcpy(e->payload, payload, len);
    }
    /* 时间戳由上层注入（hal_time_us），此处用序号占位以保证纯逻辑可测 */
    e->ts_us = e->seq;

    s_head = (s_head + 1u) % s_capacity;
    if (s_count < s_capacity) {
        s_count++;
    } else {
        s_stats.wraps++;
    }

    switch (level) {
    case H750_LOG_ERROR:
        s_stats.error++;
        s_stats.last_error_code = code;
        s_stats.last_error_module = module;
        s_stats.last_error_ts = e->ts_us;
        break;
    case H750_LOG_WARN:  s_stats.warn++;  break;
    case H750_LOG_INFO:  s_stats.info++;  break;
    default:             s_stats.debug++; break;
    }
    s_stats.count = s_count;
}

void diag_log_trace(uint16_t module, uint32_t trace_seq, uint32_t can_id,
                    uint8_t fifo, uint8_t result)
{
    uint8_t payload[9];
    payload[0] = (uint8_t)(trace_seq & 0xFFu);
    payload[1] = (uint8_t)((trace_seq >> 8) & 0xFFu);
    payload[2] = (uint8_t)((trace_seq >> 16) & 0xFFu);
    payload[3] = (uint8_t)((trace_seq >> 24) & 0xFFu);
    payload[4] = (uint8_t)(can_id & 0xFFu);
    payload[5] = (uint8_t)((can_id >> 8) & 0xFFu);
    payload[6] = (uint8_t)((can_id >> 16) & 0xFFu);
    payload[7] = fifo;
    payload[8] = result;
    diag_log_write(H750_LOG_DEBUG, module, 0x0900u, payload, 9u);
}

uint32_t diag_log_count(void)
{
    return s_count;
}

uint32_t diag_log_capacity(void)
{
    return s_capacity;
}

uint32_t diag_log_total(void)
{
    return s_stats.total;
}

uint32_t diag_log_dropped(void)
{
    return s_dropped;
}

int diag_log_get(uint32_t idx, diag_log_entry_t *out)
{
    uint32_t start;
    uint32_t pos;

    if ((s_ring == 0) || (out == 0) || (idx >= s_count)) {
        return -1;
    }
    start = (s_head + s_capacity - s_count) % s_capacity;
    pos = (start + idx) % s_capacity;
    *out = s_ring[pos];
    return 0;
}

void diag_log_get_stats(diag_stats_t *st)
{
    if (st == 0) {
        return;
    }
    *st = s_stats;
    st->count = s_count;
}

void diag_log_reset(void)
{
    s_head = 0u;
    s_count = 0u;
    s_seq = 0u;
    s_dropped = 0u;
    (void)memset(&s_stats, 0, sizeof(s_stats));
    s_stats.capacity = s_capacity;
    if (s_ring != 0) {
        (void)memset(s_ring, 0, (size_t)s_capacity * sizeof(diag_log_entry_t));
    }
}

const char *diag_module_name(uint16_t module)
{
    switch (module) {
    case DIAG_MOD_APP:   return "APP";
    case DIAG_MOD_CAN1:  return "CAN1";
    case DIAG_MOD_CAN2:  return "CAN2";
    case DIAG_MOD_ETH:   return "ETH";
    case DIAG_MOD_SD:    return "SD";
    case DIAG_MOD_LCD:   return "LCD";
    case DIAG_MOD_MPU:   return "MPU";
    case DIAG_MOD_CACHE: return "CACHE";
    case DIAG_MOD_POOL:  return "POOL";
    case DIAG_MOD_BSP:   return "BSP";
    default:             return "UNK";
    }
}

const char *diag_level_name(uint8_t level)
{
    switch (level) {
    case H750_LOG_ERROR: return "E";
    case H750_LOG_WARN:  return "W";
    case H750_LOG_INFO:  return "I";
    default:             return "D";
    }
}

/* ---- 自带小型格式化（不使用 printf，目标上无浮点/无库依赖） ------------- */
typedef struct {
    char    *buf;
    uint32_t cap;
    uint32_t len;
} text_out_t;

static void tx_char(text_out_t *t, char c)
{
    if ((t->len + 1u) < t->cap) {
        t->buf[t->len] = c;
        t->len++;
        t->buf[t->len] = '\0';
    }
}

static void tx_str(text_out_t *t, const char *s)
{
    while ((s != 0) && (*s != '\0')) {
        tx_char(t, *s);
        s++;
    }
}

static void tx_u32(text_out_t *t, uint32_t v, uint32_t width, int hex)
{
    char tmp[12];
    uint32_t n = 0u;
    const char *digits = hex ? "0123456789ABCDEF" : "0123456789";
    uint32_t base = hex ? 16u : 10u;

    if (v == 0u) {
        tmp[n++] = '0';
    }
    while (v > 0u) {
        tmp[n++] = digits[v % base];
        v /= base;
    }
    while ((n < width) && (n < sizeof(tmp))) {
        tmp[n++] = '0';
    }
    while (n > 0u) {
        n--;
        tx_char(t, tmp[n]);
    }
}

uint32_t diag_log_dump_text(char *buf, uint32_t cap, uint32_t max_entries)
{
    text_out_t t;
    uint32_t i;
    uint32_t n;

    if ((buf == 0) || (cap == 0u)) {
        return 0u;
    }
    t.buf = buf;
    t.cap = cap;
    t.len = 0u;
    buf[0] = '\0';

    tx_str(&t, "# H750 CAN-DAQ diag log, entries=");
    tx_u32(&t, s_count, 1u, 0);
    tx_str(&t, " wraps=");
    tx_u32(&t, s_stats.wraps, 1u, 0);
    tx_str(&t, " errors=");
    tx_u32(&t, s_stats.error, 1u, 0);
    tx_str(&t, "\r\n");

    n = (max_entries == 0u || max_entries > s_count) ? s_count : max_entries;
    for (i = 0u; i < n; i++) {
        diag_log_entry_t e;
        uint32_t k;
        if (diag_log_get(i, &e) != 0) {
            break;
        }
        tx_str(&t, "[");
        tx_u32(&t, e.ts_us, 8u, 0);
        tx_str(&t, "] ");
        tx_str(&t, diag_level_name(e.level));
        tx_str(&t, " ");
        tx_str(&t, diag_module_name(e.module));
        tx_str(&t, " code=0x");
        tx_u32(&t, e.code, 4u, 1);
        tx_str(&t, " seq=");
        tx_u32(&t, e.seq, 1u, 0);
        if (e.len > 0u) {
            tx_str(&t, " data=");
            for (k = 0u; k < e.len; k++) {
                tx_u32(&t, e.payload[k], 2u, 1);
                if ((k + 1u) < e.len) {
                    tx_char(&t, ',');
                }
            }
        }
        tx_str(&t, "\r\n");
    }
    return t.len;
}
