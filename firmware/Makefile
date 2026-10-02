# Cortex-M3 build of nmea-guard for QEMU's mps2-an385 board.
#
#   make -C firmware             build the test runner and the size probe
#   make -C firmware qemu-test   run the test vectors on QEMU, then print the
#                                flash/RAM report (build/firmware-size.json)
#   make -C firmware clean
#
# Needs gcc-arm-none-eabi, libnewlib-arm-none-eabi,
# libstdc++-arm-none-eabi-newlib and qemu-system-arm.

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
SIZE    := $(CROSS)size
QEMU    ?= qemu-system-arm
PYTHON  ?= python3
TIMEOUT ?= 120

ROOT  := ..
BUILD := build

ARCH     := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CXXFLAGS := $(ARCH) -std=c++17 -Os -g -ffunction-sections -fdata-sections \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
            -I$(ROOT)/include
CFLAGS   := $(ARCH) -std=c11 -Os -g -ffunction-sections -fdata-sections \
            -Wall -Wextra -Wpedantic -Wconversion -Werror
LDFLAGS  := $(ARCH) -nostartfiles --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -T mps2_an385.ld

LIB_SRC := $(wildcard $(ROOT)/src/*.cpp)
LIB_OBJ := $(patsubst $(ROOT)/src/%.cpp,$(BUILD)/lib/%.o,$(LIB_SRC))
SUPPORT_OBJ := $(BUILD)/semihost.o $(BUILD)/startup.o $(BUILD)/cxx_support.o
RUNNER_OBJ := $(BUILD)/runner.o $(BUILD)/vectors.o $(SUPPORT_OBJ)
PROBE_OBJ  := $(BUILD)/probe.o $(SUPPORT_OBJ)

QEMU_FLAGS := -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial none \
              -semihosting-config enable=on,target=native

.PHONY: all qemu-test size clean

all: $(BUILD)/runner.elf $(BUILD)/probe.elf

$(BUILD)/lib/%.o: $(ROOT)/src/%.cpp | $(BUILD)/lib
	$(CXX) $(CXXFLAGS) -fstack-usage -c $< -o $@

$(BUILD)/%.o: %.cpp vectors.hpp platform.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vectors.o: vectors.inc

$(BUILD)/runner.elf: $(RUNNER_OBJ) $(LIB_OBJ) mps2_an385.ld
	$(CXX) $(LDFLAGS) -Wl,-Map=$(BUILD)/runner.map $(RUNNER_OBJ) $(LIB_OBJ) -o $@

$(BUILD)/probe.elf: $(PROBE_OBJ) $(LIB_OBJ) mps2_an385.ld
	$(CXX) $(LDFLAGS) -Wl,-Map=$(BUILD)/probe.map $(PROBE_OBJ) $(LIB_OBJ) -o $@

$(BUILD) $(BUILD)/lib:
	mkdir -p $@

# QEMU's exit code is the runner's: 0 only if every vector and scenario passed.
qemu-test: all
	@status=0; timeout $(TIMEOUT) $(QEMU) $(QEMU_FLAGS) -kernel $(BUILD)/runner.elf \
	  > $(BUILD)/qemu.log 2>&1 || status=$$?; \
	  cat $(BUILD)/qemu.log; \
	  [ $$status -eq 0 ] || { echo "QEMU run failed with status $$status" >&2; exit 1; }
	$(MAKE) --no-print-directory size

size: all
	@$(SIZE) $(BUILD)/probe.elf $(BUILD)/runner.elf
	$(PYTHON) $(ROOT)/scripts/firmware_report.py --build $(BUILD) -o $(BUILD)/firmware-size.json

clean:
	rm -rf $(BUILD)
