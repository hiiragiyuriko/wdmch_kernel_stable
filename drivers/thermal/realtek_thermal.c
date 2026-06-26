// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek RTD1295 on-SoC thermal sensor.
 *
 * Minimal mainline driver for the temperature sensor in the SCPU wrapper
 * (0x9801d150). It registers a thermal zone via the OF thermal framework;
 * CPU throttling is handled by the generic cpufreq cooling device bound in
 * the device tree's cooling-maps, so no SoC-specific cooling driver is
 * needed (unlike the vendor's core-hotplug cpu_core_cooling).
 *
 * Read logic ported from the Realtek 4.9 vendor sensor-rtd129x.c.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define TM_SENSOR_CTRL2		0x08
#define TM_SENSOR_STATUS1	0x18

/* CTRL2 sequence that (re)starts continuous conversion. */
#define TM_CTRL2_RESET0		0x01904001
#define TM_CTRL2_RESET1		0x01924001

/* The sensor needs a short settling time after reset. */
#define TM_RESET_SETTLE_US	25000

struct rtd1295_thermal {
	void __iomem *base;
};

static void rtd1295_thermal_reset(struct rtd1295_thermal *priv)
{
	writel(TM_CTRL2_RESET0, priv->base + TM_SENSOR_CTRL2);
	writel(TM_CTRL2_RESET1, priv->base + TM_SENSOR_CTRL2);
}

static int rtd1295_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct rtd1295_thermal *priv = thermal_zone_device_priv(tz);
	u32 val;
	int t;

	val = readl(priv->base + TM_SENSOR_STATUS1);

	/* STATUS1 holds a signed 19-bit value in units of 1/1024 degree C. */
	t = sign_extend32(val, 18) * 1000 / 1024;

	if (t < -30000 || t > 150000)
		return -EAGAIN;

	*temp = t;
	return 0;
}

static const struct thermal_zone_device_ops rtd1295_thermal_ops = {
	.get_temp = rtd1295_thermal_get_temp,
};

static int rtd1295_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtd1295_thermal *priv;
	struct thermal_zone_device *tz;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	rtd1295_thermal_reset(priv);
	usleep_range(TM_RESET_SETTLE_US, TM_RESET_SETTLE_US + 5000);

	tz = devm_thermal_of_zone_register(dev, 0, priv, &rtd1295_thermal_ops);
	if (IS_ERR(tz))
		return dev_err_probe(dev, PTR_ERR(tz),
				     "failed to register thermal zone\n");

	return 0;
}

static const struct of_device_id rtd1295_thermal_of_match[] = {
	{ .compatible = "realtek,rtd1295-thermal-sensor" },
	{ }
};
MODULE_DEVICE_TABLE(of, rtd1295_thermal_of_match);

static struct platform_driver rtd1295_thermal_driver = {
	.probe = rtd1295_thermal_probe,
	.driver = {
		.name = "rtd1295-thermal",
		.of_match_table = rtd1295_thermal_of_match,
	},
};
module_platform_driver(rtd1295_thermal_driver);

MODULE_DESCRIPTION("Realtek RTD1295 thermal sensor driver");
MODULE_LICENSE("GPL");
