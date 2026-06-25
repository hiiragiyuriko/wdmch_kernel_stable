// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek RTD129x interrupt mux (demux) driver
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 *
 * The RTD1295/RTD1296 route a large number of peripheral interrupts (UARTs,
 * I2C, GPIO, RTC, ...) through two "mux" blocks (MISC and ISO), each of which
 * raises a single GIC SPI. Each mux has a 32-bit status register and a 32-bit
 * enable register; the bit position in the status register identifies the
 * source, and a per-source mapping table gives the corresponding enable bit.
 *
 * Ported from the Realtek 4.9 vendor kernel to the mainline 6.18 irqdomain API
 * (linear domain, generic_handle_domain_irq).
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define DEV_NAME "rtk-irq-mux"

#define IRQ_INMUX 32

enum misc_int_en {
	MISC_INT_FAIL = 0xFF,
	MISC_INT_RVD = 0xFE,
	MISC_INT_EN_FAN = 29,
	MISC_INT_EN_I2C3 = 28,
	MISC_INT_EN_GSPI = 27,
	MISC_INT_EN_I2C2 = 26,
	MISC_INT_EN_SC0 = 24,
	MISC_INT_EN_LSADC1 = 22,
	MISC_INT_EN_LSADC0 = 21,
	MISC_INT_EN_GPIODA = 20,
	MISC_INT_EN_GPIOA = 19,
	MISC_INT_EN_I2C4 = 15,
	MISC_INT_EN_I2C5 = 14,
	MISC_INT_EN_RTC_DATA = 12,
	MISC_INT_EN_RTC_HOUR = 11,
	MISC_INT_EN_RTC_MIN = 10,
	MISC_INT_EN_UR2 = 7,
	MISC_INT_EN_UR2_TO = 6,
	MISC_INT_EN_UR1_TO = 5,
	MISC_INT_EN_UR1 = 3,
};

enum iso_int_en {
	ISO_INT_FAIL = 0xFF,
	ISO_INT_RVD = 0xFE,
	ISO_INT_EN_I2C1_REQ = 31,
	ISO_INT_EN_GPHY_AV = 30,
	ISO_INT_EN_GPHY_DV = 29,
	ISO_INT_EN_GPIODA = 20,
	ISO_INT_EN_GPIOA = 19,
	ISO_INT_EN_RTC_ALARM = 13,
	ISO_INT_EN_RTC_HSEC = 12,
	ISO_INT_EN_I2C1 = 11,
	ISO_INT_EN_I2C0 = 8,
	ISO_INT_EN_IRDA = 5,
	ISO_INT_EN_UR0 = 2,
};

/* status-bit -> enable-bit mapping for [0] = MISC mux, [1] = ISO mux */
static const u8 irq_map_tab[2][IRQ_INMUX] = {
	{
	MISC_INT_FAIL,		/* Bit0 */
	MISC_INT_FAIL,		/* Bit1 */
	MISC_INT_RVD,		/* Bit2 */
	MISC_INT_EN_UR1,	/* Bit3 */
	MISC_INT_FAIL,		/* Bit4 */
	MISC_INT_EN_UR1_TO,	/* Bit5 */
	MISC_INT_RVD,		/* Bit6 */
	MISC_INT_RVD,		/* Bit7 */
	MISC_INT_EN_UR2,	/* Bit8 */
	MISC_INT_RVD,		/* Bit9 */
	MISC_INT_EN_RTC_MIN,	/* Bit10 */
	MISC_INT_EN_RTC_HOUR,	/* Bit11 */
	MISC_INT_EN_RTC_DATA,	/* Bit12 */
	MISC_INT_EN_UR2_TO,	/* Bit13 */
	MISC_INT_EN_I2C5,	/* Bit14 */
	MISC_INT_EN_I2C4,	/* Bit15 */
	MISC_INT_FAIL,		/* Bit16 */
	MISC_INT_FAIL,		/* Bit17 */
	MISC_INT_FAIL,		/* Bit18 */
	MISC_INT_EN_GPIOA,	/* Bit19 */
	MISC_INT_EN_GPIODA,	/* Bit20 */
	MISC_INT_EN_LSADC0,	/* Bit21 */
	MISC_INT_EN_LSADC1,	/* Bit22 */
	MISC_INT_EN_I2C3,	/* Bit23 */
	MISC_INT_EN_SC0,	/* Bit24 */
	MISC_INT_FAIL,		/* Bit25 */
	MISC_INT_EN_I2C2,	/* Bit26 */
	MISC_INT_EN_GSPI,	/* Bit27 */
	MISC_INT_FAIL,		/* Bit28 */
	MISC_INT_EN_FAN,	/* Bit29 */
	MISC_INT_FAIL,		/* Bit30 */
	MISC_INT_FAIL,		/* Bit31 */
	},
	{
	ISO_INT_FAIL,		/* Bit0 */
	ISO_INT_RVD,		/* Bit1 */
	ISO_INT_EN_UR0,		/* Bit2 */
	ISO_INT_FAIL,		/* Bit3 */
	ISO_INT_FAIL,		/* Bit4 */
	ISO_INT_EN_IRDA,	/* Bit5 */
	ISO_INT_FAIL,		/* Bit6 */
	ISO_INT_RVD,		/* Bit7 */
	ISO_INT_EN_I2C0,	/* Bit8 */
	ISO_INT_RVD,		/* Bit9 */
	ISO_INT_FAIL,		/* Bit10 */
	ISO_INT_EN_I2C1,	/* Bit11 */
	ISO_INT_EN_RTC_HSEC,	/* Bit12 */
	ISO_INT_EN_RTC_ALARM,	/* Bit13 */
	ISO_INT_FAIL,		/* Bit14 */
	ISO_INT_FAIL,		/* Bit15 */
	ISO_INT_FAIL,		/* Bit16 */
	ISO_INT_FAIL,		/* Bit17 */
	ISO_INT_FAIL,		/* Bit18 */
	ISO_INT_EN_GPIOA,	/* Bit19 */
	ISO_INT_EN_GPIODA,	/* Bit20 */
	ISO_INT_RVD,		/* Bit21 */
	ISO_INT_RVD,		/* Bit22 */
	ISO_INT_RVD,		/* Bit23 */
	ISO_INT_RVD,		/* Bit24 */
	ISO_INT_FAIL,		/* Bit25 */
	ISO_INT_FAIL,		/* Bit26 */
	ISO_INT_FAIL,		/* Bit27 */
	ISO_INT_FAIL,		/* Bit28 */
	ISO_INT_EN_GPHY_DV,	/* Bit29 */
	ISO_INT_EN_GPHY_AV,	/* Bit30 */
	ISO_INT_EN_I2C1_REQ,	/* Bit31 */
	}
};

static DEFINE_SPINLOCK(irq_mux_lock);

struct irq_mux_data {
	void __iomem *base;
	unsigned char index;
	unsigned int irq;
	unsigned int irq_offset;
	u32 intr_status;
	u32 intr_en;
};

static struct irq_domain *rtk_domain;

static void mux_mask_irq(struct irq_data *data)
{
	struct irq_mux_data *mux_data = irq_data_get_irq_chip_data(data);

	mux_data += (data->hwirq / IRQ_INMUX);

	/* W1C: writing the bit acknowledges/masks the source */
	__raw_writel(BIT(data->hwirq % IRQ_INMUX),
		     mux_data->base + mux_data->intr_status);
}

static void mux_unmask_irq(struct irq_data *data)
{
	struct irq_mux_data *mux_data = irq_data_get_irq_chip_data(data);
	void __iomem *base;
	u32 reg_en;
	u8 en_offset;

	mux_data += (data->hwirq / IRQ_INMUX);
	base = mux_data->base;
	reg_en = mux_data->intr_en;

	en_offset = irq_map_tab[mux_data->index][data->hwirq % IRQ_INMUX];

	if (en_offset != MISC_INT_RVD && en_offset != MISC_INT_FAIL)
		__raw_writel(__raw_readl(base + reg_en) | BIT(en_offset),
			     base + reg_en);
	else if (en_offset == MISC_INT_FAIL)
		pr_err("[%s] Enable irq(%lu) fail\n", DEV_NAME, data->hwirq);
}

static void mux_disable_irq(struct irq_data *data)
{
	struct irq_mux_data *mux_data = irq_data_get_irq_chip_data(data);
	void __iomem *base;
	u32 reg_en;
	u8 en_offset;

	mux_data += (data->hwirq / IRQ_INMUX);
	base = mux_data->base;
	reg_en = mux_data->intr_en;

	en_offset = irq_map_tab[mux_data->index][data->hwirq % IRQ_INMUX];

	if (en_offset != MISC_INT_RVD && en_offset != MISC_INT_FAIL)
		__raw_writel(__raw_readl(base + reg_en) & ~BIT(en_offset),
			     base + reg_en);
	else if (en_offset == MISC_INT_FAIL)
		pr_err("[%s] Disable irq(%lu) fail\n", DEV_NAME, data->hwirq);
}

#ifdef CONFIG_SMP
static int mux_set_affinity(struct irq_data *d,
			    const struct cpumask *mask_val, bool force)
{
	struct irq_mux_data *mux_data = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_get_chip(mux_data->irq);
	struct irq_data *data = irq_get_irq_data(mux_data->irq);

	if (chip && chip->irq_set_affinity)
		return chip->irq_set_affinity(data, mask_val, force);

	return -EINVAL;
}
#endif

static struct irq_chip mux_chip = {
	.name		= DEV_NAME,
	.irq_mask	= mux_mask_irq,
	.irq_unmask	= mux_unmask_irq,
	.irq_disable	= mux_disable_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity = mux_set_affinity,
#endif
};

static void mux_irq_handle(struct irq_desc *desc)
{
	struct irq_mux_data *mux_data = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 reg_st = mux_data->intr_status;
	u32 reg_en = mux_data->intr_en;
	u32 status, enable, serviced = 0, stuck;
	int i;

	chained_irq_enter(chip, desc);

	spin_lock(&irq_mux_lock);
	enable = __raw_readl(mux_data->base + reg_en);
	status = __raw_readl(mux_data->base + reg_st);
	spin_unlock(&irq_mux_lock);

	for (i = 0; i < IRQ_INMUX; i++) {
		u8 en_offset;

		if (!(status & BIT(i)))
			continue;

		en_offset = irq_map_tab[mux_data->index][i];

		/*
		 * Only service a source whose mux enable bit is actually set,
		 * i.e. one some driver requested. Reserved (RVD) and unused
		 * (FAIL) table entries, and real peripherals whose status bit
		 * is latched but whose mux interrupt was never enabled, are
		 * left untouched: several ISO sources (UART0, the GPHY, a
		 * couple of reserved bits) sit permanently asserted and would
		 * otherwise be dispatched on every single parent interrupt.
		 */
		if (en_offset >= IRQ_INMUX || !(enable & BIT(en_offset)))
			continue;

		serviced |= BIT(i);
		if (generic_handle_domain_irq(rtk_domain, mux_data->irq_offset + i))
			pr_err_ratelimited("[%s] irq(%u) desc not found (st:0x%08x en:0x%08x)\n",
					   DEV_NAME, mux_data->irq_offset + i,
					   status, enable);
	}

	/*
	 * If a level source we serviced could not be cleared by its own
	 * handler its status bit stays asserted and we would loop forever;
	 * force-ack any such bit as a last resort. Only bits we actually
	 * serviced are considered -- permanently-latched unenabled sources
	 * must not trip this.
	 */
	spin_lock(&irq_mux_lock);
	stuck = __raw_readl(mux_data->base + reg_st) & serviced;
	if (stuck) {
		pr_err_ratelimited("[%s] %s irq stuck, clearing (st:0x%08x en:0x%08x)\n",
				   DEV_NAME, mux_data->index ? "ISO" : "MISC",
				   stuck, enable);
		__raw_writel(stuck, mux_data->base + reg_st);
	}
	spin_unlock(&irq_mux_lock);

	chained_irq_exit(chip, desc);
}

static int mux_irq_domain_xlate(struct irq_domain *d,
				struct device_node *controller,
				const u32 *intspec, unsigned int intsize,
				unsigned long *out_hwirq, unsigned int *out_type)
{
	if (controller != irq_domain_get_of_node(d))
		return -EINVAL;

	if (intsize < 2)
		return -EINVAL;

	*out_hwirq = intspec[0] * IRQ_INMUX + intspec[1];
	*out_type = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static int mux_irq_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &mux_chip, handle_level_irq);
	irq_set_chip_data(irq, d->host_data);
	irq_set_probe(irq);

	return 0;
}

static const struct irq_domain_ops mux_irq_domain_ops = {
	.xlate	= mux_irq_domain_xlate,
	.map	= mux_irq_domain_map,
};

static int __init mux_of_init(struct device_node *np, struct device_node *parent)
{
	struct irq_mux_data *mux_data, *md;
	u32 nr_irq = 1;
	int i;

	if (WARN_ON(!np))
		return -ENODEV;

	if (of_property_read_u32(np, "Realtek,mux-nr", &nr_irq))
		pr_err("[%s] mux number not specified\n", DEV_NAME);

	mux_data = kcalloc(nr_irq, sizeof(*mux_data), GFP_KERNEL);
	if (!mux_data)
		return -ENOMEM;

	rtk_domain = irq_domain_add_linear(np, nr_irq * IRQ_INMUX,
					   &mux_irq_domain_ops, mux_data);
	if (!rtk_domain) {
		pr_err("[%s] IRQ domain init failed\n", DEV_NAME);
		kfree(mux_data);
		return -ENOMEM;
	}

	for (i = 0, md = mux_data; i < nr_irq; i++, md++) {
		u32 status = 0, enable = 0;

		md->base = of_iomap(np, i);
		if (!md->base)
			pr_warn("[%s] unable to map mux %d registers\n",
				DEV_NAME, i);

		md->irq = irq_of_parse_and_map(np, i);
		if (!md->irq)
			pr_warn("[%s] unable to map mux %d parent IRQ\n",
				DEV_NAME, i);

		of_property_read_u32_index(np, "intr-status", i, &status);
		of_property_read_u32_index(np, "intr-en", i, &enable);

		md->index = i;
		md->irq_offset = i * IRQ_INMUX;
		md->intr_status = status;
		md->intr_en = enable;

		/*
		 * Start fully masked: the bootloader may leave sub-interrupts
		 * enabled (e.g. the polled UART) with no Linux handler, which
		 * would storm the chained parent IRQ. Disable every source and
		 * clear any latched status; consumers re-enable through
		 * mux_unmask_irq() when they request an interrupt.
		 */
		if (md->base) {
			__raw_writel(0x0, md->base + md->intr_en);
			__raw_writel(~0x0, md->base + md->intr_status);
		}

		irq_set_chained_handler_and_data(md->irq, mux_irq_handle, md);
	}

	return 0;
}

IRQCHIP_DECLARE(rtk_irq_mux, "Realtek,rtk-irq-mux", mux_of_init);
