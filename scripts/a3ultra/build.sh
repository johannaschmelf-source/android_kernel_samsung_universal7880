#!/bin/bash
# A3 Ultra (SM-A320FL, Exynos 7870, LineageOS 22.2) - Kernel bauen
# Aufruf: build.sh <kernel-tree> <toolchain-prefix>
# Overrides per env: NAME CPU_KHZ CPU_UV_DELTA CPU_UV_MAX GPU_KHZ GPU_UV_DELTA GPU_UV_MAX
#                    THERMAL_SHIFT CPU_BOOT_KHZ GPU_BOOT_MHZ
set -eo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$1"
python3 -I "$here/patch.py" "$PWD" "$here"
export ARCH=arm64 SUBARCH=arm64 CROSS_COMPILE="$2"
NAME=${NAME:-a3-v1}
cfg=arch/arm64/configs/a3ultra_defconfig
cp arch/arm64/configs/exynos7870-a3y17lte_defconfig $cfg
sed -i "s|^CONFIG_LOCALVERSION=.*|CONFIG_LOCALVERSION=\"-A3Ultra-$NAME\"|" $cfg
cat >> $cfg <<EOF
CONFIG_ECT_OC=y
CONFIG_ECT_OC_BOOT_CAP=y
CONFIG_ECT_OC_CPU_BOOT_KHZ=${CPU_BOOT_KHZ:-1586000}
CONFIG_ECT_OC_GPU_BOOT_MHZ=${GPU_BOOT_MHZ:-1001}
CONFIG_ECT_OC_THERMAL_SHIFT=${THERMAL_SHIFT:-10}
CONFIG_ECT_OC_CPU_KHZ=${CPU_KHZ:-1794000}
CONFIG_ECT_OC_CPU_UV_DELTA=${CPU_UV_DELTA:-25000}
CONFIG_ECT_OC_CPU_UV_MAX=${CPU_UV_MAX:-1250000}
CONFIG_ECT_OC_GPU_KHZ=${GPU_KHZ:-1300000}
CONFIG_ECT_OC_GPU_UV_DELTA=${GPU_UV_DELTA:-25000}
CONFIG_ECT_OC_GPU_UV_MAX=${GPU_UV_MAX:-1200000}
EOF
make a3ultra_defconfig
grep -E '^CONFIG_(ECT|LOCALVERSION)' .config
make -j"$(nproc)" Image
ls -la arch/arm64/boot/Image
