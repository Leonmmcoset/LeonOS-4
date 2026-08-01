# LeonOS 4 standalone application SDK
# Override CROSS, CC, or LD for a locally installed cross toolchain.

SDK_ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
CROSS ?= x86_64-elf-
ifeq ($(origin CC), default)
CC := $(CROSS)gcc
endif
ifeq ($(origin LD), default)
LD := $(CROSS)ld
endif

APP ?= examples/helloworld
APP_NAME ?= helloworld
BUILD_DIR ?= build
OUTPUT := $(BUILD_DIR)/$(APP_NAME).elf
SOURCES := $(wildcard $(APP)/*.c) $(wildcard $(APP)/*.S)
OBJECTS := $(patsubst $(APP)/%.c,$(BUILD_DIR)/%.o,$(filter %.c,$(SOURCES))) \
           $(patsubst $(APP)/%.S,$(BUILD_DIR)/%.o,$(filter %.S,$(SOURCES)))

CFLAGS := -O2 -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -mno-red-zone -mgeneral-regs-only -ffunction-sections -fdata-sections \
          -Wall -Wextra -I$(SDK_ROOT)/include
LDFLAGS := --gc-sections -z max-page-size=0x1000 \
           -T $(SDK_ROOT)/linker.ld

.PHONY: all clean

all: $(OUTPUT)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: $(APP)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(APP)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT): $(OBJECTS) $(SDK_ROOT)/lib/libc.a $(SDK_ROOT)/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(SDK_ROOT)/lib/libc.a

clean:
	rm -rf $(BUILD_DIR)
