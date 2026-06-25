// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek RTD129x SoC chip id / revision helper.
 *
 * Minimal bring-up implementation: reads the chip-info registers directly
 * (no DT node required) so drivers that need the silicon revision for
 * register tweaks (e.g. the on-SoC GMAC PHY init) get the correct value.
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 */

#include <linux/io.h>
#include <linux/types.h>
#include <linux/module.h>
#include <soc/realtek/rtk_chip.h>

/* chip-info block (RTD1295/RTD1296): id @ +0x0, revision @ +0x4 */
#define RTD_CHIPINFO_BASE	0x9801a200
#define RTD_CHIPINFO_SIZE	0x8
#define REG_CHIP_ID		0x0
#define REG_CHIP_REV		0x4

int get_rtd_chip_id(void)
{
	static int id = -1;
	void __iomem *base;
	u32 raw;

	if (id != -1)
		return id;

	base = ioremap(RTD_CHIPINFO_BASE, RTD_CHIPINFO_SIZE);
	if (!base)
		return CHIP_ID_UNKNOWN;
	raw = readl(base + REG_CHIP_ID);
	iounmap(base);

	switch (raw) {
	case 0x00006421: id = CHIP_ID_RTD1295; break;
	case 0x00006481: id = CHIP_ID_RTD1395; break;
	default:         id = CHIP_ID_UNKNOWN; break;
	}
	return id;
}
EXPORT_SYMBOL(get_rtd_chip_id);

int get_rtd_chip_revision(void)
{
	static int rev = RTD_CHIP_UNKNOWN_REV;
	void __iomem *base;
	u32 raw;

	if (rev != RTD_CHIP_UNKNOWN_REV)
		return rev;

	base = ioremap(RTD_CHIPINFO_BASE, RTD_CHIPINFO_SIZE);
	if (!base)
		return RTD_CHIP_UNKNOWN_REV;
	raw = readl(base + REG_CHIP_REV);
	iounmap(base);

	switch (raw) {
	case 0x00000000: rev = RTD_CHIP_A00; break;
	case 0x00010000: rev = RTD_CHIP_A01; break;
	case 0x00020000: rev = RTD_CHIP_B00; break;
	case 0x00030000: rev = RTD_CHIP_B01; break;
	/* default to the common production cut if the value is unexpected */
	default:         rev = RTD_CHIP_B00; break;
	}
	return rev;
}
EXPORT_SYMBOL(get_rtd_chip_revision);
