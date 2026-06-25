// SPDX-License-Identifier: GPL-2.0-only
/*
 * GMT-G2227 PMIC regulator driver
 *
 * Clean pickup of the Realtek 4.9 vendor regulator support, merging the
 * shared g22xx core (drivers/regulator/g22xx-regulator-core.c) and the
 * g2227 descriptors (drivers/regulator/g2227-regulator.c) into a single
 * self-contained file (we only support the g2227, not the g2237, so the
 * exported-symbol split is unnecessary).
 *
 * Bound as the regulator child of the g2227 MFD; it drives the SCPU
 * (dc2), GPU (dc3) and other rails used for RTD129x DVFS.
 *
 * Copyright (C) 2016-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 * Author: Simon Hsu <simon_hsu@realtek.com>
 */

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/suspend.h>
#include <linux/mfd/g2227.h>
#include <linux/mfd/g22xx.h>
#include <dt-bindings/regulator/gmt,g22xx.h>

struct g22xx_regulator_desc {
	struct regulator_desc desc;
	u8 nmode_reg;
	u8 nmode_mask;
	u8 smode_reg;
	u8 smode_mask;
	u8 svsel_reg;
	u8 svsel_mask;
};

struct g22xx_regulator_data {
	struct list_head list;
	struct regulator_dev *rdev;
	struct g22xx_regulator_desc *gd;

	struct regmap_field *svsel;
	struct regmap_field *nmode;
	struct regmap_field *smode;

	struct regulator_state state_mem;
	struct regulator_state state_coldboot;

	u32 fixed_uV;
};

struct g22xx_regulator_device {
	struct device *dev;
	struct regmap *regmap;
	struct list_head list;
};

static unsigned int g22xx_regulator_dc_of_map_mode(unsigned int mode);
static unsigned int g22xx_regulator_ldo_of_map_mode(unsigned int mode);

static inline int g22xx_regulator_type_is_ldo(struct g22xx_regulator_desc *gd)
{
	return gd->desc.of_map_mode == g22xx_regulator_ldo_of_map_mode;
}

/* of_parse_cb */
static int g22xx_regulator_of_parse_cb(struct device_node *np,
	const struct regulator_desc *desc, struct regulator_config *config)
{
	struct g22xx_regulator_data *data = config->driver_data;
	struct device_node *child;
	unsigned int val;

	child = of_get_child_by_name(np, "regulator-state-coldboot");
	if (!child)
		child = of_get_child_by_name(np, "regulator-state-mem");
	if (child) {
		struct regulator_state *state = &data->state_coldboot;

		if (of_property_read_bool(child, "regulator-on-in-suspend"))
			state->enabled = ENABLE_IN_SUSPEND;

		if (of_property_read_bool(child, "regulator-off-in-suspend"))
			state->enabled = DISABLE_IN_SUSPEND;

		if (!of_property_read_u32(child, "regulator-suspend-microvolt",
			&val))
			state->uV = val;

		of_node_put(child);
	}

	if (desc->n_voltages == 1 && desc->fixed_uV == 0) {
		u32 min = 0, max = 0;

		of_property_read_u32(np, "regulator-min-microvolt", &min);
		of_property_read_u32(np, "regulator-max-microvolt", &max);
		WARN_ON(min != max);
		data->fixed_uV = max;
	}

	return 0;
}

static unsigned int g22xx_regulator_dc_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case G22XX_DC_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	default:
		break;
	}
	return REGULATOR_MODE_NORMAL;
}

static unsigned int g22xx_regulator_ldo_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case G22XX_LDO_MODE_ECO:
		return REGULATOR_MODE_IDLE;
	default:
		break;
	}
	return REGULATOR_MODE_NORMAL;
}

/* regulator_ops */
static int g22xx_regulator_set_mode_regmap(struct regulator_dev *rdev,
	unsigned int mode)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);
	struct g22xx_regulator_desc *gd = data->gd;
	unsigned int val = 0;

	if (!data->nmode)
		return -EINVAL;

	if (g22xx_regulator_type_is_ldo(gd))
		val = (mode & REGULATOR_MODE_IDLE) ? 2 : 0;
	else
		val = (mode & REGULATOR_MODE_FAST) ? 2 : 0;

	return regmap_field_write(data->nmode, val);
}

static unsigned int g22xx_regulator_get_mode_regmap(struct regulator_dev *rdev)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);
	struct g22xx_regulator_desc *gd = data->gd;
	unsigned int val;
	int ret;

	if (!data->nmode)
		return -EINVAL;

	ret = regmap_field_read(data->nmode, &val);
	if (ret)
		return 0;

	if (g22xx_regulator_type_is_ldo(gd) && val == 2)
		return REGULATOR_MODE_IDLE;
	else if (val == 2)
		return REGULATOR_MODE_FAST;
	return REGULATOR_MODE_NORMAL;
}

static int g22xx_set_suspend_enable(struct regulator_dev *rdev)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);

	if (!data->smode)
		return -EINVAL;
	regmap_field_write(data->smode, 0x1);
	return 0;
}

static int g22xx_set_suspend_disable(struct regulator_dev *rdev)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);

	if (!data->smode)
		return -EINVAL;
	regmap_field_write(data->smode, 0x3);
	return 0;
}

static int g22xx_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);
	int vsel;

	vsel = regulator_map_voltage_iterate(rdev, uV, uV);
	if (vsel < 0)
		return -EINVAL;

	if (!data->smode || !data->svsel)
		return -EINVAL;

	regmap_field_write(data->smode, 0x2);
	regmap_field_write(data->svsel, vsel);
	return 0;
}

static int g22xx_regulator_get_voltage_fixed(struct regulator_dev *rdev)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);

	if (data->fixed_uV)
		return data->fixed_uV;
	return -EINVAL;
}

static const struct regulator_ops g22xx_regulator_ops = {
	.list_voltage         = regulator_list_voltage_table,
	.map_voltage          = regulator_map_voltage_iterate,
	.set_voltage_sel      = regulator_set_voltage_sel_regmap,
	.get_voltage_sel      = regulator_get_voltage_sel_regmap,
	.enable               = regulator_enable_regmap,
	.disable              = regulator_disable_regmap,
	.is_enabled           = regulator_is_enabled_regmap,
	.get_mode             = g22xx_regulator_get_mode_regmap,
	.set_mode             = g22xx_regulator_set_mode_regmap,
	.set_suspend_voltage  = g22xx_set_suspend_voltage,
	.set_suspend_enable   = g22xx_set_suspend_enable,
	.set_suspend_disable  = g22xx_set_suspend_disable,
};

static const struct regulator_ops g22xx_regulator_fixed_uV_ops = {
	.enable               = regulator_enable_regmap,
	.disable              = regulator_disable_regmap,
	.is_enabled           = regulator_is_enabled_regmap,
	.get_mode             = g22xx_regulator_get_mode_regmap,
	.set_mode             = g22xx_regulator_set_mode_regmap,
	.set_suspend_voltage  = g22xx_set_suspend_voltage,
	.set_suspend_enable   = g22xx_set_suspend_enable,
	.set_suspend_disable  = g22xx_set_suspend_disable,
	.get_voltage          = g22xx_regulator_get_voltage_fixed,
};

static int g22xx_regulator_set_state(struct regulator_dev *rdev, int is_coldboot)
{
	struct g22xx_regulator_data *data = rdev_get_drvdata(rdev);
	struct device *dev = rdev_get_dev(rdev);
	struct regulator_state *rstate;

	rstate = &rdev->constraints->state_mem;
	*rstate = is_coldboot ? data->state_coldboot : data->state_mem;

	if (!data->smode)
		return -EINVAL;

	if (rstate->enabled == ENABLE_IN_SUSPEND && rstate->uV) {
		int vsel;

		vsel = regulator_map_voltage_iterate(rdev, rstate->uV,
						     rstate->uV);
		if (vsel < 0)
			return -EINVAL;

		if (!data->svsel)
			return -EINVAL;

		dev_dbg(dev, "set sleep_mode to volt-%d\n", rstate->uV);
		regmap_field_write(data->smode, 0x2);
		regmap_field_write(data->svsel, vsel);
		return 0;
	}

	if (rstate->enabled == DISABLE_IN_SUSPEND) {
		dev_dbg(dev, "set sleep_mode to disabled\n");
		regmap_field_write(data->smode, 0x3);
		return 0;
	}

	return -EINVAL;
}

static struct regmap_field *create_regmap_field(
		struct g22xx_regulator_device *grdev, u32 reg, u32 mask)
{
	u32 msb = fls(mask) - 1;
	u32 lsb = ffs(mask) - 1;
	struct reg_field map = REG_FIELD(reg, lsb, msb);
	struct regmap_field *rmap;

	if (reg == 0 && mask == 0)
		return NULL;

	rmap = devm_regmap_field_alloc(grdev->dev, grdev->regmap, map);
	if (IS_ERR(rmap))
		dev_err(grdev->dev, "regmap_field_alloc() for (reg=%02x, mask=%02x) returns %ld\n",
			reg, mask, PTR_ERR(rmap));
	return rmap;
}

static struct regulator_dev *g22xx_regulator_register(
		struct g22xx_regulator_device *grdev,
		struct g22xx_regulator_desc *gd)
{
	struct device *dev = grdev->dev;
	struct g22xx_regulator_data *data;
	struct regulator_config config = {};
	struct regulation_constraints *c;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	data->gd = gd;

	data->nmode = create_regmap_field(grdev, gd->nmode_reg, gd->nmode_mask);
	if (IS_ERR(data->nmode))
		return ERR_CAST(data->nmode);
	data->smode = create_regmap_field(grdev, gd->smode_reg, gd->smode_mask);
	if (IS_ERR(data->smode))
		return ERR_CAST(data->smode);
	data->svsel = create_regmap_field(grdev, gd->svsel_reg, gd->svsel_mask);
	if (IS_ERR(data->svsel))
		return ERR_CAST(data->svsel);

	config.dev         = grdev->dev;
	config.regmap      = grdev->regmap;
	config.driver_data = data;

	data->rdev = devm_regulator_register(dev, &gd->desc, &config);
	if (IS_ERR(data->rdev))
		return data->rdev;

	c = data->rdev->constraints;

	/*
	 * Copy state_mem to data for mem/coldboot switching. If there is no
	 * sleep uV and mode, set the state to enabled.
	 */
	data->state_mem = c->state_mem;
	if (data->state_mem.uV == 0 &&
	    data->state_mem.enabled != DISABLE_IN_SUSPEND) {
		data->state_mem.enabled = ENABLE_IN_SUSPEND;
		c->state_mem = data->state_mem;
	}
	if (data->state_coldboot.uV == 0 &&
	    data->state_coldboot.enabled != DISABLE_IN_SUSPEND)
		data->state_coldboot.enabled = ENABLE_IN_SUSPEND;

	/* allow runtime mode changes */
	c->valid_modes_mask |= REGULATOR_MODE_NORMAL;
	if (g22xx_regulator_type_is_ldo(data->gd))
		c->valid_modes_mask |= REGULATOR_MODE_IDLE;
	else
		c->valid_modes_mask |= REGULATOR_MODE_FAST;
	c->valid_ops_mask |= REGULATOR_CHANGE_MODE;

	list_add_tail(&data->list, &grdev->list);

	return data->rdev;
}

/* g2227 regulator id */
enum g2227_regulator_id {
	G2227_ID_DC1 = 0,
	G2227_ID_DC2,
	G2227_ID_DC3,
	G2227_ID_DC4,
	G2227_ID_DC5,
	G2227_ID_DC6,
	G2227_ID_LDO2,
	G2227_ID_LDO3,
	G2227_ID_MAX
};

/* voltage tables */
static const unsigned int dcdc1_vtbl[] = {
	3000000, 3100000, 3200000, 3300000,
};

static const unsigned int dcdcx_vtbl[] = {
	 800000,  812500,  825000,  837500,  850000,  862500,  875000,  887500,
	 900000,  912500,  925000,  937500,  950000,  962500,  975000,  987500,
	1000000, 1012500, 1025000, 1037500, 1050000, 1062500, 1075000, 1087500,
	1100000, 1112500, 1125000, 1137500, 1150000, 1162500, 1175000, 1187500,
};

static const unsigned int ldo_vtbl[] = {
	 800000,  850000,  900000,  950000, 1000000, 1100000, 1200000, 1300000,
	1500000, 1600000, 1800000, 1900000, 2500000, 2600000, 3000000, 3100000,
};

#define G2227_DESC(_id, _name, _vtbl, _type)                       \
{                                                                  \
	.desc = {                                                  \
		.owner       = THIS_MODULE,                        \
		.type        = REGULATOR_VOLTAGE,                  \
		.ops         = &g22xx_regulator_ops,               \
		.name        = _name,                              \
		.of_match    = _name,                              \
		.n_voltages  = ARRAY_SIZE(_vtbl),                  \
		.volt_table  = _vtbl,                              \
		.id          = G2227_ID_ ## _id,                   \
		.vsel_reg    = G2227_REG_ ## _id ## _NRMVOLT,      \
		.vsel_mask   = G2227_ ## _id ## _NRMVOLT_MASK,     \
		.enable_reg  = G2227_REG_ONOFF,                    \
		.enable_mask = G2227_ ## _id ## _ON_MASK,          \
		.enable_val  = G2227_ ## _id ## _ON_MASK,          \
		.of_map_mode = g22xx_regulator_ ## _type ##_of_map_mode, \
		.of_parse_cb = g22xx_regulator_of_parse_cb,        \
	},                                                         \
	.nmode_reg  = G2227_REG_ ## _id ## _MODE,                  \
	.nmode_mask = G2227_ ## _id ## _NRMMODE_MASK,              \
	.smode_reg  = G2227_REG_ ## _id ## _MODE,                  \
	.smode_mask = G2227_ ## _id ## _SLPMODE_MASK,              \
	.svsel_reg  = G2227_REG_ ## _id ## _SLPVOLT,               \
	.svsel_mask = G2227_ ## _id ## _SLPVOLT_MASK,              \
}

#define G2227_DESC_FIXED_UV(_id, _name, _fixed_uV)                 \
{                                                                  \
	.desc = {                                                  \
		.owner       = THIS_MODULE,                        \
		.type        = REGULATOR_VOLTAGE,                  \
		.ops         = &g22xx_regulator_fixed_uV_ops,      \
		.name        = _name,                              \
		.of_match    = _name,                              \
		.n_voltages  = 1,                                  \
		.fixed_uV    = _fixed_uV,                          \
		.id          = G2227_ID_ ## _id,                   \
		.enable_reg  = G2227_REG_ONOFF,                    \
		.enable_mask = G2227_ ## _id ## _ON_MASK,          \
		.enable_val  = G2227_ ## _id ## _ON_MASK,          \
		.of_map_mode = g22xx_regulator_dc_of_map_mode,     \
		.of_parse_cb = g22xx_regulator_of_parse_cb,        \
	},                                                         \
	.nmode_reg  = G2227_REG_ ## _id ## _MODE,                  \
	.nmode_mask = G2227_ ## _id ## _NRMMODE_MASK,              \
	.smode_reg  = G2227_REG_ ## _id ## _MODE,                  \
	.smode_mask = G2227_ ## _id ## _SLPMODE_MASK,              \
}

static struct g22xx_regulator_desc g2227_desc[G2227_ID_MAX] = {
	[G2227_ID_DC1]  = G2227_DESC(DC1,  "dc1",  dcdc1_vtbl, dc),
	[G2227_ID_DC2]  = G2227_DESC(DC2,  "dc2",  dcdcx_vtbl, dc),
	[G2227_ID_DC3]  = G2227_DESC(DC3,  "dc3",  dcdcx_vtbl, dc),
	[G2227_ID_DC4]  = G2227_DESC_FIXED_UV(DC4, "dc4", 0),
	[G2227_ID_DC5]  = G2227_DESC(DC5,  "dc5",  dcdcx_vtbl, dc),
	[G2227_ID_DC6]  = G2227_DESC(DC6,  "dc6",  dcdcx_vtbl, dc),
	[G2227_ID_LDO2] = G2227_DESC(LDO2, "ldo2", ldo_vtbl,   ldo),
	[G2227_ID_LDO3] = G2227_DESC(LDO3, "ldo3", ldo_vtbl,   ldo),
};

static int g2227_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct g22xx_device *gdev = dev_get_drvdata(dev->parent);
	struct g22xx_regulator_device *grdev;
	int i, ret;

	grdev = devm_kzalloc(dev, sizeof(*grdev), GFP_KERNEL);
	if (!grdev)
		return -ENOMEM;

	grdev->regmap = gdev->regmap;
	grdev->dev = dev;
	INIT_LIST_HEAD(&grdev->list);

	for (i = 0; i < ARRAY_SIZE(g2227_desc); i++) {
		struct regulator_dev *rdev;

		rdev = g22xx_regulator_register(grdev, &g2227_desc[i]);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "failed to register %s: %d\n",
				g2227_desc[i].desc.name, ret);
			return ret;
		}
	}

	platform_set_drvdata(pdev, grdev);
	dev_info(dev, "initialized\n");
	return 0;
}

static void g2227_regulator_shutdown(struct platform_device *pdev)
{
	struct g22xx_regulator_device *grdev = platform_get_drvdata(pdev);
	struct g22xx_regulator_data *pos;

	list_for_each_entry(pos, &grdev->list, list)
		g22xx_regulator_set_state(pos->rdev, 1);
}

static int g2227_regulator_suspend(struct device *dev)
{
	struct g22xx_regulator_device *grdev = dev_get_drvdata(dev);
	struct g22xx_regulator_data *pos;

	list_for_each_entry(pos, &grdev->list, list)
		g22xx_regulator_set_state(pos->rdev, 0);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(g2227_regulator_pm_ops,
				g2227_regulator_suspend, NULL);

static const struct of_device_id g2227_regulator_ids[] = {
	{ .compatible = "gmt,g2227-regulator", },
	{}
};
MODULE_DEVICE_TABLE(of, g2227_regulator_ids);

static struct platform_driver g2227_regulator_driver = {
	.driver = {
		.name = "g2227-regulator",
		.pm = pm_sleep_ptr(&g2227_regulator_pm_ops),
		.of_match_table = g2227_regulator_ids,
	},
	.probe    = g2227_regulator_probe,
	.shutdown = g2227_regulator_shutdown,
};
module_platform_driver(g2227_regulator_driver);

MODULE_DESCRIPTION("GMT G2227 PMIC Regulator Driver");
MODULE_AUTHOR("Simon Hsu <simon_hsu@realtek.com>");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
