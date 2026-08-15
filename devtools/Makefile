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

CFLAGS := -O2 -std=c11 -ffreestanding -fno-stack-protector -fPIC -fPIE \
          -mno-red-zone -mgeneral-regs-only -ffunction-sections -fdata-sections \
          -Wall -Wextra -DLEONOS_USE_PICOLIBC -D_POSIX_C_SOURCE=200809L \
          -D_DEFAULT_SOURCE \
          -I"$(SDK_ROOT)/include"
CXXFLAGS := -O2 -std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
            -fno-use-cxa-atexit -fno-threadsafe-statics -fno-stack-protector \
            -fPIC -fPIE -mno-red-zone -mgeneral-regs-only \
            -ffunction-sections -fdata-sections -Wall -Wextra \
            -DLEONOS_USE_PICOLIBC -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -nostdinc++ \
            -I"$(SDK_ROOT)/include/stardustui/leonos" \
            -I"$(SDK_ROOT)/include/stardustui" -I"$(SDK_ROOT)/include"
STATIC ?= 0
ifeq ($(STATIC),1)
LDFLAGS := --gc-sections -z max-page-size=0x1000 -T "$(SDK_ROOT)/linker.ld"
CRT :=
RUNTIME_LIBS := $(LEONOS_LIB) $(LIBPNG_LIB) $(ZLIB_LIB) $(PICOLIBC_LIB)
else
LDFLAGS := -pie --hash-style=sysv --dynamic-linker 0:/system/lib/ld-leonos.elf \
           -z relro -z now -z max-page-size=0x1000 -T "$(SDK_ROOT)/dynamic-app.ld"
CRT := "$(SDK_ROOT)/lib/crt0-dynamic.o" "$(SDK_ROOT)/lib/leonos-abi-note.o"
RUNTIME_LIBS := "$(SDK_ROOT)/lib/libleonos.so.1"
endif
LEONOS_LIB := "$(SDK_ROOT)/lib/leonos.a"
PICOLIBC_LIB := "$(SDK_ROOT)/lib/libc.a"
ZLIB_LIB := "$(SDK_ROOT)/lib/libz.a"
LIBPNG_LIB := "$(SDK_ROOT)/lib/libpng.a"
STARDUSTUI_LIB := "$(SDK_ROOT)/lib/libstardustui.a"
LIBMAGIC_LIB := "$(SDK_ROOT)/lib/libmagic.a"
LIBMAGIC_SO := "$(SDK_ROOT)/lib/libmagic.so.1"
LUA_LIB := "$(SDK_ROOT)/lib/liblua.a"
LUA_SO := "$(SDK_ROOT)/lib/liblua.so.5"
SQLITE_LIB := "$(SDK_ROOT)/lib/sqlite.a"
SQLITE_SO := "$(SDK_ROOT)/lib/sqlite.so.3"
USE_STARDUSTUI ?= 0
USE_LIBMAGIC ?= 0
USE_LUA ?= 0
USE_SQLITE ?= 0

ifeq ($(USE_STARDUSTUI),1)
APP_LIBS := $(STARDUSTUI_LIB)
else
APP_LIBS :=
endif

ifeq ($(USE_LIBMAGIC),1)
ifeq ($(STATIC),1)
APP_LIBS += $(LIBMAGIC_LIB)
else
APP_LIBS += $(LIBMAGIC_SO)
endif
endif

ifeq ($(USE_LUA),1)
ifeq ($(STATIC),1)
APP_LIBS += $(LUA_LIB)
else
APP_LIBS += $(LUA_SO)
endif

ifeq ($(USE_SQLITE),1)
ifeq ($(STATIC),1)
APP_LIBS += $(SQLITE_LIB)
else
APP_LIBS += $(SQLITE_SO)
endif
endif
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
	$(LD) $(LDFLAGS) -o $@ $(CRT) $(OBJECTS) --start-group $(APP_LIBS) $(RUNTIME_LIBS) --end-group

clean:
	rm -rf $(BUILD_DIR)
