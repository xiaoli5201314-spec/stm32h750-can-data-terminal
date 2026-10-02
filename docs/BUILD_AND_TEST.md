# 构建与测试

[返回项目首页](../README.md) · [架构](ARCHITECTURE.md) · [协议](PROTOCOLS.md) · [验证记录](VERIFICATION.md)

所有命令以下述仓库根目录为工作目录，使用 Linux/WSL 的 GNU Make 与 GCC。
现有 [`firmware/Makefile`](../firmware/Makefile) 是宿主测试的推荐入口。
本次文档整理不修改源码、测试或构建脚本。

## 1. 依赖与构建后端

- GNU Make。
- 支持 C99 的 GCC，以及相应的 AddressSanitizer/UndefinedBehaviorSanitizer 运行库。
- Linux 或 WSL shell，提供 Makefile 使用的 `mkdir`、`rm` 等命令。
- 宿主测试不依赖真实 STM32 板、CAN 适配器、LCD、SD 卡或局域网。
- 宿主源码通过 `H750_PC_SIM` 使用寄存器文件、地址映射与外设模型。

Makefile 中 `CC ?= gcc` 可能受到 GNU Make 内建 `CC=cc` 影响。
显式传入 `CC=gcc` 可固定编译器选择。
2026-10-03 的记录使用原始命令；未提供当次系统发行版、GCC/Make 版本。
本页不会将其他工程的工具链信息套用到此处。

## 2. 推荐流程

```bash
# 普通构建：只生成测试可执行文件
make -C firmware CC=gcc

# 强制重建并执行全部单元/集成测试
make -C firmware -B test CC=gcc

# 独立目录下运行 ASan + UBSan
make -C firmware asan CC=gcc
```

`make` 的默认目标是 `all`，只构建、不执行测试。
`make test` 才会运行 `./build/run_tests`。
测试框架汇总失败数，入口在检查失败时返回非零退出码。

Makefile 未自动生成头文件依赖。
修改配置或头文件后建议使用 `-B test` 强制重编译，避免复用旧对象。
若要强制重建 Sanitizer 对象：

```bash
make -C firmware -B asan CC=gcc
```

## 3. Make 目标与产物

| 命令 | 行为 | 产物/范围 |
| --- | --- | --- |
| `make -C firmware CC=gcc` | 构建宿主测试程序 | `firmware/build/run_tests` |
| `make -C firmware -B test CC=gcc` | 强制重建并执行全部测试 | `firmware/build/` |
| `make -C firmware asan CC=gcc` | 递归调用 `BUILD=build-asan SANITIZE=1 test` | `firmware/build-asan/run_tests` |
| `make -C firmware csv CC=gcc` | 与 `test` 相同，并由测试入口尝试导出 CSV | 默认 `firmware/build/filter_routing.csv` |
| `make -C firmware arm` | 交叉编译 `src/` 与 `hal/` 对象 | 默认 `firmware/build/arm/`，不链接 |
| `make -C firmware help` | 输出现有帮助 | 帮助中“固件”措辞不改变对象编译事实 |
| `make -C firmware clean` | 删除当前 `BUILD` 目录 | 默认只删除 `firmware/build/` |

普通构建编译 `src/*.c`、`hal/*.c` 与 `test/*.c`。
普通选项包含 `-std=c99`、`-O1`、`-g` 和多项警告。
Sanitizer 使用 `-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer`。
Sanitizer 目标没有沿用普通目标的全部警告选项，因此不能据其结果宣称零编译警告。

## 4. 清理与自定义目录

```bash
# 默认只清理普通构建
make -C firmware clean

# 单独清理 Sanitizer 构建
make -C firmware clean BUILD=build-asan
```

`clean` 不会自动清理所有构建目录。
不要将 `BUILD` 设置为源码目录、仓库根或未经核对的绝对路径。
本仓库的 `.gitignore` 覆盖默认构建目录、PlatformIO 产物、日志和本地秘密文件。

## 5. CSV 的真实位置

[`run_tests.c`](../firmware/test/run_tests.c) 中 `TEST_CSV_DIR` 默认为字符串 `"build"`。
因此普通测试与 Sanitizer 测试默认都尝试写：

```text
firmware/build/filter_routing.csv
```

这个路径不会随着 `BUILD=build-asan` 自动改变。
建议先运行普通测试，再运行 Sanitizer，以便默认 CSV 目录存在。
CSV 导出失败会输出提示，但入口最终退出码取决于检查失败数，不单独保证 CSV 已导出。
查看 CSV 是否存在、是否包含期望行数时应独立检查文件。

源码注释提到 `tools/verify_filter.py`，实际目录未提供该脚本。
不提供不存在的交叉验证命令，也不把它计入当前验证能力。

## 6. 测试入口

[`run_tests.c`](../firmware/test/run_tests.c) 调用 10 个顶层测试函数。
其中外设函数包含多个套件，因此执行汇总为 14 个套件，而不是 10 个。

| 测试文件 | 主要覆盖 |
| --- | --- |
| [test_mpu_config.c](../firmware/test/test_mpu_config.c) | 区域校验、RBAR/RASR 编码和寄存器模型 |
| [test_cache_coherency.c](../firmware/test/test_cache_coherency.c) | 非缓存/写回行为、CPU/DMA 陈旧数据与维护 |
| [test_fdcan_filter.c](../firmware/test/test_fdcan_filter.c) | 33 个表驱动路由用例、GFC/过滤器编码 |
| [test_can_cache.c](../firmware/test/test_can_cache.c) | 三层容量、溢出计数、优先序、TLV 往返与容量边界 |
| [test_ring_buffer.c](../firmware/test/test_ring_buffer.c) | SPSC 容量、回绕、满空和完整性 |
| [test_mem_pool.c](../firmware/test/test_mem_pool.c) | 分配/释放、耗尽和错误回收 |
| [test_fdcan_driver.c](../firmware/test/test_fdcan_driver.c) | 位时序、消息 RAM、DLC、收发、重试与恢复 |
| [test_board_clock_sdram.c](../firmware/test/test_board_clock_sdram.c) | 时钟计算、引脚规划、SDRAM 时序与模型自检 |
| [test_bsp_periph.c](../firmware/test/test_bsp_periph.c) | QSPI、H750FS-lite、以太网、LCD、日志五个套件 |
| [test_system_integration.c](../firmware/test/test_system_integration.c) | 双总线流水线、64 字节来源、满队列、诊断与导出 |

套件通过不等于安全审计、目标编译通过、物理硬件验收或穷尽全部异常输入。
当前已记录的结果与定向回归范围见 [VERIFICATION.md](VERIFICATION.md)。

## 7. ARM 对象编译

配置入口：

```bash
make -C firmware arm ARM_CC=arm-none-eabi-gcc
```

目标使用 Cortex-M7、Thumb、FPv5-D16 和 hard-float 参数。
它只编译 `src/` 与 `hal/` 到对象文件，不编译测试。
Makefile 没有最终链接步骤，不提供 ELF/BIN/HEX、烧录或上电启动验证。
当前记录没有执行该目标；命令只是现有 Makefile 提供的可选接口。

## 8. PlatformIO 边界

[`platformio.ini`](../firmware/platformio.ini) 保留 `h750` 和 `native` 环境。
其中 `h750` 板型为 `genericSTM32H750VB`，而工程目标意图为 STM32H750ZBT6。
指定的 `linker/stm32h750zbtx.ld` 不在仓库中。
`native` 环境也没有本次执行证据。
因此不将 `pio run`、烧录或调试命令列为已验证快速开始。
需先统一芯片/板型并补齐启动和链接方案，再评估目标构建。

## 9. 持续集成

[ci.yml](../.github/workflows/ci.yml) 使用 Ubuntu 和 `actions/checkout@v4`。
仓库权限限制为 `contents: read`，仅运行宿主构建与测试。
支持 `push`、`pull_request` 和 `workflow_dispatch`，任务超时为 10 分钟。

CI 执行：

```bash
make -C firmware -B test CC=gcc
make -C firmware asan CC=gcc
```

CI 不交叉编译、烧录、发布制品或修改 GitHub 设置。
配置已提供，但远端执行结果须以实际 Actions 记录为准。
