# Setup

You need CMake 3.16 or newer and a C++17 compiler. CMake uses the system GoogleTest if there is one and downloads it otherwise. The Python tests need pynmea2 and pytest (`requirements.txt`), fuzzing needs Clang, and the firmware build needs the Arm GNU toolchain and QEMU.

On Ubuntu or Debian:

```sh
sudo apt-get install cmake g++ libgtest-dev python3-pip clang libclang-rt-dev \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib qemu-system-arm
python3 -m pip install -r requirements.txt
```

## Commands

```sh
bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
bash scripts/fuzz.sh 60             # 30 s per fuzz target, ASan + UBSan
make -C firmware qemu-test          # Cortex-M3 test vectors on QEMU, then flash/RAM sizes
```

The logs come from the [gpsd](https://gitlab.com/gpsd/gpsd) regression suite (BSD licence) and aren't committed. Most tests need them, so run the fetch script first.

Fuzzing is described in [fuzzing.md](fuzzing.md) and the firmware build in [firmware.md](firmware.md).
