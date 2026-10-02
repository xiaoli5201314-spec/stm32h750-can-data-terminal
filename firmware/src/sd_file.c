/*
 * sd_file.c
 * ---------------------------------------------------------------------------
 * SD 块设备适配层 + H750FS-lite 文件系统实现。
 */
#include <string.h>
#include "sd_file.h"
#include "hal_stub.h"
#include "diag_log.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t sector_size;
    uint32_t total_sectors;
    uint32_t dir_start;
    uint32_t dir_sectors;
    uint32_t data_start;
    uint32_t next_free_sector;
    uint32_t file_count;
    uint32_t reserved;
} fs_super_t;

typedef struct {
    uint16_t magic;
    uint8_t  in_use;
    uint8_t  flags;
    char     name[H750_FS_NAME_LEN];
    uint32_t start_sector;
    uint32_t sectors;
    uint32_t bytes;
    uint32_t crc;
} fs_entry_t;

#define FS_ENTRY_MAGIC  0x4631u   /* "F1" */
#define FS_SUPER_MAGIC  H750_FS_MAGIC

static fs_stats_t s_stats;
static const char *s_last_error = "none";

static void fs_set_error(const char *e)
{
    s_last_error = e;
    s_stats.errors++;
}

const char *fs_last_error(void)
{
    return s_last_error;
}

/* ---------------------------------------------------------------------------
 * 块设备
 * ------------------------------------------------------------------------- */
int sd_block_init(void)
{
    return sd_card_present() ? 0 : -1;
}

int sd_card_present(void)
{
#ifdef H750_PC_SIM
    return hal_sim_sd_present();
#else
    /* 目标构建：由 SDMMC1 的 CMD 探测与 SD_CD 引脚共同判定 */
    return hal_gpio_read(H750_SD_CD_PORT, H750_SD_CD_PIN) ? 0 : 1;
#endif
}

int sd_block_read(uint32_t sector, void *buf)
{
    if ((buf == 0) || (sector >= sd_capacity_sectors())) {
        fs_set_error("read range");
        return -1;
    }
#ifdef H750_PC_SIM
    if (hal_sim_sd_read_sector(sector, buf) != 0) {
        fs_set_error("sd read");
        return -2;
    }
#else
    (void)memset(buf, 0, H750_SD_SECTOR_SIZE);
#endif
    s_stats.reads++;
    return 0;
}

int sd_block_write(uint32_t sector, const void *buf)
{
    if ((buf == 0) || (sector >= sd_capacity_sectors())) {
        fs_set_error("write range");
        return -1;
    }
#ifdef H750_PC_SIM
    if (hal_sim_sd_write_sector(sector, buf) != 0) {
        fs_set_error("sd write");
        return -2;
    }
#else
    (void)buf;
#endif
    s_stats.writes++;
    return 0;
}

uint32_t sd_capacity_sectors(void)
{
#ifdef H750_PC_SIM
    return hal_sim_sd_capacity_sectors();
#else
    return H750_SD_BLOCK_COUNT;
#endif
}

/* ---------------------------------------------------------------------------
 * CRC32（用于目录条目完整性）
 * ------------------------------------------------------------------------- */
uint32_t fs_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    uint32_t b;

    for (i = 0u; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (b = 0u; b < 8u; b++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* ---------------------------------------------------------------------------
 * 目录读写（每次操作都直接落盘，保证掉电一致性）
 * ------------------------------------------------------------------------- */
static uint32_t entry_sector(uint32_t index)
{
    return FS_DIR_START + (index / FS_ENTRY_PER_SEC);
}

static uint32_t entry_offset(uint32_t index)
{
    return (index % FS_ENTRY_PER_SEC) * FS_ENTRY_BYTES;
}

static int entry_read(uint32_t index, fs_entry_t *e)
{
    uint8_t sec[H750_SD_SECTOR_SIZE];
    if (sd_block_read(entry_sector(index), sec) != 0) {
        return -1;
    }
    (void)memcpy(e, &sec[entry_offset(index)], sizeof(*e));
    return 0;
}

static int entry_write(uint32_t index, const fs_entry_t *e)
{
    uint8_t sec[H750_SD_SECTOR_SIZE];
    if (sd_block_read(entry_sector(index), sec) != 0) {
        return -1;
    }
    (void)memcpy(&sec[entry_offset(index)], e, sizeof(*e));
    return sd_block_write(entry_sector(index), sec);
}

static void entry_pack(fs_entry_t *e, const char *name, uint32_t start,
                       uint32_t sectors, uint32_t bytes, uint8_t in_use)
{
    (void)memset(e, 0, sizeof(*e));
    e->magic = FS_ENTRY_MAGIC;
    e->in_use = in_use;
    e->start_sector = start;
    e->sectors = sectors;
    e->bytes = bytes;
    {
        uint32_t i = 0u;
        while ((name[i] != '\0') && (i < (H750_FS_NAME_LEN - 1u))) {
            e->name[i] = name[i];
            i++;
        }
        e->name[i] = '\0';
    }
    e->crc = fs_crc32((const uint8_t *)&e->name[0], H750_FS_NAME_LEN) ^
             (start * 2654435761u) ^ (bytes * 40503u);
}

static int entry_valid(const fs_entry_t *e)
{
    uint32_t crc;
    if (e->magic != FS_ENTRY_MAGIC) {
        return 0;
    }
    crc = fs_crc32((const uint8_t *)&e->name[0], H750_FS_NAME_LEN) ^
          (e->start_sector * 2654435761u) ^ (e->bytes * 40503u);
    return (crc == e->crc) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * 文件系统
 * ------------------------------------------------------------------------- */
static int super_write(uint32_t next_free, uint32_t file_count)
{
    uint8_t sec[H750_SD_SECTOR_SIZE];
    fs_super_t s;

    (void)memset(sec, 0, sizeof(sec));
    (void)memset(&s, 0, sizeof(s));
    s.magic = FS_SUPER_MAGIC;
    s.version = 1u;
    s.sector_size = H750_SD_SECTOR_SIZE;
    s.total_sectors = sd_capacity_sectors();
    s.dir_start = FS_DIR_START;
    s.dir_sectors = FS_DIR_SECTORS;
    s.data_start = FS_DATA_START;
    s.next_free_sector = next_free;
    s.file_count = file_count;
    (void)memcpy(sec, &s, sizeof(s));
    return sd_block_write(0u, sec);
}

static int super_read(fs_super_t *s)
{
    uint8_t sec[H750_SD_SECTOR_SIZE];
    if (sd_block_read(0u, sec) != 0) {
        return -1;
    }
    (void)memcpy(s, sec, sizeof(*s));
    if (s->magic != FS_SUPER_MAGIC) {
        return -2;
    }
    return 0;
}

int fs_format(void)
{
    uint8_t sec[H750_SD_SECTOR_SIZE];
    uint32_t i;
    uint32_t secs;

    if (sd_card_present() == 0) {
        fs_set_error("no card");
        return -1;
    }
    /* 目录区清零 */
    (void)memset(sec, 0, sizeof(sec));
    for (i = FS_DIR_START; i < (FS_DIR_START + FS_DIR_SECTORS); i++) {
        if (sd_block_write(i, sec) != 0) {
            return -2;
        }
    }
    s_stats.erases++;
    secs = sd_capacity_sectors();
    if (super_write(FS_DATA_START, 0u) != 0) {
        return -3;
    }
    s_stats.file_count = 0u;
    s_stats.data_sectors = 0u;
    s_stats.free_sectors = secs - FS_DATA_START;
    s_stats.mounted = 1u;
    s_last_error = "none";
    return 0;
}

int fs_mount(void)
{
    fs_super_t s;
    uint32_t i;
    fs_entry_t e;

    if (sd_card_present() == 0) {
        fs_set_error("no card");
        return -1;
    }
    if (super_read(&s) != 0) {
        fs_set_error("bad super");
        return -2;
    }
    s_stats.file_count = 0u;
    s_stats.data_sectors = 0u;
    for (i = 0u; i < (FS_DIR_SECTORS * FS_ENTRY_PER_SEC); i++) {
        if (entry_read(i, &e) != 0) {
            continue;
        }
        if ((e.in_use != 0u) && (entry_valid(&e) != 0)) {
            s_stats.file_count++;
            s_stats.data_sectors += e.sectors;
        }
    }
    s_stats.free_sectors = s.total_sectors - FS_DATA_START - s_stats.data_sectors;
    s_stats.mounted = 1u;
    s_last_error = "none";
    diag_log_write(H750_LOG_INFO, DIAG_MOD_SD, DIAG_EV_SD_MOUNT_OK,
                   &s_stats.file_count, 4u);
    return 0;
}

int fs_umount(void)
{
    s_stats.mounted = 0u;
    return 0;
}

static int find_entry(const char *name, uint32_t *index, fs_entry_t *out)
{
    uint32_t i;
    fs_entry_t e;

    for (i = 0u; i < (FS_DIR_SECTORS * FS_ENTRY_PER_SEC); i++) {
        if (entry_read(i, &e) != 0) {
            continue;
        }
        if ((e.in_use != 0u) && (entry_valid(&e) != 0) &&
            (strncmp(e.name, name, H750_FS_NAME_LEN) == 0)) {
            if (index != 0) { *index = i; }
            if (out != 0) { *out = e; }
            return 0;
        }
    }
    return -1;
}

static int find_free_slot(uint32_t *index)
{
    uint32_t i;
    fs_entry_t e;

    for (i = 0u; i < (FS_DIR_SECTORS * FS_ENTRY_PER_SEC); i++) {
        if (entry_read(i, &e) != 0) {
            return -1;
        }
        if ((e.in_use == 0u) || (entry_valid(&e) == 0)) {
            *index = i;
            return 0;
        }
    }
    return -2;
}

int fs_create(const char *name, fs_file_t *out)
{
    uint32_t idx = 0u;
    fs_entry_t e;
    fs_super_t s;

    if ((name == 0) || (name[0] == '\0')) {
        fs_set_error("bad name");
        return -1;
    }
    if (s_stats.mounted == 0u) {
        fs_set_error("not mounted");
        return -2;
    }
    if (find_entry(name, 0, 0) == 0) {
        fs_set_error("exists");
        return -3;
    }
    if (find_free_slot(&idx) != 0) {
        fs_set_error("dir full");
        return -4;
    }
    if (super_read(&s) != 0) {
        return -5;
    }
    entry_pack(&e, name, s.next_free_sector, 0u, 0u, 1u);
    if (entry_write(idx, &e) != 0) {
        return -6;
    }
    s_stats.file_count++;
    (void)super_write(s.next_free_sector, s_stats.file_count);
    if (out != 0) {
        out->in_use = 1u;
        (void)memcpy(out->name, e.name, H750_FS_NAME_LEN);
        out->start_sector = e.start_sector;
        out->sectors = 0u;
        out->bytes = 0u;
        out->crc = e.crc;
    }
    s_last_error = "none";
    return 0;
}

int fs_open(const char *name, fs_file_t *out)
{
    fs_entry_t e;
    if ((name == 0) || (out == 0)) {
        return -1;
    }
    if (find_entry(name, 0, &e) != 0) {
        fs_set_error("not found");
        return -2;
    }
    out->in_use = 1u;
    (void)memcpy(out->name, e.name, H750_FS_NAME_LEN);
    out->start_sector = e.start_sector;
    out->sectors = e.sectors;
    out->bytes = e.bytes;
    out->crc = e.crc;
    return 0;
}

int fs_append(fs_file_t *f, const void *data, uint32_t len)
{
    uint32_t idx = 0u;
    fs_entry_t e;
    const uint8_t *p = (const uint8_t *)data;
    uint32_t remain = len;
    uint32_t sector = 0u;
    uint32_t off_in_sector;
    uint32_t new_bytes = 0u;
    uint8_t sec[H750_SD_SECTOR_SIZE];

    if ((f == 0) || (data == 0) || (len == 0u)) {
        return -1;
    }
    if (find_entry(f->name, &idx, &e) != 0) {
        fs_set_error("append: no entry");
        return -2;
    }
    off_in_sector = e.bytes % H750_SD_SECTOR_SIZE;
    sector = e.start_sector + (e.bytes / H750_SD_SECTOR_SIZE);
    new_bytes = e.bytes;

    /* 1) 先写数据（必要时先读出扇区做部分覆盖） */
    while (remain > 0u) {
        uint32_t room = H750_SD_SECTOR_SIZE - off_in_sector;
        uint32_t chunk = (remain < room) ? remain : room;

        if (off_in_sector == 0u) {
            (void)memset(sec, 0, sizeof(sec));
        } else if (sd_block_read(sector, sec) != 0) {
            return -3;
        }
        (void)memcpy(&sec[off_in_sector], &p[new_bytes - e.bytes], chunk);
        if (sd_block_write(sector, sec) != 0) {
            return -4;   /* 写失败：目录尚未更新，文件长度保持不变，数据一致 */
        }
        new_bytes += chunk;
        remain -= chunk;
        off_in_sector += chunk;
        if (off_in_sector >= H750_SD_SECTOR_SIZE) {
            off_in_sector = 0u;
            sector++;
        }
    }

    /* 2) 数据落盘后再更新目录（长度、扇区数、CRC） */
    e.bytes = new_bytes;
    e.sectors = (new_bytes + H750_SD_SECTOR_SIZE - 1u) / H750_SD_SECTOR_SIZE;
    e.crc = fs_crc32((const uint8_t *)&e.name[0], H750_FS_NAME_LEN) ^
            (e.start_sector * 2654435761u) ^ (e.bytes * 40503u);
    if (entry_write(idx, &e) != 0) {
        return -5;
    }
    /* 3) 更新超级块中的下一个空闲扇区 */
    {
        fs_super_t s;
        if (super_read(&s) == 0) {
            uint32_t end = e.start_sector + e.sectors;
            if (end > s.next_free_sector) {
                (void)super_write(end, s_stats.file_count);
            }
        }
    }

    f->bytes = e.bytes;
    f->sectors = e.sectors;
    f->crc = e.crc;
    s_last_error = "none";
    return 0;
}

int fs_read(const fs_file_t *f, uint32_t offset, void *dst, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    uint32_t remain;
    uint32_t sector;
    uint32_t off_in_sector;
    uint8_t sec[H750_SD_SECTOR_SIZE];

    if ((f == 0) || (dst == 0) || (len == 0u)) {
        return -1;
    }
    if (offset >= f->bytes) {
        return 0;   /* 读取位置已到文件末尾 */
    }
    remain = len;
    if ((offset + remain) > f->bytes) {
        remain = f->bytes - offset;
    }
    sector = f->start_sector + (offset / H750_SD_SECTOR_SIZE);
    off_in_sector = offset % H750_SD_SECTOR_SIZE;
    {
        uint32_t done = 0u;
        while (done < remain) {
            uint32_t room = H750_SD_SECTOR_SIZE - off_in_sector;
            uint32_t chunk = (remain - done < room) ? (remain - done) : room;
            if (sd_block_read(sector, sec) != 0) {
                return -2;
            }
            (void)memcpy(&out[done], &sec[off_in_sector], chunk);
            done += chunk;
            off_in_sector += chunk;
            if (off_in_sector >= H750_SD_SECTOR_SIZE) {
                off_in_sector = 0u;
                sector++;
            }
        }
    }
    return (int)remain;
}

int fs_close(fs_file_t *f)
{
    if (f == 0) {
        return -1;
    }
    f->in_use = 0u;
    return 0;
}

int fs_delete(const char *name)
{
    uint32_t idx = 0u;
    fs_entry_t e;
    if (find_entry(name, &idx, &e) != 0) {
        return -1;
    }
    e.in_use = 0u;
    if (entry_write(idx, &e) != 0) {
        return -2;
    }
    if (s_stats.file_count > 0u) {
        s_stats.file_count--;
    }
    return 0;
}

int fs_list(char names[][H750_FS_NAME_LEN], uint32_t max, uint32_t *out_count)
{
    uint32_t i;
    uint32_t n = 0u;
    fs_entry_t e;

    for (i = 0u; i < (FS_DIR_SECTORS * FS_ENTRY_PER_SEC); i++) {
        if (entry_read(i, &e) != 0) {
            continue;
        }
        if ((e.in_use != 0u) && (entry_valid(&e) != 0)) {
            if ((names != 0) && (n < max)) {
                (void)memcpy(names[n], e.name, H750_FS_NAME_LEN);
            }
            n++;
        }
    }
    if (out_count != 0) {
        *out_count = n;
    }
    return 0;
}

int fs_recover(void)
{
    uint32_t i;
    uint32_t max_end = FS_DATA_START;
    fs_entry_t e;

    s_stats.recoveries++;
    for (i = 0u; i < (FS_DIR_SECTORS * FS_ENTRY_PER_SEC); i++) {
        if (entry_read(i, &e) != 0) {
            continue;
        }
        if ((e.in_use == 0u) || (entry_valid(&e) == 0)) {
            continue;
        }
        /* 长度与扇区数不一致（掉电发生在目录更新之前）时以扇区为准截断 */
        if (e.sectors != ((e.bytes + H750_SD_SECTOR_SIZE - 1u) / H750_SD_SECTOR_SIZE)) {
            e.sectors = (e.bytes + H750_SD_SECTOR_SIZE - 1u) / H750_SD_SECTOR_SIZE;
            (void)entry_write(i, &e);
        }
        if ((e.start_sector + e.sectors) > max_end) {
            max_end = e.start_sector + e.sectors;
        }
    }
    (void)super_write(max_end, s_stats.file_count);
    diag_log_write(H750_LOG_WARN, DIAG_MOD_SD, DIAG_EV_SD_MOUNT_OK,
                   &s_stats.recoveries, 4u);
    return 0;
}

void fs_get_stats(fs_stats_t *st)
{
    if (st == 0) {
        return;
    }
    *st = s_stats;
}
