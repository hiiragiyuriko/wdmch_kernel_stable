// SPDX-License-Identifier: GPL-2.0-only
/*
 * GMT-G2227 PMIC MFD driver
 *
 * The G2227 is an I2C PMIC used on Realtek RTD129x boards (e.g. the WD
 * Monarch) to supply the SCPU/GPU/DDR rails for DVFS. This is a clean
 * pickup of the Realtek 4.9 vendor driver (drivers/mfd/{g2227-i2c.c,
 * g22xx-core.c}) merged into a single file and updated for the modern
 * i2c_driver probe/remove signatures. It exposes a regmap and spawns the
 * g2227-regulator child.
 *
 * Copyright (C) 2016-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 * Author: Simon Hsu <simon_hsu@realtek.com>
 */

#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/mfd/g2227.h>
#include <linux/mfd/g22xx.h>

static bool g2227_regmap_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2227_REG_INTR ... G2227_REG_PWRKEY:
	case G2227_REG_SYS_CONTROL ... G2227_REG_LDO2LDO3_MODE:
	case G2227_REG_DC2_NRMVOLT ... G2227_REG_VERSION:
		return true;
	}
	return false;
}

static bool g2227_regmap_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2227_REG_INTR_MASK ... G2227_REG_PWRKEY:
	case G2227_REG_SYS_CONTROL ... G2227_REG_LDO2LDO3_MODE:
	case G2227_REG_DC2_NRMVOLT ... G2227_REG_LDO2LDO3_SLPVOLT:
		return true;
	}
	return false;
}

static bool g2227_regmap_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2227_REG_INTR_MASK ... G2227_REG_PWRKEY:
	case G2227_REG_SYS_CONTROL:
	case G2227_REG_VERSION:
		return true;
	}
	return false;
}

static const struct regmap_config g2227_regmap_config = {
	.reg_bits         = 8,
	.val_bits         = 8,
	.max_register     = 0x20,
	.cache_type       = REGCACHE_RBTREE,
	.readable_reg     = g2227_regmap_readable_reg,
	.writeable_reg    = g2227_regmap_writeable_reg,
	.volatile_reg     = g2227_regmap_volatile_reg,
};

static const struct mfd_cell g2227_devs[] = {
	{
		.name = "g2227-regulator",
		.of_compatible = "gmt,g2227-regulator",
	},
};

static int g2227_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct g22xx_device *gdev;
	unsigned int rev;
	int ret;

	gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	gdev->regmap = devm_regmap_init_i2c(client, &g2227_regmap_config);
	if (IS_ERR(gdev->regmap)) {
		ret = PTR_ERR(gdev->regmap);
		dev_err(dev, "failed to allocate regmap: %d\n", ret);
		return ret;
	}

	/*
	 * First read after power-up can return stale data on this PMIC;
	 * the vendor driver does a throw-away read of the version register.
	 */
	regmap_read(gdev->regmap, G2227_REG_VERSION, &rev);

	ret = regmap_read(gdev->regmap, G2227_REG_VERSION, &rev);
	if (ret) {
		dev_err(dev, "failed to read version: %d\n", ret);
		return ret;
	}
	dev_info(dev, "g2227 rev%d\n", rev);

	gdev->chip_id = G22XX_DEVICE_ID_G2227;
	gdev->chip_rev = rev;
	gdev->dev = dev;
	i2c_set_clientdata(client, gdev);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, g2227_devs,
				    ARRAY_SIZE(g2227_devs), NULL, 0, NULL);
}

static const struct of_device_id g2227_of_match[] = {
	{ .compatible = "gmt,g2227", },
	{}
};
MODULE_DEVICE_TABLE(of, g2227_of_match);

static const struct i2c_device_id g2227_i2c_id[] = {
	{ "g2227", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, g2227_i2c_id);

static struct i2c_driver g2227_i2c_driver = {
	.driver = {
		.name = "g2227",
		.of_match_table = g2227_of_match,
	},
	.probe = g2227_i2c_probe,
	.id_table = g2227_i2c_id,
};
module_i2c_driver(g2227_i2c_driver);

MODULE_DESCRIPTION("GMT G2227 PMIC MFD Driver");
MODULE_AUTHOR("Simon Hsu <simon_hsu@realtek.com>");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
