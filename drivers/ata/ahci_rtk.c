// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek RTD129x AHCI SATA host controller glue
 *
 * A clean reimplementation for mainline of the Realtek 4.9 vendor ahci_rtk
 * driver, built on the standard libahci_platform helpers. Only the pieces
 * required to bring up the SATA host on the RTD1295 (WD My Cloud Home /
 * Monarch board) are kept:
 *
 *   - clocks, resets and the SATA PHY are handled generically by
 *     ahci_platform_get_resources() / ahci_platform_enable_resources(),
 *   - rtk_sata_init() applies the SoC-specific MAC register tweaks that the
 *     vendor driver performed before starting the host.
 *
 * The vendor driver's GPIO-based disk power/hotplug management and the
 * background power-saving kthread are intentionally dropped: on this board
 * U-Boot already powers and boots from the SATA disk, so the drive is live
 * when Linux starts.
 *
 * Copyright (c) 2017 Realtek Semiconductor Corp.
 * Copyright (c) 2024 (mainline port)
 */

#include <linux/ahci_platform.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <soc/realtek/rtk_chip.h>
#include "ahci.h"

#define DRV_NAME "ahci_rtk"

/* SATA MAC register offsets (relative to the AHCI MMIO base) */
#define RTK_SATA_MAC_CTRL	0x00c	/* port enable */
#define RTK_SATA_RXERR_SEL	0xf20	/* RTD129x: rx error select */

static const struct ata_port_info ahci_rtk_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_platform_ops,
};

static const struct scsi_host_template ahci_rtk_sht = {
	AHCI_SHT(DRV_NAME),
};

/*
 * SoC-specific MAC initialisation, ported from the vendor rtk_sata_init().
 * Must run after clocks/resets/PHY are up but before the host is started.
 */
static void rtk_sata_init(struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = hpriv->mmio;
	int chip_id = get_rtd_chip_id();

	if ((chip_id & 0xfff0) == CHIP_ID_RTD129X)
		writel(0x0003c300, mmio + RTK_SATA_RXERR_SEL);

	/* enable the SATA ports */
	writel(readl(mmio + RTK_SATA_MAC_CTRL) | 0x3, mmio + RTK_SATA_MAC_CTRL);
}

static int ahci_rtk_probe(struct platform_device *pdev)
{
	struct ahci_host_priv *hpriv;
	int rc;

	hpriv = ahci_platform_get_resources(pdev, AHCI_PLATFORM_GET_RESETS);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	rtk_sata_init(hpriv);

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_rtk_port_info,
				     &ahci_rtk_sht);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static int ahci_rtk_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_suspend_host(dev);
	if (rc)
		return rc;

	ahci_platform_disable_resources(hpriv);
	return 0;
}

static int ahci_rtk_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	rtk_sata_init(hpriv);

	rc = ahci_platform_resume_host(dev);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ahci_rtk_pm_ops, ahci_rtk_suspend,
				ahci_rtk_resume);

static const struct of_device_id ahci_rtk_of_match[] = {
	{ .compatible = "realtek,rtd1295-ahci" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ahci_rtk_of_match);

static struct platform_driver ahci_rtk_driver = {
	.probe = ahci_rtk_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_rtk_of_match,
		.pm = &ahci_rtk_pm_ops,
	},
};
module_platform_driver(ahci_rtk_driver);

MODULE_DESCRIPTION("Realtek RTD129x AHCI SATA driver");
MODULE_LICENSE("GPL");
