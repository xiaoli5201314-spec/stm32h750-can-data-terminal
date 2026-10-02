/*
 * test_framework.c
 * ---------------------------------------------------------------------------
 * 测试框架实现：统计检查项、失败项，并按套件输出结果。
 */
#include <stdio.h>
#include <stdarg.h>
#include "test_framework.h"

static uint32_t s_checks;
static uint32_t s_failures;
static uint32_t s_suites;
static uint32_t s_suite_checks;
static uint32_t s_suite_failures;
static const char *s_current = "";

void tf_suite_begin(const char *name)
{
    s_current = name;
    s_suite_checks = 0u;
    s_suite_failures = 0u;
    s_suites++;
    printf("\n== 套件 %u: %s ==\n", s_suites, name);
}

void tf_suite_end(void)
{
    printf("   -> %s: %u 项检查, %u 项失败\n",
           (s_suite_failures == 0u) ? "PASS" : "FAIL",
           s_suite_checks, s_suite_failures);
}

void tf_check(int cond, const char *expr, const char *file, int line)
{
    s_checks++;
    s_suite_checks++;
    if (cond == 0) {
        s_failures++;
        s_suite_failures++;
        printf("   [FAIL] %s:%d  %s\n", file, line, expr);
    }
}

void tf_check_eq(long long a, long long b, const char *ea, const char *eb,
                 const char *file, int line)
{
    s_checks++;
    s_suite_checks++;
    if (a != b) {
        s_failures++;
        s_suite_failures++;
        printf("   [FAIL] %s:%d  %s == %s  (实际 %lld vs 期望 %lld)\n",
               file, line, ea, eb, a, b);
    }
}

void tf_check_neq(long long a, long long b, const char *ea, const char *eb,
                  const char *file, int line)
{
    s_checks++;
    s_suite_checks++;
    if (a == b) {
        s_failures++;
        s_suite_failures++;
        printf("   [FAIL] %s:%d  %s != %s  (两者都等于 %lld)\n",
               file, line, ea, eb, a);
    }
}

void tf_note(const char *fmt, ...)
{
    va_list ap;
    printf("   . ");
    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

int tf_failures(void)
{
    return (int)s_failures;
}

uint32_t tf_checks(void)
{
    return s_checks;
}

uint32_t tf_suites(void)
{
    return s_suites;
}

void tf_summary(void)
{
    printf("\n================ 测试汇总 ================\n");
    printf("套件数      : %u\n", s_suites);
    printf("检查项总数  : %u\n", s_checks);
    printf("失败项      : %u\n", s_failures);
    printf("结论        : %s\n", (s_failures == 0u) ? "全部通过" : "存在失败");
    printf("==========================================\n");
    (void)s_current;
}
