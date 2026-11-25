/*
 * ect_oc.c - Galaxy A3 Ultra: Exynos 7870 overclock via in-memory ECT patching
 *
 * Ported from the A5 Ultra (Exynos 7880) version. On 7870 Samsung already put
 * CPU 1690/1794/1924/2002 and GPU 1146/1246/1300 MHz into ECT, with PLL
 * entries. They are locked by the ASV table of the fused table version:
 * level_en = 0 and/or a 900000 uV placeholder voltage. The max CPU/GPU
 * frequency PWRCAL reports is the highest ASV level with level_en set.
 *
 * unlock: for every ASV table, levels above that table's own top enabled level
 * and <= target get level_en = 1 and a voltage that is never lower than a
 * linear extrapolation of the two levels below it (Samsung's own value is
 * kept when it is higher), plus uv_delta, capped and rounded up to 6.25 mV.
 * Levels are filled bottom-up so every extrapolation is based on real values.
 *
 * Nothing is moved, dropped or re-ordered; any table mismatch leaves the
 * domain stock.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "[ECT-OC] " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <soc/samsung/ect_parser.h>

#define UV_STEP		6250
#define MIN_SLOPE_UV	25000	/* per extrapolated level if the table is flat */

struct oc_domain {
	char *name;
	unsigned int target;	/* kHz: unlock levels <= target */
	unsigned int uv_delta;	/* extra voltage for unlocked levels */
	unsigned int uv_cap;
};

static struct oc_domain oc_domains[] = {
	{ "dvfs_cpucl0", CONFIG_ECT_OC_CPU_KHZ, CONFIG_ECT_OC_CPU_UV_DELTA, CONFIG_ECT_OC_CPU_UV_MAX },
	{ "dvfs_cpucl1", CONFIG_ECT_OC_CPU_KHZ, CONFIG_ECT_OC_CPU_UV_DELTA, CONFIG_ECT_OC_CPU_UV_MAX },
	{ "dvfs_g3d",    CONFIG_ECT_OC_GPU_KHZ, CONFIG_ECT_OC_GPU_UV_DELTA, CONFIG_ECT_OC_GPU_UV_MAX },
};

/* ASV level lists are MHz on 7870, DVFS is kHz */
static unsigned int lvl_unit(unsigned int lvl, unsigned int khz)
{
	if (lvl == khz)
		return 1;
	if (lvl && lvl * 1000 == khz)
		return 1000;
	return 0;
}

static unsigned int vt_get(struct ect_voltage_table *vt, int idx)
{
	if (vt->voltages)
		return vt->voltages[idx];
	if (vt->voltages_step)
		return vt->voltages_step[idx] * (vt->volt_step ? vt->volt_step : PMIC_VOLTAGE_STEP);
	return 0;
}

static void vt_set(struct ect_voltage_table *vt, int idx, unsigned int uv)
{
	if (vt->voltages) {
		vt->voltages[idx] = uv;
	} else if (vt->voltages_step) {
		unsigned int step = vt->volt_step ? vt->volt_step : PMIC_VOLTAGE_STEP;
		unsigned int s = DIV_ROUND_UP(uv, step);

		vt->voltages_step[idx] = s > 255 ? 255 : s;
	}
}

static void oc_apply_domain(struct oc_domain *oc)
{
	struct ect_dvfs_header *dh = ect_get_block(BLOCK_DVFS);
	void *asv_block = ect_get_block(BLOCK_ASV);
	struct ect_dvfs_domain *dvfs;
	struct ect_voltage_domain *asv;
	unsigned int au, top_khz = 0;
	int n, ng, t, g, l, tgt = -1, filled = 0;

	if (!oc->target)
		return;
	if (!dh || !asv_block) {
		pr_err("%s: ECT block missing, skip\n", oc->name);
		return;
	}

	/* ---------- validation, nothing is written here ---------- */
	dvfs = ect_dvfs_get_domain(dh, oc->name);
	if (!dvfs || dvfs->num_of_level < 3 || !dvfs->list_level) {
		pr_err("%s: no usable DVFS domain, skip\n", oc->name);
		return;
	}
	n = dvfs->num_of_level;
	asv = ect_asv_get_domain(asv_block, oc->name);
	if (!asv || asv->num_of_level != n ||
	    !(au = lvl_unit(asv->level_list[0], dvfs->list_level[0].level))) {
		pr_err("%s: ASV table does not match DVFS, skip\n", oc->name);
		return;
	}
	for (l = 0; l < n; l++) {
		if (asv->level_list[l] * au != dvfs->list_level[l].level) {
			pr_err("%s: ASV level %d mismatch, skip\n", oc->name, l);
			return;
		}
		if (l && dvfs->list_level[l].level >= dvfs->list_level[l - 1].level) {
			pr_err("%s: levels not descending, skip\n", oc->name);
			return;
		}
		if (tgt < 0 && dvfs->list_level[l].level <= oc->target)
			tgt = l;
	}
	if (tgt < 0 || dvfs->list_level[tgt].level != oc->target) {
		pr_err("%s: target %u kHz is not an ECT level, skip\n", oc->name, oc->target);
		return;
	}
	ng = asv->num_of_group;

	/* ---------- apply ---------- */
	for (t = 0; t < asv->num_of_table; t++) {
		struct ect_voltage_table *vt = &asv->table_list[t];
		int top = -1;

		if (!vt->level_en || (!vt->voltages && !vt->voltages_step))
			continue;
		for (l = 0; l < n; l++)
			if (vt->level_en[l]) {
				top = l;
				break;
			}
		/* need two real levels below the first one we fill */
		if (top < 0 || top + 1 >= n || top <= tgt)
			continue;

		for (l = top - 1; l >= tgt; l--) {
			unsigned int f0 = dvfs->list_level[l].level;
			unsigned int f1 = dvfs->list_level[l + 1].level;
			unsigned int f2 = dvfs->list_level[l + 2 < n ? l + 2 : l + 1].level;

			for (g = 0; g < ng; g++) {
				unsigned int v1 = vt_get(vt, (l + 1) * ng + g);
				unsigned int v2 = l + 2 < n ? vt_get(vt, (l + 2) * ng + g) : v1;
				unsigned int cur = vt_get(vt, l * ng + g);
				unsigned int ext;

				if (v1 > v2 && f1 > f2)
					ext = v1 + DIV_ROUND_UP((v1 - v2) * ((f0 - f1) / 1000), (f1 - f2) / 1000);
				else
					ext = v1 + MIN_SLOPE_UV;
				if (cur < ext)
					cur = ext;
				cur += oc->uv_delta;
				cur = roundup(cur, UV_STEP);
				if (cur > oc->uv_cap)
					cur = oc->uv_cap;
				vt_set(vt, l * ng + g, cur);
			}
			vt->level_en[l] = 1;
			filled++;
		}
	}

	/* Samsung's disabled DVFS levels between the target and the stock top */
	for (l = tgt; l < n && !dvfs->list_level[l].level_en; l++) {
		dvfs->list_level[l].level_en = 1;
		pr_info("%s: DVFS level %u kHz enabled\n", oc->name, dvfs->list_level[l].level);
	}
	top_khz = dvfs->list_level[tgt].level;
	if (dvfs->max_frequency < top_khz)
		dvfs->max_frequency = top_khz;

	pr_info("%s: unlocked up to %u kHz (%d table levels filled, +%u uV, cap %u uV)\n",
		oc->name, oc->target, filled, oc->uv_delta, oc->uv_cap);
}

/*
 * Move the throttling ranges of CPU/GPU up by CONFIG_ECT_OC_THERMAL_SHIFT C.
 * The last range (hardware trip / emergency) is kept; shifted bounds stay
 * strictly below the trip and in ascending order.
 */
static void oc_shift_thermal(void)
{
	struct ect_ap_thermal_header *th = ect_get_block(BLOCK_AP_THERMAL);
	int f, r, n, shift = CONFIG_ECT_OC_THERMAL_SHIFT;

	if (!th || shift <= 0)
		return;
	for (f = 0; f < th->num_of_function; f++) {
		struct ect_ap_thermal_function *fn = &th->function_list[f];
		unsigned int trip;

		if (strcmp(fn->function_name, "CPUCL0") && strcmp(fn->function_name, "CPUCL1") &&
		    strcmp(fn->function_name, "G3D"))
			continue;
		n = fn->num_of_range;
		if (n < 3)
			continue;
		trip = fn->range_list[n - 1].lower_bound_temperature;
		for (r = 0; r < n - 1; r++) {
			unsigned int t = fn->range_list[r].lower_bound_temperature + shift;
			unsigned int max_t = trip - (n - 1 - r);

			if (t > max_t)
				t = max_t;
			fn->range_list[r].lower_bound_temperature = t;
			if (r)
				fn->range_list[r - 1].upper_bound_temperature = t - 1;
		}
		fn->range_list[n - 2].upper_bound_temperature = trip - 1;
		pr_info("%s: throttling starts at %u C (+%d), trip %u C unchanged\n",
			fn->function_name, fn->range_list[0].lower_bound_temperature, shift, trip);
	}
}

void ect_oc_apply(void)
{
	int i;

	oc_shift_thermal();

	for (i = 0; i < ARRAY_SIZE(oc_domains); i++)
		oc_apply_domain(&oc_domains[i]);
}
