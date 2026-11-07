# SPDX-License-Identifier: MIT
# Builds openlf2.gba from the original Little Fighter 2 v2.0a installer.
#
#   make INSTALLER=/path/to/LF2_v2.0a.exe
#
# TOOL_PREFIX selects the ARM toolchain (default: arm-none-eabi- on PATH).
# EXTRA_FLAGS can add include and library paths for a toolchain installed elsewhere.

INSTALLER ?= LF2_v2.0a.exe
TOOL_PREFIX ?= arm-none-eabi-
CXX := $(TOOL_PREFIX)g++
AS := $(TOOL_PREFIX)as
LD := $(TOOL_PREFIX)g++
OBJCOPY := $(TOOL_PREFIX)objcopy
PYTHON ?= python3
BOOT_LOGO ?= build/boot_logo.bin

ARCH := -mcpu=arm7tdmi -mthumb -mthumb-interwork
COMMON := $(ARCH) -O2 -Wall -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections $(EXTRA_FLAGS)
CXXFLAGS := $(COMMON) -std=gnu++20 -Ibuild/assets -Igame
LDFLAGS := $(ARCH) -nostartfiles -Wl,-T,gba/linker.ld -Wl,--gc-sections $(EXTRA_LDFLAGS)
LIBS := -lstdc++ -lsupc++ -lm -lc -lgcc

GAME_SRC := $(wildcard game/*.cpp)
GBA_SRC := $(wildcard gba/*.cpp)
OBJS := $(patsubst %.cpp,build/obj/%.o,$(GAME_SRC) $(GBA_SRC)) build/obj/gba/crt0.o build/obj/assets.o

all: build/openlf2.gba

build/assets/assets.h: $(INSTALLER) $(wildcard tools/*.py)
	@mkdir -p build/assets
	$(PYTHON) tools/convert_assets.py $(INSTALLER) build/assets

build/obj/%.o: %.cpp build/assets/assets.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/obj/gba/crt0.o: gba/crt0.s
	@mkdir -p $(dir $@)
	$(AS) $(ARCH) $< -o $@

build/obj/assets.o: build/assets/assets.h
	@mkdir -p build/obj
	$(AS) $(ARCH) -Ibuild/assets build/assets/assets.S -o $@

build/openlf2.elf: $(OBJS) gba/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

build/boot_logo.bin:
	@mkdir -p build
	$(PYTHON) tools/fetch_logo.py $@

build/openlf2.gba: build/openlf2.elf $(BOOT_LOGO)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/gbafix.py $@ $(BOOT_LOGO)

clean:
	rm -rf build

-include $(OBJS:.o=.d)
.PHONY: all clean
