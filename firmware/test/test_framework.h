/*
 * test_framework.h
 * ---------------------------------------------------------------------------
 * 极简单元测试框架：套件 + 断言 + 统计。不使用任何第三方测试库。
 */
#ifndef H750_TEST_FRAMEWORK_H
#define H750_TEST_FRAMEWORK_H

#include <stdint.h>

void     tf_suite_begin(const char *name);
void     tf_suite_end(void);
void     tf_check(int cond, const char *expr, const char *file, int line);
void     tf_check_eq(long long a, long long b, const char *ea, const char *eb,
                     const char *file, int line);
void     tf_check_neq(long long a, long long b, const char *ea, const char *eb,
                      const char *file, int line);
void     tf_note(const char *fmt, ...);
int      tf_failures(void);
uint32_t tf_checks(void);
uint32_t tf_suites(void);
void     tf_summary(void);

#define TF_ASSERT(cond) \
    tf_check(((cond) != 0) ? 1 : 0, #cond, __FILE__, __LINE__)

#define TF_ASSERT_EQ(a, b) \
    tf_check_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

#define TF_ASSERT_NE(a, b) \
    tf_check_neq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)

/* 各测试套件入口 */
void test_mpu_config(void);
void test_cache_coherency(void);
void test_fdcan_filter(void);
void test_can_cache(void);
void test_ring_buffer(void);
void test_mem_pool(void);
void test_fdcan_driver(void);
void test_board_clock_sdram(void);
void test_bsp_periph(void);
void test_system_integration(void);

/* 过滤器用例导出（供 tools/verify_filter.py 交叉验证） */
int  test_fdcan_filter_dump_csv(const char *path);
uint32_t test_fdcan_filter_case_count(void);

#endif /* H750_TEST_FRAMEWORK_H */
