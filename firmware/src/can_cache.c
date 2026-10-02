/*
 * can_cache.c
 * ---------------------------------------------------------------------------
 * 分层缓存与批量上报实现。核心是"溢出决策"：永远优先牺牲低优先级数据。
 */
#include <string.h>
#include "can_cache.h"

/* ---------------------------------------------------------------------------
 * 内部工具
 * ------------------------------------------------------------------------- */
static can_cache_layer_t *layer_of(can_cache_t *c, can_prio_t p)
{
    switch (p) {
    case CAN_PRIO_HIGH: return &c->p0;
    case CAN_PRIO_MID:  return &c->p1;
    default:            return &c->p2;
    }
}

static const can_cache_layer_t *layer_of_c(const can_cache_t *c, can_prio_t p)
{
    switch (p) {
    case CAN_PRIO_HIGH: return &c->p0;
    case CAN_PRIO_MID:  return &c->p1;
    default:            return &c->p2;
    }
}

static uint32_t layer_oldest_index(const can_cache_layer_t *l)
{
    return (l->head + l->capacity - l->count) % l->capacity;
}

static int layer_push(can_cache_layer_t *l, const can_report_rec_t *rec)
{
    if (l->count >= l->capacity) {
        return -1;
    }
    l->slots[l->head] = *rec;
    l->head = (l->head + 1u) % l->capacity;
    l->count++;
    l->enqueued++;
    if (l->count > l->peak) {
        l->peak = l->count;
    }
    return 0;
}

static int layer_pop(can_cache_layer_t *l, can_report_rec_t *out)
{
    uint32_t idx;
    if (l->count == 0u) {
        return -1;
    }
    idx = layer_oldest_index(l);
    if (out != 0) {
        *out = l->slots[idx];
    }
    l->count--;
    l->dequeued++;
    return 0;
}

static int layer_evict_oldest(can_cache_layer_t *l)
{
    return layer_pop(l, 0);
}

/* ---------------------------------------------------------------------------
 * 初始化与统计
 * ------------------------------------------------------------------------- */
int can_cache_init(can_cache_t *c,
                   can_report_rec_t *p0, uint32_t p0_slots,
                   can_report_rec_t *p1, uint32_t p1_slots,
                   can_report_rec_t *p2, uint32_t p2_slots)
{
    if ((c == 0) || (p0 == 0) || (p1 == 0) || (p2 == 0)) {
        return -1;
    }
    if ((p0_slots == 0u) || (p1_slots == 0u) || (p2_slots == 0u)) {
        return -2;
    }
    (void)memset(c, 0, sizeof(*c));
    c->p0.slots = p0; c->p0.capacity = p0_slots;
    c->p1.slots = p1; c->p1.capacity = p1_slots;
    c->p2.slots = p2; c->p2.capacity = p2_slots;
    return 0;
}

void can_cache_reset(can_cache_t *c)
{
    if (c == 0) {
        return;
    }
    c->p0.head = 0u; c->p0.count = 0u;
    c->p1.head = 0u; c->p1.count = 0u;
    c->p2.head = 0u; c->p2.count = 0u;
    c->p0.enqueued = 0u; c->p0.dequeued = 0u; c->p0.peak = 0u;
    c->p1.enqueued = 0u; c->p1.dequeued = 0u; c->p1.peak = 0u;
    c->p2.enqueued = 0u; c->p2.dequeued = 0u; c->p2.peak = 0u;
    c->p0.dropped_low = 0u; c->p0.dropped_mid = 0u; c->p0.dropped_high = 0u;
    c->p1.dropped_low = 0u; c->p1.dropped_mid = 0u; c->p1.dropped_high = 0u;
    c->p2.dropped_low = 0u; c->p2.dropped_mid = 0u; c->p2.dropped_high = 0u;
    c->p0.evicted_low_for_higher = 0u;
    c->p1.evicted_low_for_higher = 0u;
    c->p2.evicted_low_for_higher = 0u;
    c->p0.enqueue_fail = 0u; c->p1.enqueue_fail = 0u; c->p2.enqueue_fail = 0u;
    c->total_enqueued = 0u;
    c->total_dropped = 0u;
    c->pack_calls = 0u;
    c->pack_bytes = 0u;
    c->last_pack_records = 0u;
    c->high_overflow_alarm = 0u;
}

uint32_t can_cache_count(const can_cache_t *c)
{
    if (c == 0) {
        return 0u;
    }
    return c->p0.count + c->p1.count + c->p2.count;
}

uint32_t can_cache_capacity(const can_cache_t *c)
{
    if (c == 0) {
        return 0u;
    }
    return c->p0.capacity + c->p1.capacity + c->p2.capacity;
}

uint32_t can_cache_capacity_of(const can_cache_t *c, can_prio_t prio)
{
    if (c == 0) {
        return 0u;
    }
    return layer_of_c(c, prio)->capacity;
}

uint32_t can_cache_count_of(const can_cache_t *c, can_prio_t prio)
{
    if (c == 0) {
        return 0u;
    }
    return layer_of_c(c, prio)->count;
}

uint32_t can_cache_watermark_pm(const can_cache_t *c)
{
    uint32_t cap = can_cache_capacity(c);
    if (cap == 0u) {
        return 0u;
    }
    return (can_cache_count(c) * 1000u) / cap;
}

int can_cache_should_flush(const can_cache_t *c)
{
    if (c == 0) {
        return 0;
    }
    if (c->p0.count > 0u) {
        return 1;   /* 高优先级数据存在就立即上报 */
    }
    if (can_cache_watermark_pm(c) >= H750_CACHE_FLUSH_WM_PM) {
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * 入队与溢出决策
 * ------------------------------------------------------------------------- */
int can_cache_push(can_cache_t *c, const can_report_rec_t *rec)
{
    can_cache_layer_t *l;
    can_prio_t prio;
    int sacrificed = 0;

    if ((c == 0) || (rec == 0)) {
        return -1;
    }
    prio = (can_prio_t)rec->prio;
    if (prio >= CAN_PRIO_COUNT) {
        prio = CAN_PRIO_LOW;
    }
    l = layer_of(c, prio);

    if (layer_push(l, rec) == 0) {
        c->total_enqueued++;
        return 0;
    }

    /* 本层已满：先按"丢低优先级保高优先级"的顺序牺牲更低优先级的数据 */
    if (prio == CAN_PRIO_LOW) {
        /* 低优先级：直接丢弃新数据，绝不动更高优先级 */
        c->p2.dropped_low++;
        c->p2.enqueue_fail++;
        c->total_dropped++;
        return -1;
    }

    if (prio == CAN_PRIO_MID) {
        if (layer_evict_oldest(&c->p2) == 0) {
            c->p2.dropped_low++;
            c->p2.evicted_low_for_higher++;
            c->total_dropped++;
            sacrificed = 1;
        }
    } else {
        /* 高优先级：优先牺牲低优先级，其次牺牲中优先级 */
        if (layer_evict_oldest(&c->p2) == 0) {
            c->p2.dropped_low++;
            c->p2.evicted_low_for_higher++;
            c->total_dropped++;
            sacrificed = 1;
        } else if (layer_evict_oldest(&c->p1) == 0) {
            c->p1.dropped_mid++;
            c->p1.evicted_low_for_higher++;
            c->total_dropped++;
            sacrificed = 1;
        }
    }

    /* 最后手段：本层已满且没有更低优先级数据可让，只能淘汰本层最老条目 */
    if (layer_evict_oldest(l) != 0) {
        c->total_dropped++;
        return -1;
    }
    if (prio == CAN_PRIO_MID) {
        c->p1.dropped_mid++;
    } else {
        c->p0.dropped_high++;
        c->high_overflow_alarm = 1u;   /* 三层都被高优先级占满，置严重标志 */
    }
    c->total_dropped++;

    if (layer_push(l, rec) != 0) {
        c->total_dropped++;
        return -1;
    }
    c->total_enqueued++;
    return sacrificed;
}

int can_cache_pop(can_cache_t *c, can_report_rec_t *out)
{
    if ((c == 0) || (out == 0)) {
        return -1;
    }
    if (layer_pop(&c->p0, out) == 0) {
        return 1;   /* 高优先级先出 */
    }
    if (layer_pop(&c->p1, out) == 0) {
        return 2;
    }
    if (layer_pop(&c->p2, out) == 0) {
        return 3;
    }
    return 0;
}

int can_cache_peek(const can_cache_t *c, can_report_rec_t *out)
{
    if ((c == 0) || (out == 0)) {
        return -1;
    }
    if (c->p0.count > 0u) {
        *out = c->p0.slots[layer_oldest_index(&c->p0)];
        return 1;
    }
    if (c->p1.count > 0u) {
        *out = c->p1.slots[layer_oldest_index(&c->p1)];
        return 2;
    }
    if (c->p2.count > 0u) {
        *out = c->p2.slots[layer_oldest_index(&c->p2)];
        return 3;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * 优先级判定（与报文 ID 规划表一致）
 * ------------------------------------------------------------------------- */
can_prio_t can_cache_prio_of_id(uint32_t can_id, uint8_t xtd)
{
    if (xtd != 0u) {
        if ((can_id >= 0x0A000000u) && (can_id <= 0x0A0000FFu)) {
            return CAN_PRIO_HIGH;   /* 私有扩展告警 */
        }
        if ((can_id >= 0x18FF0000u) && (can_id <= 0x18FFFFFFu)) {
            return CAN_PRIO_MID;    /* J1939 风格第三方设备 */
        }
        return CAN_PRIO_LOW;
    }
    if ((can_id >= 0x400u) && (can_id <= 0x47Fu)) {
        return CAN_PRIO_HIGH;       /* 报警事件 */
    }
    if ((can_id >= 0x580u) && (can_id <= 0x5FFu)) {
        return CAN_PRIO_HIGH;       /* 时间同步 SYNC */
    }
    if ((can_id >= 0x200u) && (can_id <= 0x27Fu)) {
        return CAN_PRIO_MID;        /* 诊断/心跳 */
    }
    if ((can_id >= 0x080u) && (can_id <= 0x1FFu)) {
        return CAN_PRIO_MID;        /* 伺服状态/传感器/执行器命令 */
    }
    if (can_id == H750_HEARTBEAT_ID) {
        return CAN_PRIO_MID;
    }
    if ((can_id >= 0x300u) && (can_id <= 0x37Fu)) {
        return CAN_PRIO_LOW;        /* 1kHz 高频观测数据 */
    }
    return CAN_PRIO_LOW;
}

/* ---------------------------------------------------------------------------
 * 批量打包 / 解包（TLV）
 * ------------------------------------------------------------------------- */
static uint32_t put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    return 2u;
}

static uint32_t put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
    return 4u;
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t can_cache_pack_tlv(can_cache_t *c, uint8_t *buf, uint32_t cap, uint32_t *out_len)
{
    uint32_t used = 0u;
    uint32_t records = 0u;
    can_report_rec_t rec;

    if ((c == 0) || (buf == 0) || (cap < (CAN_PACK_HEADER_BYTES + CAN_TLV_HEADER_BYTES + CAN_PACK_RECORD_BYTES))) {
        if (out_len != 0) {
            *out_len = 0u;
        }
        return 0u;
    }

    /* 报文头：版本 + 节点号 + 条目数（占位）+ 批次号 */
    buf[used++] = (uint8_t)H750_REPORT_VERSION;
    buf[used++] = (uint8_t)H750_NODE_ID;
    used += put_le16(&buf[used], 0u);
    used += put_le32(&buf[used], c->pack_calls);

    while (can_cache_peek(c, &rec) > 0) {
        uint32_t rec_len = CAN_PACK_RECORD_BYTES + (uint32_t)rec.dlc;
        uint32_t need = CAN_TLV_HEADER_BYTES + rec_len;
        if ((used + need) > cap) {
            break;   /* 本包已满，剩余条目留给下一包 */
        }
        (void)can_cache_pop(c, &rec);
        buf[used++] = CAN_TLV_TYPE_RECORD;
        buf[used++] = (uint8_t)rec_len;
        used += put_le32(&buf[used], rec.can_id);
        buf[used++] = rec.flags;
        buf[used++] = rec.dlc;
        buf[used++] = rec.bus;
        buf[used++] = rec.prio;
        used += put_le32(&buf[used], rec.ts_us);
        used += put_le32(&buf[used], rec.seq);
        if (rec.dlc > 0u) {
            (void)memcpy(&buf[used], rec.data, rec.dlc);
            used += rec.dlc;
        }
        records++;
    }

    (void)put_le16(&buf[2], (uint16_t)records);
    c->pack_calls++;
    c->pack_bytes += used;
    c->last_pack_records = records;
    if (out_len != 0) {
        *out_len = used;
    }
    return records;
}

uint32_t can_cache_unpack_tlv(const uint8_t *buf, uint32_t len,
                              can_report_rec_t *out, uint32_t max_out)
{
    uint32_t pos = CAN_PACK_HEADER_BYTES;
    uint32_t n = 0u;
    uint16_t declared;

    if ((buf == 0) || (out == 0) || (len < CAN_PACK_HEADER_BYTES)) {
        return 0u;
    }
    declared = get_le16(&buf[2]);
    while ((pos + CAN_TLV_HEADER_BYTES) <= len) {
        uint8_t type = buf[pos];
        uint8_t tlv_len = buf[pos + 1u];
        pos += CAN_TLV_HEADER_BYTES;
        if ((pos + tlv_len) > len) {
            break;
        }
        if ((type == CAN_TLV_TYPE_RECORD) && (tlv_len >= CAN_PACK_RECORD_BYTES) && (n < max_out)) {
            can_report_rec_t r;
            (void)memset(&r, 0, sizeof(r));
            r.can_id = get_le32(&buf[pos]);
            r.flags  = buf[pos + 4u];
            r.dlc    = buf[pos + 5u];
            r.bus    = buf[pos + 6u];
            r.prio   = buf[pos + 7u];
            r.ts_us  = get_le32(&buf[pos + 8u]);
            r.seq    = get_le32(&buf[pos + 12u]);
            if (r.dlc > H750_CAN_MAX_DATA) {
                r.dlc = H750_CAN_MAX_DATA;
            }
            if (r.dlc > 0u) {
                (void)memcpy(r.data, &buf[pos + CAN_PACK_RECORD_BYTES], r.dlc);
            }
            out[n] = r;
            n++;
        }
        pos += tlv_len;
    }
    if (n > declared) {
        n = declared;   /* 与头部声明不一致时以头部为准，便于发现截断 */
    }
    return n;
}
