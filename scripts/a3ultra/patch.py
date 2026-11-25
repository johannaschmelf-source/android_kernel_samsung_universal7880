#!/usr/bin/env python3
"""A3 Ultra: apply the Exynos 7870 OC changes to a LineageOS 22.2 kernel tree.

Usage: patch.py <kernel-tree> <a3ultra-dir>
Every edit must match exactly once, otherwise the script fails (no silent
half-patched trees).
"""
import os
import shutil
import sys

tree, here = sys.argv[1], sys.argv[2]


def edit(rel, old, new, count=1):
    p = os.path.join(tree, rel)
    s = open(p, encoding="utf-8", errors="surrogateescape").read()
    n = s.count(old)
    if n != count:
        sys.exit(f"patch.py: {rel}: expected {count} match(es), found {n}: {old[:60]!r}")
    s = s.replace(old, new)
    open(p, "w", encoding="utf-8", errors="surrogateescape").write(s)
    print(f"patched {rel}")


def append(rel, text):
    with open(os.path.join(tree, rel), "a", encoding="utf-8") as f:
        f.write(text)
    print(f"appended {rel}")


# 1. ECT OC driver
shutil.copy(os.path.join(here, "ect_oc.c"), os.path.join(tree, "drivers/soc/samsung/ect_oc.c"))
append("drivers/soc/samsung/Makefile", "\nobj-$(CONFIG_ECT_OC)\t\t+= ect_oc.o\n")
append("drivers/soc/samsung/Kconfig", open(os.path.join(here, "Kconfig.ect_oc"), encoding="utf-8").read())

# 2. run it right after the ECT blob is parsed (before PWRCAL/ASV init use it)
edit("drivers/soc/samsung/ect_parser.c",
     "int ect_parse_binary_header(void)\n{",
     "#ifdef CONFIG_ECT_OC\nvoid ect_oc_apply(void);\n#endif\n\nint ect_parse_binary_header(void)\n{")
edit("drivers/soc/samsung/ect_parser.c",
     "\tect_header_info.block_handle = ect_header;\n\n\treturn ret;",
     "\tect_header_info.block_handle = ect_header;\n\n#ifdef CONFIG_ECT_OC\n\tect_oc_apply();\n#endif\n\n\treturn ret;")

# 3. CPU boot cap: boot at the stock max, raise via scaling_max_freq
edit("drivers/cpufreq/exynos-mp-cpufreq.c",
     "\treturn cpufreq_frequency_table_cpuinfo(policy, exynos_info[cur]->freq_table);\n}",
     "\t{\n"
     "\t\tint ret = cpufreq_frequency_table_cpuinfo(policy, exynos_info[cur]->freq_table);\n"
     "#ifdef CONFIG_ECT_OC_BOOT_CAP\n"
     "\t\tif (!ret && policy->max > CONFIG_ECT_OC_CPU_BOOT_KHZ)\n"
     "\t\t\tpolicy->max = CONFIG_ECT_OC_CPU_BOOT_KHZ;\n"
     "#endif\n"
     "\t\treturn ret;\n"
     "\t}\n}")

# 4. GPU: boot clock + raisable limit, safe fallback voltages for the OC rows
gpu = "drivers/gpu/arm/t83x/r28p0/platform/exynos/gpu_exynos7870.c"
edit(gpu, "\t{GPU_MAX_CLOCK, 1001},", "\t{GPU_MAX_CLOCK, CONFIG_ECT_OC_GPU_BOOT_MHZ},")
edit(gpu, "\t{GPU_MAX_CLOCK_LIMIT, 1001},", "\t{GPU_MAX_CLOCK_LIMIT, CONFIG_ECT_OC_GPU_KHZ / 1000},")
# fallback voltages + governor thresholds (min, max utilisation in %):
# stock 1001 had max 100, so the governor never stepped above 1001.
edit(gpu, "\t{1300, 900000, 0, 98, 100,", "\t{1300, 1200000, 0, 95, 100,")
edit(gpu, "\t{1246, 900000, 0, 98, 100,", "\t{1246, 1150000, 0, 93, 98,")
edit(gpu, "\t{1146, 900000, 0, 98, 100,", "\t{1146, 1100000, 0, 90, 96,")
edit(gpu, "\t{1001, 900000, 0, 98, 100,", "\t{1001, 900000, 0, 85, 95,")
edit(gpu, "\t{845,  900000, 0, 78,  98,", "\t{845,  900000, 0, 75,  92,")
