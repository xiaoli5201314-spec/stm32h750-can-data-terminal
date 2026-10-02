/*
 * mem_pool.c
 * ---------------------------------------------------------------------------
 * 定长内存池实现。头部字段布局（每个字段 32 位，共 32 字节）：
 *   [0] magic
 *   [1] state
 *   [2] index
 *   [3] next（块号 + 1，0 表示链尾）
 *   [4..7] 保留（保证负载区 8 字节对齐）
 */
#include <string.h>
#include "mem_pool.h"

#define MP_OFF_MAGIC   0u
#define MP_OFF_STATE   1u
#define MP_OFF_INDEX   2u
#define MP_OFF_NEXT    3u

static uint32_t *hdr_of(void *payload)
{
    return (uint32_t *)(void *)(((uint8_t *)payload) - MP_HDR_BYTES);
}

static uint32_t *hdr_at(const mem_pool_t *p, uint32_t index)
{
    return (uint32_t *)(void *)(p->base + ((size_t)index * p->block_bytes));
}

static void hdr_set(uint32_t *h, uint32_t magic, uint32_t state,
                    uint32_t index, uint32_t next)
{
    h[MP_OFF_MAGIC] = magic;
    h[MP_OFF_STATE] = state;
    h[MP_OFF_INDEX] = index;
    h[MP_OFF_NEXT] = next;
}

int mp_init(mem_pool_t *p, void *mem, uint32_t payload_bytes,
            uint32_t block_count, const char *name)
{
    uint32_t i;
    uint32_t total;
    uint8_t *base = (uint8_t *)mem;

    if ((p == 0) || (mem == 0) || (payload_bytes == 0u) || (block_count == 0u)) {
        return -1;
    }
    /* 负载按 8 字节对齐，块大小 = 头部 + 对齐后的负载 */
    payload_bytes = (payload_bytes + 7u) & ~7u;
    total = MP_HDR_BYTES + payload_bytes;

    p->base = base;
    p->block_bytes = total;
    p->payload_bytes = payload_bytes;
    p->block_count = block_count;
    p->free_head = 0u;
    p->used = 0u;
    p->peak = 0u;
    p->alloc_calls = 0u;
    p->free_calls = 0u;
    p->alloc_fail = 0u;
    p->free_err = 0u;
    p->name = name;

    /* 构建空闲链：块 i 的 next 指向块 i+1（编码为 i+2），最后一块为 0 */
    for (i = 0u; i < block_count; i++) {
        uint32_t next = (i + 1u < block_count) ? (i + 2u) : 0u;
        hdr_set(hdr_at(p, i), MP_MAGIC_FREE, MP_STATE_FREE, i, next);
    }
    p->free_head = (block_count > 0u) ? 1u : 0u;
    return 0;
}

uint32_t mp_block_index(const mem_pool_t *p, const void *payload)
{
    uintptr_t off;
    uint32_t idx;

    if ((p == 0) || (payload == 0) || (p->base == 0)) {
        return 0xFFFFFFFFu;
    }
    if ((const uint8_t *)payload < p->base) {
        return 0xFFFFFFFFu;
    }
    off = (uintptr_t)((const uint8_t *)payload - p->base);
    if (off < MP_HDR_BYTES) {
        return 0xFFFFFFFFu;
    }
    idx = (uint32_t)((off - MP_HDR_BYTES) / p->block_bytes);
    if (idx >= p->block_count) {
        return 0xFFFFFFFFu;
    }
    return idx;
}

void *mp_payload_at(const mem_pool_t *p, uint32_t index)
{
    if ((p == 0) || (index >= p->block_count)) {
        return 0;
    }
    return (void *)(p->base + ((size_t)index * p->block_bytes) + MP_HDR_BYTES);
}

void *mp_alloc(mem_pool_t *p)
{
    uint32_t head;
    uint32_t idx;
    uint32_t *h;

    if (p == 0) {
        return 0;
    }
    p->alloc_calls++;
    head = p->free_head;
    if (head == 0u) {
        p->alloc_fail++;
        return 0;   /* 耗尽 */
    }
    idx = head - 1u;
    if (idx >= p->block_count) {
        p->alloc_fail++;
        return 0;
    }
    h = hdr_at(p, idx);
    if (h[MP_OFF_MAGIC] != MP_MAGIC_FREE) {
        p->alloc_fail++;
        return 0;   /* 池结构损坏 */
    }
    p->free_head = h[MP_OFF_NEXT];
    hdr_set(h, MP_MAGIC_ALLOC, MP_STATE_ALLOC, idx, 0u);

    p->used++;
    if (p->used > p->peak) {
        p->peak = p->used;
    }
    {
        void *payload = (void *)((uint8_t *)h + MP_HDR_BYTES);
        (void)memset(payload, 0, p->payload_bytes);   /* 分配即清零 */
        return payload;
    }
}

int mp_free(mem_pool_t *p, void *block)
{
    uint32_t idx;
    uint32_t *h;

    if ((p == 0) || (block == 0)) {
        return -1;
    }
    p->free_calls++;
    idx = mp_block_index(p, block);
    if (idx == 0xFFFFFFFFu) {
        p->free_err++;
        return -2;   /* 不是本池的块 */
    }
    h = hdr_of(block);
    if (h[MP_OFF_MAGIC] != MP_MAGIC_ALLOC) {
        p->free_err++;
        return -3;   /* magic 不匹配：重复释放或内存被踩 */
    }
    if (h[MP_OFF_INDEX] != idx) {
        p->free_err++;
        return -4;   /* 块号与地址不符 */
    }
    hdr_set(h, MP_MAGIC_FREE, MP_STATE_FREE, idx, p->free_head);
    p->free_head = idx + 1u;
    if (p->used > 0u) {
        p->used--;
    }
    return 0;
}

uint32_t mp_used(const mem_pool_t *p)
{
    return (p == 0) ? 0u : p->used;
}

uint32_t mp_free_blocks(const mem_pool_t *p)
{
    return (p == 0) ? 0u : (p->block_count - p->used);
}

uint32_t mp_block_count(const mem_pool_t *p)
{
    return (p == 0) ? 0u : p->block_count;
}

uint32_t mp_payload_bytes(const mem_pool_t *p)
{
    return (p == 0) ? 0u : p->payload_bytes;
}

uint32_t mp_peak(const mem_pool_t *p)
{
    return (p == 0) ? 0u : p->peak;
}

uint32_t mp_alloc_fail_count(const mem_pool_t *p)
{
    return (p == 0) ? 0u : p->alloc_fail;
}

uint32_t mp_free_err_count(const mem_pool_t *p)
{
    return (p == 0) ? 0u : p->free_err;
}

int mp_check_integrity(const mem_pool_t *p)
{
    uint32_t node;
    uint32_t guard = 0u;
    uint32_t free_cnt = 0u;

    if ((p == 0) || (p->base == 0)) {
        return -1;
    }
    node = p->free_head;
    while (node != 0u) {
        uint32_t idx = node - 1u;
        const uint32_t *h;
        if (idx >= p->block_count) {
            return -2;
        }
        h = hdr_at(p, idx);
        if (h[MP_OFF_MAGIC] != MP_MAGIC_FREE) {
            return -3;
        }
        if (h[MP_OFF_STATE] != MP_STATE_FREE) {
            return -4;
        }
        if (h[MP_OFF_INDEX] != idx) {
            return -5;
        }
        node = h[MP_OFF_NEXT];
        free_cnt++;
        guard++;
        if (guard > p->block_count) {
            return -6;   /* 环 */
        }
    }
    if (free_cnt != (p->block_count - p->used)) {
        return -7;   /* 链表长度与计数不符 */
    }
    return 0;
}

void mp_reset(mem_pool_t *p)
{
    uint32_t i;
    if (p == 0) {
        return;
    }
    for (i = 0u; i < p->block_count; i++) {
        uint32_t next = (i + 1u < p->block_count) ? (i + 2u) : 0u;
        hdr_set(hdr_at(p, i), MP_MAGIC_FREE, MP_STATE_FREE, i, next);
    }
    p->free_head = (p->block_count > 0u) ? 1u : 0u;
    p->used = 0u;
    p->peak = 0u;
    p->alloc_calls = 0u;
    p->free_calls = 0u;
    p->alloc_fail = 0u;
    p->free_err = 0u;
}
