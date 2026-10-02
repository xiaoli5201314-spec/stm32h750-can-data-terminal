# ===========================================================================
# STM32H750 CAN 数据采集终端 —— 固件构建
#
#   make            构建主机测试程序（PC 仿真后端）
#   make test       构建并运行全部单元/集成测试
#   make csv        运行测试并导出过滤器用例 CSV（供 tools/verify_filter.py）
#   make arm        交叉编译目标固件（需要 arm-none-eabi-gcc）
#   make clean      清理
#
# 说明：目标构建需要 arm-none-eabi 工具链；平台相关代码在 hal/ 与 board_init.c
#       中通过 H750_PC_SIM 宏切换后端，因此同一份驱动代码既能在目标上运行，
#       也能在主机上做单元测试。
# ===========================================================================

CC        ?= gcc
BUILD     ?= build
TARGETDIR ?= build/arm

INCLUDES  := -Iinclude -Ihal -Itest

WARN      := -Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith -Wcast-align \
             -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings
CFLAGS    := -std=c99 $(WARN) -O1 -g -DH750_PC_SIM $(INCLUDES)
LDFLAGS   :=

SRC       := $(wildcard src/*.c) $(wildcard hal/*.c)
TSRC      := $(wildcard test/*.c)
OBJ       := $(SRC:%.c=$(BUILD)/%.o) $(TSRC:%.c=$(BUILD)/%.o)
BIN       := $(BUILD)/run_tests

# make SANITIZE=1：开启 AddressSanitizer + UndefinedBehaviorSanitizer 做内存检查
ifeq ($(SANITIZE),1)
CFLAGS := -std=c99 -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
          -DH750_PC_SIM $(INCLUDES)
LDFLAGS := -fsanitize=address,undefined
endif

ARM_CC    ?= arm-none-eabi-gcc
ARM_CFLAGS := -std=c99 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
              -O2 -g -ffunction-sections -fdata-sections $(INCLUDES)
ARM_SRC   := $(wildcard src/*.c) $(wildcard hal/*.c)
ARM_OBJ   := $(patsubst %.c,$(TARGETDIR)/%.o,$(ARM_SRC))

.PHONY: all test csv arm clean help

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(BIN)
	@cd . && ./$(BIN)

csv: test

# 内存与未定义行为检查（ASan + UBSan），用于定位越界/悬垂指针
asan:
	@$(MAKE) BUILD=build-asan SANITIZE=1 test

arm: $(ARM_OBJ)

$(TARGETDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@command -v $(ARM_CC) >/dev/null 2>&1 || \
	  { echo "未找到 $(ARM_CC)：目标构建需要 arm-none-eabi 工具链"; exit 127; }
	$(ARM_CC) $(ARM_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

help:
	@echo "make            构建主机测试程序"
	@echo "make test       构建并运行全部测试"
	@echo "make arm        交叉编译目标固件（需要 arm-none-eabi-gcc）"
	@echo "make clean      清理构建产物"
