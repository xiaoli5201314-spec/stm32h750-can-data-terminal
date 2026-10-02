/*
 * fdcan_filter.c
 * ---------------------------------------------------------------------------
 * FDCAN 过滤器分组：匹配语义、路由决策、寄存器编码与消息 RAM 编程。
 */
#include "fdcan_filter.h"
#include "fdcan_driver.h"
#include "hal_stub.h"

/* ---------------------------------------------------------------------------
 * 板级过滤器分组表
 *
 * FDCAN1（设备控制总线，500k 仲裁 / 2M 数据）
 *   G0 标准 范围  0x080~0x0FF -> FIFO0  伺服/变频器周期状态
 *   G1 标准 经典  0x100 掩码 0x7F0 -> FIFO0  传感器数据组（组播）
 *   G2 标准 双 ID 0x200/0x201  -> FIFO1  诊断/心跳关键帧
 *   G3 标准 范围  0x400~0x47F  -> FIFO1  报警事件（高优先级）
 *   G4 标准 双 ID 0x700/0x701  -> FIFO1  本机心跳（回环自检）
 *   G5 扩展 经典  0x18FF0000 掩码 0x1FFF0000 -> FIFO0  J1939 风格第三方设备
 *   G6 扩展 范围（不应用 XIDAM）0x0A000000~0x0A0000FF -> FIFO1 私有扩展告警
 *
 * FDCAN2（高频观测总线，1M 仲裁 / 5M 数据）
 *   G0 标准 范围  0x300~0x37F  -> FIFO0  1kHz 高频原始采样
 *   G1 标准 双 ID 0x580/0x581  -> FIFO1  时间同步 SYNC（最高优先级）
 *   G2 标准 范围  0x080~0x0FF  -> FIFO0  跨总线镜像数据
 *   G3 标准 双 ID 0x400/0x401  -> FIFO1  关键报警
 *   G4 扩展 经典  0x1FFFFFFF 掩码 0x1FFFFFFF -> FIFO1 精确匹配私有扩展告警
 *   G5 扩展 范围（不应用 XIDAM）0x0A000000~0x0A0000FF -> FIFO1 私有扩展
 * ------------------------------------------------------------------------- */
const fdcan_filter_rule_t h750_can1_std_filters[] = {
    { 0u, 0u, FDCAN_FTYPE_RANGE, FDCAN_FECFG_FIFO0,      0x080u, 0x0FFu, "G0 伺服/变频器周期状态" },
    { 1u, 0u, FDCAN_FTYPE_MASK,  FDCAN_FECFG_FIFO0,      0x100u, 0x7F0u, "G1 传感器数据组" },
    { 2u, 0u, FDCAN_FTYPE_DUAL,  FDCAN_FECFG_FIFO1_PRIO, 0x200u, 0x201u, "G2 诊断/心跳" },
    { 3u, 0u, FDCAN_FTYPE_RANGE, FDCAN_FECFG_FIFO1_PRIO, 0x400u, 0x47Fu, "G3 报警事件" },
    { 4u, 0u, FDCAN_FTYPE_DUAL,  FDCAN_FECFG_FIFO1,      0x700u, 0x701u, "G4 本机心跳回环" },
};
const uint32_t h750_can1_std_filter_count =
    (uint32_t)(sizeof(h750_can1_std_filters) / sizeof(h750_can1_std_filters[0]));

const fdcan_filter_rule_t h750_can1_ext_filters[] = {
    { 0u, 1u, FDCAN_FTYPE_MASK,          FDCAN_FECFG_FIFO0,      0x18FF0000u, 0x1FFF0000u, "G5 J1939 第三方设备" },
    { 1u, 1u, FDCAN_FTYPE_RANGE_NO_EIDM, FDCAN_FECFG_FIFO1_PRIO, 0x0A000000u, 0x0A0000FFu, "G6 私有扩展告警" },
};
const uint32_t h750_can1_ext_filter_count =
    (uint32_t)(sizeof(h750_can1_ext_filters) / sizeof(h750_can1_ext_filters[0]));

const fdcan_filter_rule_t h750_can2_std_filters[] = {
    { 0u, 0u, FDCAN_FTYPE_RANGE, FDCAN_FECFG_FIFO0,      0x300u, 0x37Fu, "G0 高频原始采样 1kHz" },
    { 1u, 0u, FDCAN_FTYPE_DUAL,  FDCAN_FECFG_FIFO1_PRIO, 0x580u, 0x581u, "G1 时间同步 SYNC" },
    { 2u, 0u, FDCAN_FTYPE_RANGE, FDCAN_FECFG_FIFO0,      0x080u, 0x0FFu, "G2 跨总线镜像数据" },
    { 3u, 0u, FDCAN_FTYPE_DUAL,  FDCAN_FECFG_FIFO1_PRIO, 0x400u, 0x401u, "G3 关键报警" },
};
const uint32_t h750_can2_std_filter_count =
    (uint32_t)(sizeof(h750_can2_std_filters) / sizeof(h750_can2_std_filters[0]));

const fdcan_filter_rule_t h750_can2_ext_filters[] = {
    { 0u, 1u, FDCAN_FTYPE_MASK,          FDCAN_FECFG_FIFO1_PRIO, 0x1FFFFFFFu, 0x1FFFFFFFu, "G4 精确匹配私有扩展告警" },
    { 1u, 1u, FDCAN_FTYPE_RANGE_NO_EIDM, FDCAN_FECFG_FIFO1,      0x0A000000u, 0x0A0000FFu, "G5 私有扩展" },
};
const uint32_t h750_can2_ext_filter_count =
    (uint32_t)(sizeof(h750_can2_ext_filters) / sizeof(h750_can2_ext_filters[0]));

/* 两个实例的过滤器配置（计数字段为编译期常量表达式，可直接用于静态初始化） */
const fdcan_filter_config_t h750_can1_filter_cfg = {
    h750_can1_std_filters,
    (uint32_t)(sizeof(h750_can1_std_filters) / sizeof(h750_can1_std_filters[0])),
    h750_can1_ext_filters,
    (uint32_t)(sizeof(h750_can1_ext_filters) / sizeof(h750_can1_ext_filters[0])),
    0x1FFFFFFFu,          /* XIDAM：全部 29 位参与比较 */
    2u, 2u,               /* 不匹配帧拒收（正常运行模式） */
    0u, 0u
};

const fdcan_filter_config_t h750_can2_filter_cfg = {
    h750_can2_std_filters,
    (uint32_t)(sizeof(h750_can2_std_filters) / sizeof(h750_can2_std_filters[0])),
    h750_can2_ext_filters,
    (uint32_t)(sizeof(h750_can2_ext_filters) / sizeof(h750_can2_ext_filters[0])),
    0x1FFFFFFFu,
    2u, 2u,
    0u, 0u
};

const fdcan_filter_config_t *fdcan_filter_config_can1(void)
{
    return &h750_can1_filter_cfg;
}

const fdcan_filter_config_t *fdcan_filter_config_can2(void)
{
    return &h750_can2_filter_cfg;
}

/* ---------------------------------------------------------------------------
 * 匹配语义
 * ------------------------------------------------------------------------- */
static uint32_t id_mask_of(uint8_t xtd)
{
    return (xtd != 0u) ? 0x1FFFFFFFu : 0x7FFu;
}

int fdcan_filter_rule_match(const fdcan_filter_rule_t *r, uint32_t id, uint8_t xtd)
{
    uint32_t mask;
    uint32_t a;
    uint32_t b;

    if (r == 0) {
        return 0;
    }
    if (r->id_type != xtd) {
        return 0;   /* 标准/扩展过滤器互不通用 */
    }
    if (r->fec == FDCAN_FECFG_DISABLE) {
        return 0;   /* 元素被禁用 */
    }
    mask = id_mask_of(xtd);
    id &= mask;

    switch (r->filter_type) {
    case FDCAN_FTYPE_DUAL:
        return ((id == (r->id1 & mask)) || (id == (r->id2 & mask))) ? 1 : 0;

    case FDCAN_FTYPE_MASK:
        /* 经典过滤：ID1 为过滤器，ID2 为掩码 */
        return (((id ^ r->id1) & r->id2 & mask) == 0u) ? 1 : 0;

    case FDCAN_FTYPE_RANGE:
        if (xtd != 0u) {
            /* 扩展帧范围过滤同时应用 XIDAM */
            a = (id & 0x1FFFFFFFu);
            b = (r->id1 & 0x1FFFFFFFu);
            {
                uint32_t m = 0x1FFFFFFFu;   /* 规则级 XIDAM 由路由函数传入，此处默认全掩码 */
                a &= m;
                b &= m;
            }
            return ((a >= b) && (a <= (r->id2 & 0x1FFFFFFFu))) ? 1 : 0;
        }
        return ((id >= (r->id1 & mask)) && (id <= (r->id2 & mask))) ? 1 : 0;

    case FDCAN_FTYPE_RANGE_NO_EIDM:
    default:
        return ((id >= (r->id1 & mask)) && (id <= (r->id2 & mask))) ? 1 : 0;
    }
}

static int route_of_fec(uint8_t fec, uint8_t *hp)
{
    switch (fec) {
    case FDCAN_FECFG_FIFO0:      *hp = 0u; return FDCAN_ROUTE_FIFO0;
    case FDCAN_FECFG_FIFO0_PRIO: *hp = 1u; return FDCAN_ROUTE_FIFO0;
    case FDCAN_FECFG_FIFO1:      *hp = 0u; return FDCAN_ROUTE_FIFO1;
    case FDCAN_FECFG_FIFO1_PRIO: *hp = 1u; return FDCAN_ROUTE_FIFO1;
    case FDCAN_FECFG_REJECT:     *hp = 0u; return FDCAN_ROUTE_REJECT;
    case FDCAN_FECFG_RXBUFFER:   *hp = 0u; return FDCAN_ROUTE_RXBUF;
    case FDCAN_FECFG_PRIORITY:   *hp = 1u; return FDCAN_ROUTE_FIFO0;
    default:                     *hp = 0u; return FDCAN_ROUTE_NONE;
    }
}

int fdcan_filter_match_index(const fdcan_filter_config_t *cfg, uint32_t id,
                             uint8_t xtd, uint8_t *hp_out)
{
    uint32_t i;
    if (cfg == 0) {
        return -1;
    }
    if (xtd == 0u) {
        for (i = 0u; i < cfg->std_count; i++) {
            if (fdcan_filter_rule_match(&cfg->std_rules[i], id, xtd) != 0) {
                uint8_t hp = 0u;
                (void)route_of_fec(cfg->std_rules[i].fec, &hp);
                if (hp_out != 0) {
                    *hp_out = hp;
                }
                return (int)i;
            }
        }
    } else {
        for (i = 0u; i < cfg->ext_count; i++) {
            int hit;
            const fdcan_filter_rule_t *r = &cfg->ext_rules[i];
            if ((r->filter_type == FDCAN_FTYPE_RANGE) &&
                (cfg->xidam != 0x1FFFFFFFu) && (r->fec != FDCAN_FECFG_DISABLE)) {
                /* 扩展帧范围过滤：报文 ID 与过滤器 ID 均先与 XIDAM 相与 */
                uint32_t m = cfg->xidam & 0x1FFFFFFFu;
                uint32_t a = id & m;
                uint32_t lo = r->id1 & m;
                uint32_t hi = r->id2 & m;
                hit = ((a >= lo) && (a <= hi)) ? 1 : 0;
            } else {
                hit = fdcan_filter_rule_match(r, id, xtd);
            }
            if (hit != 0) {
                uint8_t hp = 0u;
                (void)route_of_fec(cfg->ext_rules[i].fec, &hp);
                if (hp_out != 0) {
                    *hp_out = hp;
                }
                return (int)i;
            }
        }
    }
    return -1;
}

int fdcan_filter_route(const fdcan_filter_config_t *cfg, uint32_t id, uint8_t xtd,
                       uint8_t *hp_out)
{
    int idx;
    uint8_t hp = 0u;
    const fdcan_filter_rule_t *r;

    if (cfg == 0) {
        return FDCAN_ROUTE_NONE;
    }
    if (hp_out != 0) {
        *hp_out = 0u;
    }
    idx = fdcan_filter_match_index(cfg, id, xtd, &hp);
    if (idx >= 0) {
        r = (xtd == 0u) ? &cfg->std_rules[idx] : &cfg->ext_rules[idx];
        if (hp_out != 0) {
            *hp_out = hp;
        }
        return route_of_fec(r->fec, &hp);
    }

    /* 未命中任何过滤器：按全局过滤配置处理 */
    {
        uint8_t anf = (xtd == 0u) ? cfg->anfs : cfg->anfe;
        if (anf == 0u) {
            return FDCAN_ROUTE_FIFO0;   /* 监听/嗅探模式 */
        }
        if (anf == 1u) {
            return FDCAN_ROUTE_FIFO1;
        }
    }
    return FDCAN_ROUTE_REJECT;
}

/* ---------------------------------------------------------------------------
 * 寄存器编码
 * ------------------------------------------------------------------------- */
uint32_t fdcan_filter_encode_std(const fdcan_filter_rule_t *r)
{
    uint32_t w0 = 0u;
    if (r == 0) {
        return 0u;
    }
    w0 |= (((uint32_t)r->filter_type) << FDCAN_SFT_Pos) & FDCAN_SFT_Msk;
    w0 |= (((uint32_t)r->fec) << FDCAN_SFEC_Pos) & FDCAN_SFEC_Msk;
    w0 |= ((r->id1 & 0x7FFu) << FDCAN_SFID1_Pos) & FDCAN_SFID1_Msk;
    w0 |= (r->id2 & 0x7FFu) & FDCAN_SFID2_Msk;
    return w0;
}

void fdcan_filter_encode_ext(const fdcan_filter_rule_t *r, uint32_t *f0, uint32_t *f1)
{
    if (r == 0) {
        if (f0 != 0) { *f0 = 0u; }
        if (f1 != 0) { *f1 = 0u; }
        return;
    }
    if (f0 != 0) {
        *f0 = ((((uint32_t)r->fec) << FDCAN_EFEC_Pos) & FDCAN_EFEC_Msk) |
              (r->id1 & FDCAN_EFID1_Msk);
    }
    if (f1 != 0) {
        *f1 = ((((uint32_t)r->filter_type) << FDCAN_EFT_Pos) & FDCAN_EFT_Msk) |
              (r->id2 & FDCAN_EFID2_Msk);
    }
}

uint32_t fdcan_filter_gfc_value(const fdcan_filter_config_t *cfg)
{
    uint32_t v = 0u;
    if (cfg == 0) {
        return 0u;
    }
    if (cfg->rrfe != 0u) { v |= FDCAN_GFC_RRFE; }
    if (cfg->rrfs != 0u) { v |= FDCAN_GFC_RRFS; }
    v |= (((uint32_t)cfg->anfe) << FDCAN_GFC_ANFE_Pos) & FDCAN_GFC_ANFE_Msk;
    v |= (((uint32_t)cfg->anfs) << FDCAN_GFC_ANFS_Pos) & FDCAN_GFC_ANFS_Msk;
    return v;
}

uint32_t fdcan_filter_sidfc_value(uint32_t word_offset, uint32_t count)
{
    return (((word_offset << 2) & FDCAN_SIDFC_FLSSA_Msk)) |
           ((count << FDCAN_SIDFC_LSS_Pos) & FDCAN_SIDFC_LSS_Msk);
}

uint32_t fdcan_filter_xidfc_value(uint32_t word_offset, uint32_t count)
{
    return (((word_offset << 2) & FDCAN_XIDFC_FLESA_Msk)) |
           ((count << FDCAN_XIDFC_LSE_Pos) & FDCAN_XIDFC_LSE_Msk);
}

/* ---------------------------------------------------------------------------
 * 消息 RAM 编程
 * ------------------------------------------------------------------------- */
int fdcan_filter_program(uint32_t base, uint32_t msgram_base,
                         const fdcan_filter_config_t *cfg,
                         const struct fdcan_msgram_layout *lay)
{
    uint32_t i;
    volatile uint32_t *mram;

    if ((cfg == 0) || (lay == 0)) {
        return -1;
    }
    mram = (volatile uint32_t *)hal_mem_ptr(msgram_base, lay->total_words * 4u);
    if (mram == 0) {
        return -2;
    }

    /* 全局过滤配置与扩展 ID 掩码 */
    H750_REG32(base + FDCAN_GFC)   = fdcan_filter_gfc_value(cfg);
    H750_REG32(base + FDCAN_XIDAM) = cfg->xidam & FDCAN_XIDAM_EIDM_Msk;

    /* 标准过滤器区（每元素 1 word） */
    H750_REG32(base + FDCAN_SIDFC) =
        fdcan_filter_sidfc_value(lay->std_filter_words, cfg->std_count);
    for (i = 0u; i < cfg->std_count; i++) {
        mram[lay->std_filter_words + i] = fdcan_filter_encode_std(&cfg->std_rules[i]);
    }

    /* 扩展过滤器区（每元素 2 word） */
    H750_REG32(base + FDCAN_XIDFC) =
        fdcan_filter_xidfc_value(lay->ext_filter_words, cfg->ext_count);
    for (i = 0u; i < cfg->ext_count; i++) {
        uint32_t w0 = 0u;
        uint32_t w1 = 0u;
        uint32_t idx = lay->ext_filter_words + (i * FDCAN_EXT_FILTER_WORDS);
        fdcan_filter_encode_ext(&cfg->ext_rules[i], &w0, &w1);
        mram[idx]      = w0;
        mram[idx + 1u] = w1;
    }
    return 0;
}
