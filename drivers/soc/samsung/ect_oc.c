/*
 * ect_oc.c - Galaxy A5 Ultra: Exynos 7880 overclock via in-memory ECT patching
 *
 * All CPU/GPU clock levels, PLL dividers and ASV voltages on Exynos 7880 come
 * from the ECT table the bootloader hands to the kernel. A frequency that is
 * not in ECT can not be set by PWRCAL.
 *
 * v8 rule: never move, drop or reorder levels. Level indices are used all
 * over PWRCAL (boot/resume level, CPD resume, minlock, devfreq maps), so
 * shifting them breaks stock clocks too. Instead:
 *
 *   - unlock:  levels Samsung put into ECT but disabled ((X), e.g. GPU 1086 /
 *              1200 MHz) are enabled in place, with an optional extra voltage.
 *   - replace: the top level (L0) is rewritten in place to a higher clock:
 *              new label, PLL M divider scaled (same P/S), L0 voltage raised.
 *              Only done if L0's PLL entry is used by nobody else.
 *
 * Everything is validated first; on any mismatch the domain stays stock.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "[ECT-OC] " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/math64.h>
#include <soc/samsung/ect_parser.h>

#define PLL_M_MAX	1023

bool ect_oc_mif_shifted;	/* kept for drampara/asv hooks, never set in v8 */

/*
 * v12: the top PLL entry is NOT rewritten (something outside the kernel DVFS
 * path needs Samsung's original entry, rewriting it bootloops). The new rate
 * is appended to a copy of the PLL list instead. ECT is parsed before slab
 * is up, so the copies live in static buffers.
 */
#define OC_PLL_MAX_FREQ	32
static struct ect_pll_frequency oc_pll_buf[3][OC_PLL_MAX_FREQ];
static int oc_pll_buf_used;

static struct ect_pll_frequency *pll_append(struct ect_pll *pll)
{
	struct ect_pll_frequency *buf;
	int n = pll->num_of_frequency;

	if (oc_pll_buf_used >= ARRAY_SIZE(oc_pll_buf) || n + 1 > OC_PLL_MAX_FREQ)
		return NULL;
	buf = oc_pll_buf[oc_pll_buf_used++];
	memcpy(buf, pll->frequency_list, sizeof(*buf) * n);
	pll->frequency_list = buf;
	pll->num_of_frequency = n + 1;
	return &buf[n];
}

struct oc_domain {
	char *name;
	unsigned int target;	/* kHz: unlock levels <= target, replace L0 if target > L0 */
	unsigned int uv_delta;	/* extra voltage for unlocked/replaced levels */
	unsigned int uv_cap;
	bool thermal;		/* AP thermal caps are CPU frequencies */
	unsigned int top_extra;	/* extra uV for a replaced top level */
};

static struct oc_domain oc_domains[] = {
	{ "dvfs_cpucl0", CONFIG_ECT_OC_CPU_KHZ, CONFIG_ECT_OC_CPU_UV_DELTA, CONFIG_ECT_OC_CPU_UV_MAX, true, CONFIG_ECT_OC_TOP_EXTRA_UV },
	{ "dvfs_cpucl1", CONFIG_ECT_OC_CPU_KHZ, CONFIG_ECT_OC_CPU_UV_DELTA, CONFIG_ECT_OC_CPU_UV_MAX, true, CONFIG_ECT_OC_TOP_EXTRA_UV },
	{ "dvfs_g3d",    CONFIG_ECT_OC_GPU_KHZ, CONFIG_ECT_OC_GPU_UV_DELTA, CONFIG_ECT_OC_GPU_UV_MAX, false, CONFIG_ECT_OC_GPU_TOP_EXTRA_UV },
};

static unsigned int add_uv(unsigned int v, unsigned int delta, unsigned int cap)
{
	v += delta;
	return v > cap ? cap : v;
}

/* ASV/RCC level lists are MHz on 7880, DVFS is kHz */
static unsigned int lvl_unit(unsigned int lvl, unsigned int khz)
{
	if (lvl == khz)
		return 1;
	if (lvl && lvl * 1000 == khz)
		return 1000;
	return 0;
}

static int pll_unit(unsigned int pll_freq, unsigned int dvfs_val)
{
	if (pll_freq == dvfs_val)
		return 1;
	if ((u64)dvfs_val * 1000 == pll_freq)
		return 1000;
	return 0;
}

static struct ect_pll_frequency *pll_find(struct ect_pll *pll, unsigned int dvfs_val, int *unit)
{
	int i, u;

	for (i = 0; i < pll->num_of_frequency; i++) {
		u = pll_unit(pll->frequency_list[i].frequency, dvfs_val);
		if (u) {
			if (unit)
				*unit = u;
			return &pll->frequency_list[i];
		}
	}
	return NULL;
}

/* Is (clock, value) used by any DVFS level other than (skip_dom, skip_lv)? */
static bool pll_value_used_elsewhere(struct ect_dvfs_header *dh, const char *clk,
				     unsigned int val, struct ect_dvfs_domain *skip_dom, int skip_lv)
{
	int d, c, l;

	for (d = 0; d < dh->num_of_domain; d++) {
		struct ect_dvfs_domain *dom = &dh->domain_list[d];

		for (c = 0; c < dom->num_of_clock; c++) {
			if (ect_strcmp((char *)clk, dom->list_clock[c]))
				continue;
			for (l = 0; l < dom->num_of_level; l++) {
				if (dom == skip_dom && l == skip_lv)
					continue;
				if (dom->list_dvfs_value[l * dom->num_of_clock + c] == val)
					return true;
			}
		}
	}
	return false;
}

/* raise the voltage of level lv in every ASV table/group */
static void asv_raise_level(struct ect_voltage_domain *asv, int lv,
			    unsigned int delta, unsigned int cap)
{
	int t, g, ng = asv->num_of_group;

	for (t = 0; t < asv->num_of_table; t++) {
		struct ect_voltage_table *vt = &asv->table_list[t];

		if (vt->level_en)
			vt->level_en[lv] = 1;
		for (g = 0; g < ng; g++) {
			if (vt->voltages) {
				vt->voltages[lv * ng + g] = add_uv(vt->voltages[lv * ng + g], delta, cap);
			} else if (vt->voltages_step) {
				unsigned int step = vt->volt_step ? vt->volt_step : PMIC_VOLTAGE_STEP;
				unsigned int uv = add_uv(vt->voltages_step[lv * ng + g] * step, delta, cap) / step;

				vt->voltages_step[lv * ng + g] = uv > 255 ? 255 : uv;
			}
		}
	}
}

static void oc_apply_domain(struct oc_domain *oc)
{
	struct ect_dvfs_header *dh = ect_get_block(BLOCK_DVFS);
	void *pll_block = ect_get_block(BLOCK_PLL);
	void *asv_block = ect_get_block(BLOCK_ASV);
	void *rcc_block = ect_get_block(BLOCK_RCC);
	void *minlock_block = ect_get_block(BLOCK_MINLOCK);
	struct ect_ap_thermal_header *th = ect_get_block(BLOCK_AP_THERMAL);
	struct ect_dvfs_domain *dvfs;
	struct ect_voltage_domain *asv;
	struct ect_rcc_domain *rcc = NULL;
	struct ect_minlock_domain *minlock = NULL;
	struct ect_pll *pll = NULL;
	struct ect_pll_frequency *e0 = NULL;
	unsigned int old0, v0 = 0, m_new = 0, actual = 0, au, ru = 0, stock_top = 0;
	int nclk, c, l, f, r, pll_idx = -1, unit = 0, unlocked = 0;
	bool replace;

	if (!oc->target)
		return;
	if (!dh || !pll_block || !asv_block) {
		pr_err("%s: ECT block missing, skip\n", oc->name);
		return;
	}

	/* ---------- validation, nothing is written here ---------- */
	dvfs = ect_dvfs_get_domain(dh, oc->name);
	if (!dvfs || dvfs->num_of_level < 2 || !dvfs->list_level || !dvfs->list_dvfs_value) {
		pr_err("%s: no usable DVFS domain, skip\n", oc->name);
		return;
	}
	nclk = dvfs->num_of_clock;
	old0 = dvfs->list_level[0].level;

	asv = ect_asv_get_domain(asv_block, oc->name);
	if (!asv || asv->num_of_level != dvfs->num_of_level ||
	    !(au = lvl_unit(asv->level_list[0], old0)) || oc->target % au) {
		pr_err("%s: ASV table does not match DVFS, skip\n", oc->name);
		return;
	}
	for (l = 1; l < dvfs->num_of_level; l++)
		if (asv->level_list[l] * au != dvfs->list_level[l].level) {
			pr_err("%s: ASV level %d mismatch, skip\n", oc->name, l);
			return;
		}

	replace = oc->target > old0;
	if (replace) {
		if (oc->target > old0 + old0 / 5) {
			pr_err("%s: target %u is more than +20%% over %u, refused\n",
			       oc->name, oc->target, old0);
			return;
		}
		/* the first clock member that is a known PLL (e.g. CPUCL0_PLL) */
		for (c = 0; c < nclk && pll_idx < 0; c++)
			if (strstr(dvfs->list_clock[c], "PLL")) {
				pll = ect_pll_get_pll(pll_block, dvfs->list_clock[c]);
				if (pll)
					pll_idx = c;
			}
		if (pll_idx < 0) {
			pr_err("%s: no PLL member, skip\n", oc->name);
			return;
		}
		v0 = dvfs->list_dvfs_value[pll_idx];
		e0 = pll_find(pll, v0, &unit);
		if (!e0 || !e0->m) {
			pr_err("%s: PLL entry for %u not found, skip\n", oc->name, v0);
			return;
		}
		m_new = DIV_ROUND_CLOSEST(e0->m * oc->target, old0);
		if (m_new <= e0->m || m_new > PLL_M_MAX) {
			pr_err("%s: M divider %u out of range, skip\n", oc->name, m_new);
			return;
		}
		actual = (unsigned int)div_u64((u64)e0->frequency * m_new, e0->m);
		actual = (actual / unit) * unit;
		if (pll_find(pll, actual / unit, NULL)) {
			pr_err("%s: PLL rate %u already in use, skip\n", oc->name, actual);
			return;
		}
		if (rcc_block) {
			rcc = ect_rcc_get_domain(rcc_block, oc->name);
			if (rcc && !(ru = lvl_unit(rcc->level_list[0], old0))) {
				pr_err("%s: RCC table does not match, skip\n", oc->name);
				return;
			}
		}
		if (minlock_block)
			minlock = ect_minlock_get_domain(minlock_block, oc->name);
	}

	/* ---------- apply ---------- */

	/*
	 * unlock Samsung's own disabled levels between the stock max and the
	 * target (e.g. GPU 1086/1200). Disabled levels *below* the stock max
	 * (e.g. CPU 345 MHz) stay off. L0 is handled below if replaced.
	 */
	for (l = 0; l < dvfs->num_of_level; l++)
		if (dvfs->list_level[l].level_en) {
			stock_top = dvfs->list_level[l].level;
			break;
		}
	for (l = replace ? 1 : 0; l < dvfs->num_of_level; l++) {
		if (dvfs->list_level[l].level_en || dvfs->list_level[l].level > oc->target ||
		    dvfs->list_level[l].level <= stock_top)
			continue;
		dvfs->list_level[l].level_en = 1;
		asv_raise_level(asv, l, oc->uv_delta, oc->uv_cap);
		unlocked++;
		pr_info("%s: unlocked Samsung level %u kHz (+%u uV)\n",
			oc->name, dvfs->list_level[l].level, oc->uv_delta);
	}
	/* stock max now includes the unlocked levels */
	for (l = 0; l < dvfs->num_of_level && unlocked; l++)
		if (dvfs->list_level[l].level_en) {
			if (dvfs->max_frequency < dvfs->list_level[l].level)
				dvfs->max_frequency = dvfs->list_level[l].level;
			break;
		}

	if (!replace)
		return;

	/* L0 in place: label, PLL member value, new PLL entry, voltage */
	{
		struct ect_pll_frequency *ne = pll_append(pll);

		if (!ne) {
			pr_err("%s: no room to append PLL rate, skip replace\n", oc->name);
			return;
		}
		/* pll_append moved the list: re-find the original top entry */
		e0 = pll_find(pll, v0, NULL);
		ne->frequency = actual;
		ne->p = e0->p;
		ne->m = m_new;
		ne->s = e0->s;
		ne->k = 0;
	}
	dvfs->list_dvfs_value[pll_idx] = actual / unit;
	dvfs->list_level[0].level = oc->target;
	dvfs->list_level[0].level_en = 1;
	if (dvfs->max_frequency < oc->target)
		dvfs->max_frequency = oc->target;

	asv->level_list[0] = oc->target / au;
	/* a level above Samsung's table gets extra voltage on top of uv_delta */
	asv_raise_level(asv, 0, oc->uv_delta + oc->top_extra, oc->uv_cap);

	if (rcc)
		rcc->level_list[0] = oc->target / ru;

	if (minlock)
		for (l = 0; l < minlock->num_of_level; l++)
			if (minlock->level[l].main_frequencies == old0)
				minlock->level[l].main_frequencies = oc->target;

	if (th && oc->thermal)
		for (f = 0; f < th->num_of_function; f++) {
			struct ect_ap_thermal_function *fn = &th->function_list[f];

			if (strncmp(fn->function_name, "CPU", 3))
				continue;
			for (r = 0; r < fn->num_of_range; r++)
				if (fn->range_list[r].max_frequency == old0)
					fn->range_list[r].max_frequency = oc->target;
		}

	pr_info("%s: L0 %u -> %u kHz in place (PLL %u, M %u, P %u, S %u), +%u uV (cap %u)\n",
		oc->name, old0, oc->target, actual, m_new, e0->p, e0->s,
		oc->uv_delta + oc->top_extra, oc->uv_cap);
}

/*
 * Move the throttling ranges of CPU/GPU up by CONFIG_ECT_OC_THERMAL_SHIFT C.
 * Range 0 (normal) and the last range (hardware trip / emergency) are kept;
 * shifted bounds stay strictly below the trip and in ascending order.
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
		for (r = 1; r < n - 1; r++) {
			unsigned int t = fn->range_list[r].lower_bound_temperature + shift;
			unsigned int max_t = trip - (n - 1 - r);

			if (t > max_t)
				t = max_t;
			fn->range_list[r].lower_bound_temperature = t;
			fn->range_list[r - 1].upper_bound_temperature = t;
		}
		pr_info("%s: throttling starts at %u C (+%d), trip %u C unchanged\n",
			fn->function_name, fn->range_list[1].lower_bound_temperature, shift, trip);
	}
}

void ect_oc_apply(void)
{
	int i;

	oc_shift_thermal();

	for (i = 0; i < ARRAY_SIZE(oc_domains); i++)
		oc_apply_domain(&oc_domains[i]);
}
