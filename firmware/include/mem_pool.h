/*
 * mem_pool.h
 * ---------------------------------------------------------------------------
 * 定长内存池（RT-Thread 内存池思想的自研实现）：用于 CAN 帧、事件、上报包
 * 的固定规格分配，杜绝长时间运行下的内存碎片，保证分配时间可预期。
 *
 * 实现要点：
 *   - 每个块有 32 字节头部（magic / 状态 / 块号 / 空闲链）；
 *   - 空闲链表以"块号 + 1"编码，0 表示链尾，不依赖指针宽度；
 *   - 释放时校验 magic 与块号范围，可发现野指针与重复释放；
 *   - 分配时清零负载区，便于测试与协议解析的确定性。
 */
#ifndef H750_MEM_POOL_H
#define H750_MEM_POOL_H

#include <stdint.h>

#define MP_MAGIC_FREE     0x504F4F4Cu   /* "POOL" */
#define MP_MAGIC_ALLOC    0x504F4F41u   /* "POOA" */
#define MP_HDR_BYTES      32u
#define MP_HDR_WORDS      (MP_HDR_BYTES / 4u)
#define MP_STATE_FREE     0u
#define MP_STATE_ALLOC    1u

typedef struct {
    uint8_t    *base;          /* 池起始地址（8 字节对齐） */
    uint32_t    block_bytes;   /* 每块总字节（头部 + 负载，按 8 字节对齐） */
    uint32_t    payload_bytes; /* 可用负载字节 */
    uint32_t    block_count;
    uint32_t    free_head;     /* 空闲链头（块号 + 1，0 = 空链） */
    uint32_t    used;
    uint32_t    peak;
    uint32_t    alloc_calls;
    uint32_t    free_calls;
    uint32_t    alloc_fail;
    uint32_t    free_err;
    const char *name;
} mem_pool_t;

int      mp_init(mem_pool_t *p, void *mem, uint32_t payload_bytes,
                 uint32_t block_count, const char *name);
void    *mp_alloc(mem_pool_t *p);
int      mp_free(mem_pool_t *p, void *block);
uint32_t mp_used(const mem_pool_t *p);
uint32_t mp_free_blocks(const mem_pool_t *p);
uint32_t mp_block_count(const mem_pool_t *p);
uint32_t mp_payload_bytes(const mem_pool_t *p);
uint32_t mp_peak(const mem_pool_t *p);
uint32_t mp_alloc_fail_count(const mem_pool_t *p);
uint32_t mp_free_err_count(const mem_pool_t *p);
/* 遍历空闲链并校验每个块的 magic，返回 0 表示结构完整 */
int      mp_check_integrity(const mem_pool_t *p);
/* 由负载指针反推块号（越界返回 0xFFFFFFFF） */
uint32_t mp_block_index(const mem_pool_t *p, const void *payload);
void    *mp_payload_at(const mem_pool_t *p, uint32_t index);
void     mp_reset(mem_pool_t *p);

#endif /* H750_MEM_POOL_H */
