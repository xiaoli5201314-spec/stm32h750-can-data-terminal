/*
 * sd_file.h
 * ---------------------------------------------------------------------------
 * SD 卡块设备与自研日志文件系统（H750FS-lite）。
 *
 * 为什么自研而不是用通用 FAT：
 *   本终端只需要"创建文件 / 顺序追加 / 顺序读取 / 导出"四类操作，不需要
 *   随机改写与目录树。自研文件系统用"数据先落盘、目录后更新"的写序保证
 *   掉电后最多丢失最后一个扇区，不会出现目录与数据不一致；同时日志文件
 *   内部是纯文本记录，用 dd 直接读取原始扇区即可阅读，便于现场取证。
 *
 * 磁盘布局（扇区 512 字节）：
 *   扇区 0       超级块（magic / 版本 / 容量 / 目录起点 / 数据起点）
 *   扇区 1~2     目录区（每扇区 10 条，共 20 条，条目 48 字节）
 *   扇区 3 起    数据区（每个文件连续分配）
 */
#ifndef H750_SD_FILE_H
#define H750_SD_FILE_H

#include <stdint.h>
#include "h750_config.h"

#define FS_ENTRY_BYTES      48u
#define FS_ENTRY_PER_SEC    (H750_SD_SECTOR_SIZE / FS_ENTRY_BYTES)
#define FS_DIR_SECTORS      2u
#define FS_DIR_START        1u
#define FS_DATA_START       (FS_DIR_START + FS_DIR_SECTORS)

typedef struct {
    uint8_t  in_use;
    char     name[H750_FS_NAME_LEN];
    uint32_t start_sector;
    uint32_t sectors;
    uint32_t bytes;
    uint32_t crc;
} fs_file_t;

typedef struct {
    uint32_t file_count;
    uint32_t data_sectors;
    uint32_t free_sectors;
    uint32_t writes;
    uint32_t reads;
    uint32_t erases;
    uint32_t recoveries;
    uint32_t errors;
    uint8_t  mounted;
} fs_stats_t;

/* ---- 块设备 ----------------------------------------------------------- */
int  sd_block_init(void);
int  sd_card_present(void);
int  sd_block_read(uint32_t sector, void *buf);
int  sd_block_write(uint32_t sector, const void *buf);
uint32_t sd_capacity_sectors(void);

/* ---- 文件系统 --------------------------------------------------------- */
int  fs_format(void);
int  fs_mount(void);
int  fs_umount(void);
int  fs_create(const char *name, fs_file_t *out);
int  fs_open(const char *name, fs_file_t *out);
int  fs_append(fs_file_t *f, const void *data, uint32_t len);
int  fs_read(const fs_file_t *f, uint32_t offset, void *dst, uint32_t len);
int  fs_close(fs_file_t *f);
int  fs_delete(const char *name);
int  fs_list(char names[][H750_FS_NAME_LEN], uint32_t max, uint32_t *out_count);
/* 掉电后重新挂载：以目录为准重建内存视图，丢弃未提交的尾部数据 */
int  fs_recover(void);
void fs_get_stats(fs_stats_t *st);
uint32_t fs_crc32(const uint8_t *data, uint32_t len);
const char *fs_last_error(void);

#endif /* H750_SD_FILE_H */
