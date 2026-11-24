#!/bin/bash
# A5 Ultra v21 (CPU 2002 / GPU 1352) - Kernel bauen
# Aufruf: scripts/a5ultra/build.sh <toolchain-prefix> [original-boot.img]
#   toolchain-prefix z.B. /pfad/aarch64-linux-android-  (GCC 4.9)
#   original-boot.img = dein bootendes boot.img (mit Magisk-Ramdisk);
#   ohne wird nur Image + dtb.img gebaut.
set -e
cd "$(dirname "$0")/../.."
export ARCH=arm64 SUBARCH=arm64 ANDROID_MAJOR_VERSION=p CROSS_COMPILE="$1"
NAME=${NAME:-v21-gpu1352}
cat arch/arm64/configs/rise-a5y17lte_defconfig arch/arm64/configs/oneui_defconfig > arch/arm64/configs/tmp_defconfig
sed -i "s|CONFIG_LOCALVERSION=\"\"|CONFIG_LOCALVERSION=\"-riseKernel-10.0-v1.11-A5Ultra-$NAME\"|" arch/arm64/configs/tmp_defconfig
# optionale Test-Overrides, z.B.: NAME=v22-cpu2054 CPU_KHZ=2054000 scripts/a5ultra/build.sh ...
for v in CPU_KHZ CPU_UV_DELTA TOP_EXTRA_UV GPU_KHZ GPU_UV_MAX GPU_TOP_EXTRA_UV THERMAL_SHIFT MIF_KHZ MIF_UV_DELTA MIF_UV_MAX; do
  val=${!v}
  [ -n "$val" ] && sed -i "s|^CONFIG_ECT_OC_$v=.*|CONFIG_ECT_OC_$v=$val|" arch/arm64/configs/tmp_defconfig && echo "Override: CONFIG_ECT_OC_$v=$val"
done
make CCACHE= tmp_defconfig
make CCACHE= -j"$(nproc)"
if [ -n "$2" ]; then
  python3 -I scripts/a5ultra/swapkernel.py "$2" arch/arm64/boot/Image arch/arm64/boot/dtb.img "boot_A5Ultra_${NAME}.img"
  echo "Fertig: boot_A5Ultra_${NAME}.img"
fi
