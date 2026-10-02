/*
 * run_tests.c
 * ---------------------------------------------------------------------------
 * 测试入口：依次运行全部套件，输出汇总，并把过滤器用例导出为 CSV
 * 供 tools/verify_filter.py 做独立实现的交叉验证。
 *
 * 退出码：0 = 全部通过；1 = 存在失败。
 */
#include <stdio.h>
#include "test_framework.h"
#include "hal_stub.h"
#include "h750_config.h"

#ifndef TEST_CSV_DIR
#define TEST_CSV_DIR "build"
#endif

int main(void)
{
    printf("==========================================================\n");
    printf(" STM32H750 CAN 数据采集终端 —— 固件单元/集成测试\n");
    printf(" 目标芯片: STM32H750ZBT6 (Cortex-M7 @480MHz)\n");
    printf(" 仿真后端: 寄存器文件 + 内存映射 + 外设模型 (H750_PC_SIM)\n");
    printf("==========================================================\n");

    hal_sim_reset();

    test_mpu_config();
    test_cache_coherency();
    test_fdcan_filter();
    test_can_cache();
    test_ring_buffer();
    test_mem_pool();
    test_fdcan_driver();
    test_board_clock_sdram();
    test_bsp_periph();
    test_system_integration();

    tf_summary();

    printf("过滤器用例总数 : %u\n", test_fdcan_filter_case_count());
    {
        char path[128];
        (void)snprintf(path, sizeof(path), "%s/filter_routing.csv", TEST_CSV_DIR);
        if (test_fdcan_filter_dump_csv(path) == 0) {
            printf("过滤器用例已导出: %s\n", path);
        } else {
            printf("过滤器用例导出失败: %s\n", path);
        }
    }

    return (tf_failures() == 0) ? 0 : 1;
}
