# Cross toolchain for rpi-5 (aarch64-linux-gnu) — nine targets, one source tree (R9).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Tune for the Pi 5 SoC (BCM2712: 4x Cortex-A76). The Yocto recipe does not
# need this — the raspberrypi5 machine's cortexa76 tune already passes the
# right -mcpu — but a standalone cross build otherwise compiles for the
# generic ARMv8-A baseline and leaves the A76's wider execution engine,
# crypto and dotprod throughput on the table.
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a76")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a76")
