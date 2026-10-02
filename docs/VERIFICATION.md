# 验证记录

[返回项目首页](../README.md) · [构建与测试](BUILD_AND_TEST.md) · [架构](ARCHITECTURE.md) · [协议](PROTOCOLS.md)

## 1. 2026-10-03 执行记录

本页记录 2026-10-03 在本地主机执行的构建、单元测试和集成回归。
普通构建与 Sanitizer 构建使用独立产物目录，测试后端为 `H750_PC_SIM`。
本页结果对应公开代码的主机测试、外设模型和确定性故障注入场景。

| 环境项 | 实际版本 |
| --- | --- |
| 主机环境 | Windows / WSL2，x86_64 |
| Linux 发行版 | Ubuntu 22.04.5 LTS |
| C 编译器 | GCC 11.4.0 |
| 构建工具 | GNU Make 4.3 |

在仓库根目录执行的原始命令：

```bash
make -C firmware -B test CC=gcc
make -C firmware -B asan CC=gcc
```

| 项目 | 普通宿主测试 | ASan/UBSan 测试 |
| --- | --- | --- |
| 日期 | 2026-10-03 | 2026-10-03 |
| 构建入口 | `firmware/Makefile` | `firmware/Makefile` 的 `asan` 目标 |
| 后端 | `H750_PC_SIM` | `H750_PC_SIM` |
| 套件 | 14 | 14 |
| 检查数 | 11812 | 11812 |
| 失败数 | 0 | 0 |
| 执行结论 | PASS | PASS |
| 退出码 | 0 | 0 |
| Sanitizer 报告 | 未启用 | 未出现 ASan/UBSan 报告 |

Makefile 的 `asan` 目标使用 `-fsanitize=address,undefined`。
普通构建和 Sanitizer 构建均强制重新编译，分别使用 `build/` 与 `build-asan/`。
CI 同样显式传入 `CC=gcc`。

## 2. 流水线场景

[`test_system_integration.c`](../firmware/test/test_system_integration.c) 的主要混合总线场景：

- 向 CAN1/CAN2 注入 240 帧，两个实例各半。
- 混合状态、告警和观测分组，以及不同 FD 负载长度。
- 每注入 12 帧推进 2 ms 模型时间，避免一开始就压满硬件 FIFO 模型。
- 再推进 200 ms 模型时间排空流水线。
- 检查接收、解析、记录上报和缓存排空。

| 指标 | 记录值 | 含义 |
| --- | --- | --- |
| 接收/解析/上报记录 | 240 | 该注入场景中的记录数 |
| 上报包 | 20 | 本地 `eth_send_report()` 成功计数 |
| 上报字节 | 8960 | TLV 应用负载累计，不含外层网络头 |
| 接收软件队列丢弃 | 0 | 仅该混合注入节奏下的结果 |

这是确定性宿主场景，不是满载总线、网络抓包、远端确认或长期不丢帧证明。
不能把模拟时间换算为实际吞吐、CPU 占用或板级延迟。

## 3. 过滤与定向回归

[`test_fdcan_filter.c`](../firmware/test/test_fdcan_filter.c) 定义 33 个表驱动路由用例。
此外有规则/寄存器编码、GFC 0/1/2/3 动作和嗅探切换检查。
CSV 由测试入口尝试输出到默认 `firmware/build/filter_routing.csv`。

2026-10-03 的定向回归采用先增加用例、再修复并重新执行的 RED/GREEN 流程。
重点覆盖以下数据和配置路径：

| 关注点 | 当前源码/测试证据 |
| --- | --- |
| 接收存储大小 | 类型化 `app_rxq_item_t` 数组与 `sizeof` 槽宽，256 槽满边界 |
| CAN FD 最大长度 | DLC 15 转 64 字节，集成场景逐字节比较完整负载 |
| 总线来源 | 由 `cfg->bus` 入队并进入记录，CAN2 场景断言 `bus=1` |
| GFC 与嗅探 | 未匹配 0→FIFO0、1→FIFO1、2/3→拒收；注入读取当前寄存器 |
| 运行时配置握手 | GFC 写保护、延迟 INIT、运行/停止及 CCE 状态恢复 |
| 嗅探失败回滚 | INIT/CCE/GFC/重启故障注入；两路事务、错误返回、软件标志与日志 |
| Classic / RTR | DLC 0..15、陈旧 RAM 尾部、两路总线、发送写入范围和 TLV 实际长度 |
| 严重 Bus-off | 驱动锁存、CAN2 独立连续故障、应用统计、LED 与停止恢复 |

新增回归的 RED 阶段执行记录：

| 回归 | 修复前的失败检查 | 触发的问题 |
| --- | --- | --- |
| 运行时嗅探切换 | 101 | 配置未更新、错误未传播、软件标志与日志错误更新 |
| GFC 写保护模型 | 3 | 运行状态下仍允许写入受保护配置 |
| CAN2 严重故障 | 2 | 全局严重故障统计和错误 LED 未置位 |
| Classic / RTR 负载 | 1462 | 读取陈旧数据、超范围写入、TLV 长度过长 |

所有修复合并后，普通与 Sanitizer 构建均重新执行全部 11812 项检查并通过。

接收槽实际字节数由结构体布局和 ABI 决定。
本页记录类型化容量修复，不把未经当次 `sizeof` 测量的估算值写成固定大小。
严重故障细节见 [`test_fdcan_driver.c`](../firmware/test/test_fdcan_driver.c) 和 [架构说明](ARCHITECTURE.md)。

## 4. 模块覆盖

当前套件还包含：

- MPU 区域属性、编码、查找与寄存器模型。
- 非缓存/写回模型、CPU/DMA 陈旧数据及 clean/invalidate。
- SPSC 环形缓冲、定长内存池、三层缓存溢出和 TLV 正常往返。
- FDCAN 时序计算、消息 RAM、发送超时和退避恢复。
- 时钟/引脚配置、SDRAM 模式自检与时序计算。
- QSPI、H750FS-lite、以太网本地回环、LCD 帧缓冲和诊断日志。

测试框架检查数不是覆盖率，也不证明每个目标分支已执行。
尤其目标 SDMMC、QSPI 数据传输与真正 RTOS 接入不能用宿主套件结果替代。

## 5. 结果解读

| 测试对象 | 记录方式 |
| --- | --- |
| 单元测试 | 自定义测试框架累计断言数与失败数 |
| 应用集成 | 双总线注入、解析、缓存、组包与诊断统计 |
| 缓存行为 | 容量边界、出队顺序、溢出和丢弃计数 |
| Cache / DMA | 影子缓存与 DMA 内存视图的数据比较 |
| 外设模型 | 寄存器、内存影像、像素与文件数据回读 |
| Sanitizer | ASan/UBSan 编译后的测试程序运行记录 |
| 自动回归 | 首页徽章及 GitHub Actions 的对应提交记录 |

检查数是执行断言数，主机调度 tick 是模型时间，8960 字节是 TLV 应用负载统计。
这些定义方便读者按相同输入、工具链和命令复现结果。

## 6. 后续复现建议

```bash
make -C firmware -B test CC=gcc
make -C firmware -B asan CC=gcc
```

后续证据宜同时保留工具链版本、提交 SHA、原始日志、退出码和必要的生成文件。
板端记录应单独标注设备、接线、输入负载、持续时间和测量方式。
只有实际补齐这些证据后，才更新硬件、性能或远端交付结论。

## 7. GitHub Actions 自动回归

2026-10-03 核对首次公开版本的远端工作流：

| 项目 | 记录 |
| --- | --- |
| 提交 | [`ce799cc`](https://github.com/xiaoli5201314-spec/stm32h750-can-data-terminal/tree/ce799cc10aedc19ce3108224adb5f4a572012e5f) |
| 工作流 | [Host Tests / 37060921303](https://github.com/xiaoli5201314-spec/stm32h750-can-data-terminal/actions/runs/37060921303) |
| 状态 | `completed` / `success` |
| 执行内容 | Ubuntu GCC 主机测试、AddressSanitizer 与 UndefinedBehaviorSanitizer 测试 |

后续提交的自动回归结果可从首页徽章进入对应的 Actions 记录查看。
