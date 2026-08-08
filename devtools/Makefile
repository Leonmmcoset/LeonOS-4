# LeonOS 4 standalone application SDK
# Override CROSS, CC, CXX, or LD for a locally installed cross toolchain.

SDK_ROOT ?= $(CURDIR)
CROSS ?= x86_64-elf-
ifeq ($(origin CC), default)
CC := $(CROSS)gcc
endif
ifeq ($(origin CXX), default)
CXX := $(CROSS)g++
endif
ifeq ($(origin LD), default)
LD := $(CROSS)ld
endif

APP ?= examples/helloworld
APP_NAME ?= helloworld
BUILD_DIR ?= build
OUTPUT := $(BUILD_DIR)/$(APP_NAME).elf
C_SOURCES := $(wildcard $(APP)/*.c)
CXX_SOURCES := $(wildcard $(APP)/*.cpp) $(wildcard $(APP)/*.cc) $(wildcard $(APP)/*.cxx)
ASM_SOURCES := $(wildcard $(APP)/*.S)
OBJECTS := $(patsubst $(APP)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
           $(patsubst $(APP)/%.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(CXX_SOURCES))) \
           $(patsubst $(APP)/%.cc,$(BUILD_DIR)/%.o,$(filter %.cc,$(CXX_SOURCES))) \
           $(patsubst $(APP)/%.cxx,$(BUILD_DIR)/%.o,$(filter %.cxx,$(CXX_SOURCES))) \
           $(patsubst $(APP)/%.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

CFLAGS := -O2 -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -mno-red-zone -mgeneral-regs-only -ffunction-sections -fdata-sections \
          -Wall -Wextra -DLEONOS_USE_PICOLIBC -D_POSIX_C_SOURCE=200809L \
          -I"$(SDK_ROOT)/include"
CXXFLAGS := -O2 -std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
            -fno-use-cxa-atexit -fno-threadsafe-statics -fno-stack-protector \
            -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only \
            -ffunction-sections -fdata-sections -Wall -Wextra \
            -DLEONOS_USE_PICOLIBC -D_POSIX_C_SOURCE=200809L -nostdinc++ \
            -I"$(SDK_ROOT)/include/stardustui/leonos" \
            -I"$(SDK_ROOT)/include/stardustui" -I"$(SDK_ROOT)/include"
LDFLAGS := --gc-sections -z max-page-size=0x1000 \
           -T "$(SDK_ROOT)/linker.ld"
LEONOS_LIB := "$(SDK_ROOT)/lib/leonos.a"
PICOLIBC_LIB := "$(SDK_ROOT)/lib/libc.a"
ZLIB_LIB := "$(SDK_ROOT)/lib/libz.a"
LIBPNG_LIB := "$(SDK_ROOT)/lib/libpng.a"
STARDUSTUI_LIB := "$(SDK_ROOT)/lib/libstardustui.a"
USE_STARDUSTUI ?= 0

ifeq ($(USE_STARDUSTUI),1)
APP_LIBS := $(STARDUSTUI_LIB)
else
APP_LIBS :=
endif

.PHONY: all clean

all: $(OUTPUT)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: $(APP)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(APP)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(APP)/%.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(APP)/%.cxx | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(APP)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) --start-group $(APP_LIBS) $(LEONOS_LIB) $(LIBPNG_LIB) $(ZLIB_LIB) $(PICOLIBC_LIB) --end-group

clean:
	rm -rf $(BUILD_DIR)
