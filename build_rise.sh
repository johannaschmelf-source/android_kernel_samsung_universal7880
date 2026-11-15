#!/bin/bash
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE=aarch64-linux-android-

mkdir -p rise
make exynos7880-a5y17lte_defconfig
make -j$(nproc) Image dtb.img 2>&1 | tee rise/build.log

cp arch/arm64/boot/Image rise/boot_oneui_10.0_a5.img
