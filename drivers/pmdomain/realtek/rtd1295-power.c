// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek RTD1295 power-domain (genpd) controller.
 *
 * Ported and consolidated from the Realtek 4.9 vendor rtk_pd driver
 * (drivers/soc/realtek/common/rtk_pd). The RTD1295 gates the video engines
 * (VE1..VE3), the Mali GPU and the NAT/network accelerator through a small
 * hierarchy of power islands in the CRT register block:
 *
 *	SRAM domain  ->  ISO (isolation) domain  ->  consumer domain
 *
 * Powering a block down means isolating it first (set the ISO bit) and only
 * then cutting its SRAM power; bringing it up reverses that. Driving the
 * external rail directly (e.g. the g2227 dc3 GPU supply) without this
 * sequencing hangs the SoC interconnect, which is why this controller exists.
 *
 * On this NAS none of these blocks has a driver, so once the domains are
 * registered the genpd core powers the unused ones down at late_initcall_sync
 * using the correct sequence above.
 *
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/bits.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <dt-bindings/power/rtd1295-power.h>

/* SRAM island registers, relative to the island's PWR0 base. */
#define SRAM_PWR4		0x10
#define SRAM_PWR5		0x14
#define SRAM_DONE		0x4	/* PWR5 power-sequence-done status */
#define SRAM_LAST_CH		0xf	/* last shut-down channel (PWR4[11:8]) */

enum rtd1295_pd_kind {
	PD_DUMMY,	/* container only, no register action */
	PD_ISO,		/* isolation bit in a shared register */
	PD_SRAM,	/* SRAM power sequencer */
};

struct rtd1295_pd_desc {
	const char *name;
	enum rtd1295_pd_kind kind;
	unsigned int reg;	/* PD_ISO: register; PD_SRAM: PWR0 base */
	unsigned int bit;	/* PD_ISO: isolation bit */
	unsigned int pwr5;	/* PD_SRAM: PWR5 offset (0 => reg + SRAM_PWR5) */
};

#define DOM_ISO(_name, _bit) \
	{ .name = _name, .kind = PD_ISO, .reg = 0x400, .bit = _bit }
#define DOM_SRAM(_name, _base) \
	{ .name = _name, .kind = PD_SRAM, .reg = _base }
#define DOM_SRAM_NC(_name, _base, _pwr5) \
	{ .name = _name, .kind = PD_SRAM, .reg = _base, .pwr5 = _pwr5 }
#define DOM_DUMMY(_name) \
	{ .name = _name, .kind = PD_DUMMY }

static const struct rtd1295_pd_desc rtd1295_pd_descs[RTD1295_PD_MAX] = {
	[RTD1295_PD_VE1]      = DOM_DUMMY("ve1"),
	[RTD1295_PD_VE2]      = DOM_DUMMY("ve2"),
	[RTD1295_PD_VE3]      = DOM_DUMMY("ve3"),
	[RTD1295_PD_GPU]      = DOM_DUMMY("gpu"),
	[RTD1295_PD_NAT]      = DOM_DUMMY("nat"),
	[RTD1295_PD_SRAM_VE1] = DOM_SRAM_NC("sram_ve1", 0x380, 0x3a8),
	[RTD1295_PD_SRAM_VE2] = DOM_SRAM("sram_ve2", 0x3c0),
	[RTD1295_PD_SRAM_VE3] = DOM_SRAM("sram_ve3", 0x3e0),
	[RTD1295_PD_SRAM_GPU] = DOM_SRAM_NC("sram_gpu", 0x394, 0x3ac),
	[RTD1295_PD_SRAM_NAT] = DOM_SRAM("sram_nat", 0x420),
	[RTD1295_PD_ISO_VE1]  = DOM_ISO("iso_ve1", 0),
	[RTD1295_PD_ISO_VE2]  = DOM_ISO("iso_ve2", 4),
	[RTD1295_PD_ISO_VE3]  = DOM_ISO("iso_ve3", 6),
	[RTD1295_PD_ISO_GPU]  = DOM_ISO("iso_gpu", 1),
	[RTD1295_PD_ISO_NAT]  = DOM_ISO("iso_nat", 18),
};

/* Power hierarchy as { parent, child } pairs (parent must be on for child). */
static const u8 rtd1295_pd_links[][2] = {
	{ RTD1295_PD_SRAM_VE1, RTD1295_PD_ISO_VE1 },
	{ RTD1295_PD_SRAM_VE2, RTD1295_PD_ISO_VE2 },
	{ RTD1295_PD_SRAM_VE3, RTD1295_PD_ISO_VE3 },
	{ RTD1295_PD_SRAM_GPU, RTD1295_PD_ISO_GPU },
	{ RTD1295_PD_SRAM_NAT, RTD1295_PD_ISO_NAT },
	{ RTD1295_PD_ISO_VE1,  RTD1295_PD_VE1 },
	{ RTD1295_PD_ISO_VE2,  RTD1295_PD_VE2 },
	{ RTD1295_PD_ISO_VE3,  RTD1295_PD_VE3 },
	{ RTD1295_PD_ISO_GPU,  RTD1295_PD_GPU },
	{ RTD1295_PD_ISO_NAT,  RTD1295_PD_NAT },
	{ RTD1295_PD_VE2,      RTD1295_PD_VE1 },
};

struct rtd1295_power {
	struct regmap *regmap;
	struct genpd_onecell_data onecell;
};

struct rtd1295_pd {
	struct generic_pm_domain genpd;
	struct rtd1295_power *power;
	const struct rtd1295_pd_desc *desc;
};

#define to_rtd1295_pd(p) container_of(p, struct rtd1295_pd, genpd)

/* ISO island: bit set = isolated (off), bit clear = released (on). */
static int rtd1295_iso_power(struct rtd1295_pd *pd, bool on)
{
	const struct rtd1295_pd_desc *d = pd->desc;

	return regmap_update_bits(pd->power->regmap, d->reg, BIT(d->bit),
				  on ? 0 : BIT(d->bit));
}

static bool rtd1295_iso_is_on(struct rtd1295_pd *pd)
{
	const struct rtd1295_pd_desc *d = pd->desc;
	unsigned int val;

	if (regmap_read(pd->power->regmap, d->reg, &val))
		return true;
	return !(val & BIT(d->bit));
}

/* SRAM island: PWR4[7:0] holds the target (0 = on, 1 = off); PWR5 == SRAM_DONE
 * signals the sequence completed and is then written back to clear it.
 */
static unsigned int rtd1295_sram_pwr5(const struct rtd1295_pd_desc *d)
{
	return d->pwr5 ? d->pwr5 : d->reg + SRAM_PWR5;
}

static int rtd1295_sram_power(struct rtd1295_pd *pd, bool on)
{
	const struct rtd1295_pd_desc *d = pd->desc;
	struct regmap *regmap = pd->power->regmap;
	unsigned int pwr4 = d->reg + SRAM_PWR4;
	unsigned int pwr5 = rtd1295_sram_pwr5(d);
	unsigned int target = on ? 0 : 1;
	unsigned int val;
	int ret = 0;

	if (regmap_read(regmap, pwr4, &val))
		return -EIO;

	if ((val & 0xff) != target) {
		regmap_write(regmap, pwr4, target | (SRAM_LAST_CH << 8));
		ret = regmap_read_poll_timeout(regmap, pwr5, val,
					       val == SRAM_DONE, 0, 500);
	}
	/* clear the sequence-done status */
	regmap_write(regmap, pwr5, SRAM_DONE);
	return ret;
}

static bool rtd1295_sram_is_on(struct rtd1295_pd *pd)
{
	unsigned int val;

	if (regmap_read(pd->power->regmap, pd->desc->reg + SRAM_PWR4, &val))
		return true;
	return (val & 0xff) == 0;
}

static int rtd1295_pd_power_on(struct generic_pm_domain *genpd)
{
	struct rtd1295_pd *pd = to_rtd1295_pd(genpd);

	switch (pd->desc->kind) {
	case PD_ISO:
		return rtd1295_iso_power(pd, true);
	case PD_SRAM:
		return rtd1295_sram_power(pd, true);
	default:
		return 0;
	}
}

static int rtd1295_pd_power_off(struct generic_pm_domain *genpd)
{
	struct rtd1295_pd *pd = to_rtd1295_pd(genpd);

	switch (pd->desc->kind) {
	case PD_ISO:
		return rtd1295_iso_power(pd, false);
	case PD_SRAM:
		return rtd1295_sram_power(pd, false);
	default:
		return 0;
	}
}

static bool rtd1295_pd_is_on(struct rtd1295_pd *pd)
{
	switch (pd->desc->kind) {
	case PD_ISO:
		return rtd1295_iso_is_on(pd);
	case PD_SRAM:
		return rtd1295_sram_is_on(pd);
	default:
		return true;
	}
}

static int rtd1295_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct genpd_onecell_data *data;
	struct rtd1295_power *power;
	int i, ret;

	power = devm_kzalloc(dev, sizeof(*power), GFP_KERNEL);
	if (!power)
		return -ENOMEM;

	/* the power islands live in the parent CRT syscon register block */
	power->regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(power->regmap))
		return dev_err_probe(dev, PTR_ERR(power->regmap),
				     "failed to get parent syscon\n");

	data = &power->onecell;
	data->num_domains = RTD1295_PD_MAX;
	data->domains = devm_kcalloc(dev, RTD1295_PD_MAX,
				     sizeof(*data->domains), GFP_KERNEL);
	if (!data->domains)
		return -ENOMEM;

	for (i = 0; i < RTD1295_PD_MAX; i++) {
		const struct rtd1295_pd_desc *desc = &rtd1295_pd_descs[i];
		struct rtd1295_pd *pd;

		if (!desc->name)
			continue;

		pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;

		pd->power = power;
		pd->desc = desc;
		pd->genpd.name = desc->name;
		pd->genpd.power_on = rtd1295_pd_power_on;
		pd->genpd.power_off = rtd1295_pd_power_off;

		ret = pm_genpd_init(&pd->genpd, NULL, !rtd1295_pd_is_on(pd));
		if (ret) {
			dev_err(dev, "failed to init domain %s: %d\n",
				desc->name, ret);
			return ret;
		}
		data->domains[i] = &pd->genpd;
	}

	for (i = 0; i < ARRAY_SIZE(rtd1295_pd_links); i++) {
		struct generic_pm_domain *parent = data->domains[rtd1295_pd_links[i][0]];
		struct generic_pm_domain *child = data->domains[rtd1295_pd_links[i][1]];

		ret = pm_genpd_add_subdomain(parent, child);
		if (ret)
			dev_warn(dev, "failed to link %s -> %s: %d\n",
				 parent->name, child->name, ret);
	}

	ret = of_genpd_add_provider_onecell(np, data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add genpd provider\n");

	dev_info(dev, "RTD1295 power-domain controller registered\n");
	return 0;
}

static const struct of_device_id rtd1295_power_match[] = {
	{ .compatible = "realtek,rtd1295-power" },
	{ }
};

static struct platform_driver rtd1295_power_driver = {
	.probe = rtd1295_power_probe,
	.driver = {
		.name = "rtd1295-power",
		.of_match_table = rtd1295_power_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(rtd1295_power_driver);
