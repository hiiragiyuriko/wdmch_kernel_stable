// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek RTD1295 Serial Flash Controller (SFC) - spi-mem driver.
 *
 * The SFC is not a generic SPI shifter: it drives the boot SPI-NOR through an
 * opcode/control register pair plus a memory-mapped flash window. A transaction
 * is issued by writing the command to OPCODE, the transaction type to CTL, and
 * then touching the window:
 *
 *   CTL = 0x00  command only            (WREN/WRDI/EN4B/EX4B/chip-erase)
 *   CTL = 0x08  command + address       (sector erase; address = window offset)
 *   CTL = 0x10  command + register data (RDID/RDSR/WRSR; data at window base)
 *   CTL = 0x18  command + memory address (read/program; flash mapped at window)
 *
 * Ported from the Realtek 4.9 vendor rtk-sfc.c, which used the old struct
 * spi_nor controller ops (removed from mainline). It is reworked here as a
 * single-lane spi-mem controller; the generic spi-nor core then drives the
 * jedec,spi-nor child.
 *
 * Copyright (C) 2021 Realtek Ltd. (vendor original)
 * Author: PK Chuang <pk.chuang@realtek.com>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/* JEDEC SPI-NOR opcodes the controller dispatches on (kept local to avoid a
 * drivers/spi -> mtd/spi-nor.h dependency).
 */
#define OP_READ			0x03
#define OP_RDSR			0x05
#define OP_EN4B			0xb7
#define OP_EX4B			0xe9
#define OP_CHIP_ERASE		0xc7
#define SR_WIP			BIT(0)

/* SFC register block (DT reg). */
#define SFC_OPCODE		0x00
#define SFC_CTL			0x04
#define SFC_SCK			0x08
#define SFC_CE			0x0c
#define SFC_POS_LATCH		0x14
#define SFC_WAIT_WR		0x18
#define SFC_EN_WR		0x1c
#define SFC_OPCODE2		0x28

/* CTL transaction types. */
#define SFC_CTL_CMD		0x00	/* command only */
#define SFC_CTL_ADDR		0x08	/* command + address */
#define SFC_CTL_REG		0x10	/* command + register data */
#define SFC_CTL_MEM		0x18	/* command + memory address */

/* Fixed SoC addresses (not in the DT reg). */
#define SFC_NOR_WINDOW_PHYS	0x88100000
#define SFC_NOR_WINDOW_SIZE	0x02000000	/* 32 MiB direct-map window */
#define SFC_RBUS_SYNC_PHYS	0x9801a020	/* rbus posted-write flush */
#define SFC_RBUS_ENABLE_PHYS	0x9801a914	/* RTD129x: bit0 enables the SFC */

#define SFC_OP_DELAY_US		50
#define SFC_WIP_POLL_US		100
#define SFC_WIP_TIMEOUT_US	1000000

struct rtk_sfc {
	struct device *dev;
	void __iomem *regs;	/* SFC register block */
	void __iomem *window;	/* memory-mapped flash window */
	void __iomem *sync;	/* rbus posted-write flush register */
	struct mutex lock;
};

/*
 * Flush the OPCODE/CTL writes (on the rbus) before touching the NOR window
 * (on a different bus), matching the vendor SFC_SYNC barrier.
 */
static void rtk_sfc_sync(struct rtk_sfc *sfc)
{
	mb();
	writel(0, sfc->sync);
	mb();
}

/* Arm a transaction: write opcode + control, then flush. */
static void rtk_sfc_arm(struct rtk_sfc *sfc, u8 opcode, u32 ctl)
{
	writel(opcode, sfc->regs + SFC_OPCODE);
	udelay(SFC_OP_DELAY_US);
	writel(ctl, sfc->regs + SFC_CTL);
	rtk_sfc_sync(sfc);
}

static void rtk_sfc_enable_auto_write(struct rtk_sfc *sfc)
{
	writel(0x105, sfc->regs + SFC_WAIT_WR);
	writel(0x106, sfc->regs + SFC_EN_WR);
	writel(0x001a1307, sfc->regs + SFC_CE);
}

static void rtk_sfc_disable_auto_write(struct rtk_sfc *sfc)
{
	writel(0x005, sfc->regs + SFC_WAIT_WR);
	writel(0x006, sfc->regs + SFC_EN_WR);
}

/*
 * The flash window only decodes 32-bit accesses; byte/halfword reads return
 * the first response byte replicated (which is why a naive memcpy_fromio reads
 * "ef ef ef..."). So all data transfers go through aligned readl()/writel().
 */

/* Register response (RDID/RDSR/...): presented at the window base as a word. */
static void rtk_sfc_reg_in(struct rtk_sfc *sfc, u8 *buf, size_t len)
{
	(void)readl(sfc->window);		/* prime/trigger */

	while (len) {
		u32 w = readl(sfc->window);
		size_t n = min_t(size_t, len, 4);
		size_t i;

		for (i = 0; i < n; i++)
			buf[i] = w >> (i * 8);
		buf += n;
		len -= n;
	}
}

/* Linear flash read from the memory-mapped window, handling unaligned ends. */
static void rtk_sfc_mem_in(struct rtk_sfc *sfc, u8 *buf, u32 off, size_t len)
{
	while (len) {
		u32 a = off & ~0x3u;
		u32 sh = (off & 0x3u) * 8;
		u32 w = readl(sfc->window + a);
		size_t n = min_t(size_t, len, 4 - (off & 0x3u));
		size_t i;

		for (i = 0; i < n; i++)
			buf[i] = w >> (sh + i * 8);
		buf += n;
		off += n;
		len -= n;
	}
}

/* Program data into the window; non-written bytes are left as 0xff (no-op). */
static void rtk_sfc_mem_out(struct rtk_sfc *sfc, const u8 *buf, u32 off,
			    size_t len)
{
	while (len) {
		u32 a = off & ~0x3u;
		u32 sh = (off & 0x3u) * 8;
		size_t n = min_t(size_t, len, 4 - (off & 0x3u));
		u32 w = 0xffffffff;
		size_t i;

		for (i = 0; i < n; i++) {
			u32 b = sh + i * 8;

			w = (w & ~(0xffu << b)) | ((u32)buf[i] << b);
		}
		writel(w, sfc->window + a);
		buf += n;
		off += n;
		len -= n;
	}
}

/* Poll the flash WIP (write-in-progress) bit via RDSR until clear. */
static int rtk_sfc_wait_wip(struct rtk_sfc *sfc)
{
	int timeout = SFC_WIP_TIMEOUT_US / SFC_WIP_POLL_US;
	u8 sr;

	while (timeout--) {
		rtk_sfc_arm(sfc, OP_RDSR, SFC_CTL_REG);
		rtk_sfc_reg_in(sfc, &sr, 1);
		if (!(sr & SR_WIP))
			return 0;
		udelay(SFC_WIP_POLL_US);
	}
	dev_err(sfc->dev, "timed out waiting for write to complete\n");
	return -ETIMEDOUT;
}

/* Put the controller back into linear read mode (vendor read_mode). */
static void rtk_sfc_read_mode(struct rtk_sfc *sfc)
{
	rtk_sfc_arm(sfc, OP_READ, SFC_CTL_MEM);
	(void)readl(sfc->window);
}

static int rtk_sfc_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct rtk_sfc *sfc = spi_controller_get_devdata(mem->spi->controller);
	u8 opcode = op->cmd.opcode;
	u64 addr = op->addr.val;
	int ret = 0;

	mutex_lock(&sfc->lock);

	if (op->addr.nbytes == 0 && op->data.dir == SPI_MEM_NO_DATA) {
		/* command only: WREN/WRDI/EN4B/EX4B/chip-erase */
		rtk_sfc_arm(sfc, opcode, SFC_CTL_CMD);
		(void)readb(sfc->window);

		if (opcode == OP_EN4B)
			writel(0x1, sfc->regs + SFC_OPCODE2);
		else if (opcode == OP_EX4B)
			writel(0x0, sfc->regs + SFC_OPCODE2);
		else if (opcode == OP_CHIP_ERASE)
			ret = rtk_sfc_wait_wip(sfc);
	} else if (op->addr.nbytes == 0 && op->data.dir == SPI_MEM_DATA_IN) {
		/* register read: RDID/RDSR */
		rtk_sfc_arm(sfc, opcode, SFC_CTL_REG);
		rtk_sfc_reg_in(sfc, op->data.buf.in, op->data.nbytes);
	} else if (op->addr.nbytes == 0 && op->data.dir == SPI_MEM_DATA_OUT) {
		/* register write: WRSR */
		rtk_sfc_arm(sfc, opcode, SFC_CTL_REG);
		rtk_sfc_mem_out(sfc, op->data.buf.out, 0, op->data.nbytes);
	} else if (op->addr.nbytes && op->data.dir == SPI_MEM_NO_DATA) {
		/* sector erase: address carried via the window offset */
		rtk_sfc_arm(sfc, opcode, SFC_CTL_ADDR);
		(void)readb(sfc->window + addr);
		ret = rtk_sfc_wait_wip(sfc);
	} else if (op->addr.nbytes && op->data.dir == SPI_MEM_DATA_IN) {
		/* memory read (single-lane, opcode 0x03) */
		rtk_sfc_read_mode(sfc);
		rtk_sfc_mem_in(sfc, op->data.buf.in, addr, op->data.nbytes);
	} else if (op->addr.nbytes && op->data.dir == SPI_MEM_DATA_OUT) {
		/* page program */
		rtk_sfc_enable_auto_write(sfc);
		rtk_sfc_arm(sfc, opcode, SFC_CTL_MEM);
		rtk_sfc_mem_out(sfc, op->data.buf.out, addr, op->data.nbytes);
		rtk_sfc_read_mode(sfc);
		rtk_sfc_disable_auto_write(sfc);
		ret = rtk_sfc_wait_wip(sfc);
	} else {
		ret = -EOPNOTSUPP;
	}

	mutex_unlock(&sfc->lock);
	return ret;
}

/*
 * The controller is a plain single-lane 1-1-1 engine with no dummy-cycle
 * support, so reject anything fancier and let the spi-nor core fall back to
 * the basic command set (0x03 read, 0x02 program, ...).
 */
static bool rtk_sfc_supports_op(struct spi_mem *mem,
				const struct spi_mem_op *op)
{
	if (op->cmd.buswidth != 1 || op->addr.buswidth > 1 ||
	    op->dummy.buswidth > 1 || op->data.buswidth > 1)
		return false;
	if (op->dummy.nbytes)
		return false;
	return spi_mem_default_supports_op(mem, op);
}

static const struct spi_controller_mem_ops rtk_sfc_mem_ops = {
	.supports_op = rtk_sfc_supports_op,
	.exec_op = rtk_sfc_exec_op,
};

static void rtk_sfc_hw_init(struct rtk_sfc *sfc)
{
	void __iomem *en;

	/* RTD129x: enable the SFC in the rbus mux register. */
	en = ioremap(SFC_RBUS_ENABLE_PHYS, 0x4);
	if (en) {
		writel(readl(en) | 0x1, en);
		iounmap(en);
	}

	writel(0x00000013, sfc->regs + SFC_SCK);
	writel(0x001a1307, sfc->regs + SFC_CE);
	writel(0x00000000, sfc->regs + SFC_POS_LATCH);
	writel(0x00000005, sfc->regs + SFC_WAIT_WR);
	writel(0x00000006, sfc->regs + SFC_EN_WR);
}

static int rtk_sfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctlr;
	struct rtk_sfc *sfc;
	int ret;

	ctlr = devm_spi_alloc_host(dev, sizeof(*sfc));
	if (!ctlr)
		return -ENOMEM;

	sfc = spi_controller_get_devdata(ctlr);
	sfc->dev = dev;
	mutex_init(&sfc->lock);

	sfc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sfc->regs))
		return PTR_ERR(sfc->regs);

	sfc->window = devm_ioremap(dev, SFC_NOR_WINDOW_PHYS,
				   SFC_NOR_WINDOW_SIZE);
	if (!sfc->window)
		return -ENOMEM;

	sfc->sync = devm_ioremap(dev, SFC_RBUS_SYNC_PHYS, 0x4);
	if (!sfc->sync)
		return -ENOMEM;

	rtk_sfc_hw_init(sfc);

	ctlr->dev.of_node = dev->of_node;
	/* single-lane controller: no dual/quad, no CPOL/CPHA selection */
	ctlr->mode_bits = 0;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->mem_ops = &rtk_sfc_mem_ops;
	ctlr->num_chipselect = 1;

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register controller\n");

	dev_info(dev, "Realtek RTD1295 SFC registered\n");
	return 0;
}

static const struct of_device_id rtk_sfc_of_match[] = {
	{ .compatible = "realtek,rtd1295-sfc" },
	{ }
};
MODULE_DEVICE_TABLE(of, rtk_sfc_of_match);

static struct platform_driver rtk_sfc_driver = {
	.driver = {
		.name = "rtk-sfc",
		.of_match_table = rtk_sfc_of_match,
	},
	.probe = rtk_sfc_probe,
};
module_platform_driver(rtk_sfc_driver);

MODULE_DESCRIPTION("Realtek RTD1295 Serial Flash Controller driver");
MODULE_LICENSE("GPL");
