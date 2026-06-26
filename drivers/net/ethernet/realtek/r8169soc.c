// SPDX-License-Identifier: GPL-2.0
/*
 * r8169soc.c - Realtek RTD1295 on-SoC Gigabit Ethernet (GMAC) driver
 *
 * The RTD1295 GMAC is the classic RTL8169/RTL8168 MAC IP, but memory-mapped
 * on the SoC bus instead of PCI, and wired to an on-chip ("embedded") gigabit
 * PHY.  This driver supports ONLY that configuration (DT "output-mode" 0):
 * the embedded PHY reached through the MAC's internal paged MDIO.  External
 * RGMII/SGMII PHYs, force-Giga, fibre, and the other RTD13xx/139x/16xx
 * variants handled by the Realtek vendor driver are deliberately omitted.
 *
 * This is a clean rewrite for mainline Linux 6.18.  The data path (descriptor
 * model, NAPI, ethtool) follows the in-tree r8169_main.c style; the SoC
 * bring-up sequences (PLL/clock/reset via the ISO syscon, embedded-PHY MDIO
 * access, PHY calibration and the MAC MCU patch) are ported faithfully from
 * the vendor r8169soc.c CONFIG_ARCH_RTD129x paths -- the exact register
 * writes, values and delays there are hardware magic and are preserved.
 *
 * Layout:
 *   1. Register / descriptor definitions (standard RTL8169 model)
 *   2. Private struct and low-level MMIO helpers
 *   3. OCP and embedded-PHY paged-MDIO access (ported)
 *   4. phylib glue (mii_bus over the internal MDIO, adjust_link)
 *   5. SoC bring-up: PLL/clock/reset, PHY power-up + calibration (ported)
 *   6. MAC init / rtl_hw_start (ported, mac_version 42 / RTL8168g family)
 *   7. TX/RX rings, NAPI, interrupt, start_xmit
 *   8. net_device ops, ethtool, open/stop
 *   9. platform_driver probe/remove
 *
 * Copyright (c) 2002 ShuChen <shuchen@realtek.com.tw>
 * Copyright (c) 2003-2007 Francois Romieu <romieu@fr.zoreil.com>
 * Copyright (c) 2015-2019 Realtek Semiconductor Corp.
 * Clean rewrite for RTD1295 / Linux 6.18.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/timer.h>

#include <soc/realtek/rtk_chip.h>

#define DRV_NAME	"r8169soc"

/* ------------------------------------------------------------------------- *
 * 1. Register / descriptor definitions (standard RTL8169 model)
 * ------------------------------------------------------------------------- */

/* MAC register block at DT reg[0] (0x98016000) */
enum rtl_registers {
	MAC0			= 0x00,	/* Ethernet hardware address */
	MAC4			= 0x04,
	MAR0			= 0x08,	/* Multicast filter (hash) */
	LEDSEL			= 0x18,
	TxDescStartAddrLow	= 0x20,
	TxDescStartAddrHigh	= 0x24,
	ChipCmd			= 0x37,
	TxPoll			= 0x38,
	IntrMask		= 0x3c,
	IntrStatus		= 0x3e,
	TxConfig		= 0x40,
#define TXCFG_AUTO_FIFO		BIT(7)
#define TXCFG_EMPTY		BIT(11)
	RxConfig		= 0x44,
#define RX_FIFO_THRESH		(7 << 13)
#define RX_EARLY_OFF		BIT(11)
#define RX_DMA_BURST		(7 << 8)	/* unlimited burst */
	Cfg9346			= 0x50,
	Config2			= 0x53,
	Config5			= 0x56,
	PHYAR			= 0x60,	/* internal PHY (MDIO) access */
	PHYstatus		= 0x6c,
	RxMaxSize		= 0xda,
	CPlusCmd		= 0xe0,
	IntrMitigate		= 0xe2,
	RxDescAddrLow		= 0xe4,
	RxDescAddrHigh		= 0xe8,
	MaxTxPacketSize		= 0xec,
	MISC			= 0xf0,
#define RXDV_GATED_EN		BIT(19)
};

/* RTL8168 extended registers used by the bring-up paths */
enum rtl8168_registers {
	EEE_LED			= 0x1b,
	ERIDR			= 0x70,
	ERIAR			= 0x74,
#define ERIAR_FLAG		0x80000000
#define ERIAR_WRITE_CMD		0x80000000
#define ERIAR_READ_CMD		0x00000000
#define ERIAR_ADDR_BYTE_ALIGN	4
#define ERIAR_TYPE_SHIFT	16
#define ERIAR_EXGMAC		(0x00 << ERIAR_TYPE_SHIFT)
#define ERIAR_MASK_SHIFT	12
#define ERIAR_MASK_0001		(0x1 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0011		(0x3 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0101		(0x5 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_1111		(0xf << ERIAR_MASK_SHIFT)
	CSIDR			= 0x64,
	CSIAR			= 0x68,
#define CSIAR_FLAG		0x80000000
#define CSIAR_WRITE_CMD		0x80000000
#define CSIAR_BYTE_ENABLE	0x0f
#define CSIAR_BYTE_ENABLE_SHIFT	12
#define CSIAR_ADDR_MASK		0x0fff
	MCU			= 0xd3,
#define DIS_MCU_CLROOB		BIT(0)
	OCPDR			= 0xb0,	/* OCP GPHY/MAC-MCU access */
#define OCPDR_WRITE_CMD		0x80000000
#define OCPDR_READ_CMD		0x00000000
#define OCPDR_REG_MASK		0x7fff
#define OCPDR_REG_SHIFT		16
#define OCPDR_DATA_MASK		0xffff
};

/* Interrupt status / mask bits */
enum rtl_irq_bits {
	SYSErr		= 0x8000,
	LinkChg		= 0x0020,
	RxOverflow	= 0x0010,
	TxErr		= 0x0008,
	TxOK		= 0x0004,
	RxErr		= 0x0002,
	RxOK		= 0x0001,
};

#define RTL_EVENT_NAPI	(RxOK | RxErr | TxOK | TxErr)
#define RTL_INTR_MASK	(RTL_EVENT_NAPI | LinkChg | RxOverflow | SYSErr)

enum rtl_register_content {
	/* ChipCmd */
	StopReq		= 0x80,
	CmdReset	= 0x10,
	CmdRxEnb	= 0x08,
	CmdTxEnb	= 0x04,

	/* TxPoll */
	NPQ		= 0x40,

	/* Cfg9346 */
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xc0,

	/* rx_mode bits */
	AcceptErr	= 0x20,
	AcceptRunt	= 0x10,
	AcceptBroadcast	= 0x08,
	AcceptMulticast	= 0x04,
	AcceptMyPhys	= 0x02,
	AcceptAllPhys	= 0x01,
#define RX_CONFIG_ACCEPT_MASK	0x3f

	/* TxConfig */
	TxInterFrameGapShift = 24,
	TxDMAShift	= 8,

	/* Config2 */
	ClkReqEn	= BIT(7),
	/* Config5 */
	ASPM_en		= BIT(0),

	/* RxStatusDesc */
	RxRWT		= BIT(22),
	RxRES		= BIT(21),
	RxRUNT		= BIT(20),
	RxCRC		= BIT(19),

	/* CPlusCmd */
	PktCntrDisable	= BIT(7),
	INTT_1		= 0x0001,

	/* PHYstatus */
	LinkStatus	= 0x02,
	FullDup		= 0x01,
	_10bps		= 0x04,
	_100bps		= 0x08,
	_1000bpsF	= 0x10,
};

/* Descriptor common bits (first dword) */
enum rtl_desc_bit {
	DescOwn		= BIT(31),	/* owned by NIC */
	RingEnd		= BIT(30),	/* last descriptor in ring */
	FirstFrag	= BIT(29),
	LastFrag	= BIT(28),
};

/* RX status / protocol bits */
enum rtl_rx_desc_bit {
	PID1		= BIT(18),
	PID0		= BIT(17),
#define RxProtoUDP	PID1
#define RxProtoTCP	PID0
#define RxProtoMask	(PID1 | PID0)
	IPFail		= BIT(16),
	UDPFail		= BIT(15),
	TCPFail		= BIT(14),
#define RxCSFailMask	(IPFail | UDPFail | TCPFail)
};

struct TxDesc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct RxDesc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct ring_info {
	struct sk_buff	*skb;
	u32		len;
};

/* ISO syscon block at DT reg[1] (0x98007000) - PHY/PLL/clock control */
enum iso_registers {
	ISO_UMSK_ISR		= 0x0004,
	ISO_ETN_TESTIO		= 0x0060,
	ISO_PWRCUT_ETN		= 0x005c,
	ISO_ETN_DBUS_CTRL	= 0x0fc0,
};

/* Ring sizes (single TX/RX queue) */
#define NUM_TX_DESC		256
#define NUM_RX_DESC		256
#define R8169_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct TxDesc))
#define R8169_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct RxDesc))

#define R8169_RX_BUF_SIZE	(SZ_16K - 1)	/* 0x3fff: descriptor length max */
#define R8169_NAPI_WEIGHT	64
#define RTL8169_TX_TIMEOUT	(6 * HZ)

/* TX flow-control thresholds (descriptors) */
#define R8169_TX_STOP_THRS	(MAX_SKB_FRAGS + 1)
#define R8169_TX_START_THRS	(2 * R8169_TX_STOP_THRS)

/* mac_version we emulate: RTL8168g family == vendor RTL_GIGA_MAC_VER_42 */
#define RTL_MAC_VER_42		42

/* Embedded PHY address on the internal MDIO bus.  The vendor enum
 * phy_addr_e defines INT_PHY_ADDR = 1 for the embedded GPHY, and on
 * RTD129x the address is selected via ISO_PWRCUT_ETN[20:16]; for the
 * MII bus we expose the embedded PHY at this address.
 */
#define INT_PHY_ADDR		1

/* Sentinel for "do not switch MDIO page". */
#define CURRENT_MDIO_PAGE	0xffffffff

/* ------------------------------------------------------------------------- *
 * 2. Private struct and low-level MMIO helpers
 * ------------------------------------------------------------------------- */

struct rtl8169_private {
	struct platform_device	*pdev;
	struct net_device	*dev;
	struct napi_struct	napi;
	struct device		*phydev_dev;

	void __iomem		*mmio_addr;	/* MAC registers (reg[0]) */
	void __iomem		*mmio_clkaddr;	/* ISO syscon (reg[1]) */

	struct clk		*clk_etn_sys;
	struct clk		*clk_etn_250m;
	struct reset_control	*rstc_gmac;
	struct reset_control	*rstc_gphy;

	struct mii_bus		*mii_bus;
	struct phy_device	*phydev;

	u16			mac_version;
	u16			cp_cmd;
	u32			ocp_base;	/* MAC-MCU paged base (mac_mcu) */

	/* TX/RX rings */
	struct TxDesc		*TxDescArray;	/* 256-aligned TX ring */
	struct RxDesc		*RxDescArray;	/* 256-aligned RX ring */
	dma_addr_t		TxPhyAddr;
	dma_addr_t		RxPhyAddr;
	struct page		*Rx_databuff[NUM_RX_DESC];
	struct ring_info	tx_skb[NUM_TX_DESC];

	u32			cur_rx;
	u32			cur_tx;
	u32			dirty_tx;

	u32			irq_mask;
	int			irq;
	int			cur_speed;
	int			cur_duplex;
};

static inline struct device *tp_to_dev(struct rtl8169_private *tp)
{
	return &tp->pdev->dev;
}

/* MMIO accessors against the MAC register block. */
#define RTL_W8(tp, reg, val8)	writeb((val8), (tp)->mmio_addr + (reg))
#define RTL_W16(tp, reg, val16)	writew((val16), (tp)->mmio_addr + (reg))
#define RTL_W32(tp, reg, val32)	writel((val32), (tp)->mmio_addr + (reg))
#define RTL_R8(tp, reg)		readb((tp)->mmio_addr + (reg))
#define RTL_R16(tp, reg)	readw((tp)->mmio_addr + (reg))
#define RTL_R32(tp, reg)	readl((tp)->mmio_addr + (reg))

/* Poll a 32-bit MAC register until (value & mask) matches @cond.
 * Returns true on success, false on timeout.  Mirrors the vendor
 * rtl_udelay_loop_wait helpers (d us per step, n steps).
 */
static bool rtl_loop_wait(struct rtl8169_private *tp, u32 reg, u32 mask,
			  bool cond, unsigned int d, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!!(RTL_R32(tp, reg) & mask) == cond)
			return true;
		udelay(d);
	}
	return false;
}

/* Same as rtl_loop_wait() but for byte-wide registers (e.g. ChipCmd at
 * offset 0x37). A 32-bit read of an unaligned byte register faults on the
 * arm64 device mapping, so these must use an 8-bit access.
 */
static bool rtl_loop_wait_8(struct rtl8169_private *tp, u32 reg, u8 mask,
			    bool cond, unsigned int d, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!!(RTL_R8(tp, reg) & mask) == cond)
			return true;
		udelay(d);
	}
	return false;
}

/* ------------------------------------------------------------------------- *
 * 3. OCP and embedded-PHY paged-MDIO access (ported from vendor RTD129x)
 * ------------------------------------------------------------------------- */

/* OCP is the GMAC's on-chip register window used to reach the MAC MCU and
 * a handful of ETN/PHY-control registers.  reg must be 16-bit aligned and
 * fit OCPDR_REG_MASK; see vendor rtl_ocp_read/write.
 */
static void rtl_ocp_write(struct rtl8169_private *tp, u32 reg, u32 value)
{
	if (WARN_ON_ONCE(reg & 0xffff0001))
		return;

	RTL_W32(tp, OCPDR, OCPDR_WRITE_CMD |
		(((reg >> 1) & OCPDR_REG_MASK) << OCPDR_REG_SHIFT) |
		(value & OCPDR_DATA_MASK));
}

static u32 rtl_ocp_read(struct rtl8169_private *tp, u32 reg)
{
	if (WARN_ON_ONCE(reg & 0xffff0001))
		return 0;

	RTL_W32(tp, OCPDR, OCPDR_READ_CMD |
		(((reg >> 1) & OCPDR_REG_MASK) << OCPDR_REG_SHIFT));
	return RTL_R32(tp, OCPDR) & OCPDR_DATA_MASK;
}

/* ERI (extended register interface, EXGMAC) - used by rtl_hw_start. */
static void rtl_eri_write(struct rtl8169_private *tp, int addr, u32 mask,
			  u32 val, int type)
{
	RTL_W32(tp, ERIDR, val);
	RTL_W32(tp, ERIAR, ERIAR_WRITE_CMD | type | mask | addr);
	rtl_loop_wait(tp, ERIAR, ERIAR_FLAG, false, 100, 100);
}

static u32 rtl_eri_read(struct rtl8169_private *tp, int addr, int type)
{
	RTL_W32(tp, ERIAR, ERIAR_READ_CMD | type | ERIAR_MASK_1111 | addr);
	return rtl_loop_wait(tp, ERIAR, ERIAR_FLAG, true, 100, 100) ?
		RTL_R32(tp, ERIDR) : ~0;
}

static void rtl_w0w1_eri(struct rtl8169_private *tp, int addr, u32 mask,
			 u32 p, u32 m, int type)
{
	u32 val = rtl_eri_read(tp, addr, type);

	rtl_eri_write(tp, addr, mask, (val & ~m) | p, type);
}

/* CSI (configuration space interface) - used by rtl_hw_start. */
static void rtl_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	RTL_W32(tp, CSIDR, value);
	RTL_W32(tp, CSIAR, CSIAR_WRITE_CMD | (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);
	rtl_loop_wait(tp, CSIAR, CSIAR_FLAG, false, 10, 100);
}

static u32 rtl_csi_read(struct rtl8169_private *tp, int addr)
{
	RTL_W32(tp, CSIAR, (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);
	return rtl_loop_wait(tp, CSIAR, CSIAR_FLAG, true, 10, 100) ?
		RTL_R32(tp, CSIDR) : ~0;
}

/*
 * Embedded-PHY MDIO access.
 *
 * On RTD129x the embedded gigabit PHY is NOT a normal MMIO MDIO bus: it is
 * reached through the MAC's paged register file via the PHYAR register, with
 * page selection done by writing register 0x1f.  The vendor functions
 * __int_mdio_read/write + int_mdio_read/write are ported here verbatim
 * (PHYAR command format and the mandatory 20us post-access delay are HW
 * requirements).
 */

/* Disable / re-enable PHY->MCU interrupt around an MDIO transaction
 * (vendor MDIO_LOCK/UNLOCK for CONFIG_ARCH_RTD129x).
 */
static void rtl_mdio_lock(struct rtl8169_private *tp)
{
	rtl_ocp_write(tp, 0xfc1e,
		      rtl_ocp_read(tp, 0xfc1e) & ~(BIT(1) | BIT(11) | BIT(12)));
}

static void rtl_mdio_unlock(struct rtl8169_private *tp)
{
	rtl_ocp_write(tp, 0xfc1e,
		      rtl_ocp_read(tp, 0xfc1e) | (BIT(1) | BIT(11) | BIT(12)));
}

static int __int_mdio_read(struct rtl8169_private *tp, int reg)
{
	int value;

	RTL_W32(tp, PHYAR, 0x0 | (reg & 0x1f) << 16);

	/* PHYAR bit31 clears when the read result is ready. */
	value = rtl_loop_wait(tp, PHYAR, 0x80000000, true, 25, 20) ?
		RTL_R32(tp, PHYAR) & 0xffff : ~0;

	/* HW spec: 20us settle time before the next command. */
	usleep_range(20, 21);
	return value;
}

static void __int_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	RTL_W32(tp, PHYAR, 0x80000000 | (reg & 0x1f) << 16 | (value & 0xffff));

	rtl_loop_wait(tp, PHYAR, 0x80000000, false, 25, 20);
	usleep_range(20, 21);
}

/* Paged access: select @page via reg 0x1f, then read/write @reg. */
static int int_mdio_read(struct rtl8169_private *tp, int page, int reg)
{
	int value;

	rtl_mdio_lock(tp);
	if (page != CURRENT_MDIO_PAGE)
		__int_mdio_write(tp, 0x1f, page);
	value = __int_mdio_read(tp, reg);
	rtl_mdio_unlock(tp);

	return value;
}

static void int_mdio_write(struct rtl8169_private *tp, int page, int reg,
			   int value)
{
	rtl_mdio_lock(tp);
	if (page != CURRENT_MDIO_PAGE)
		__int_mdio_write(tp, 0x1f, page);
	__int_mdio_write(tp, reg, value);
	rtl_mdio_unlock(tp);
}

/* Page-0 register helpers used by the bring-up sequences. */
static int rtl_phy_read(struct rtl8169_private *tp, int page, int reg)
{
	return int_mdio_read(tp, page, reg);
}

static void rtl_phy_write(struct rtl8169_private *tp, int page, int reg, u32 v)
{
	int_mdio_write(tp, page, reg, v);
}

/* ------------------------------------------------------------------------- *
 * 4. phylib glue: mii_bus over the internal paged MDIO
 * ------------------------------------------------------------------------- */

/*
 * phylib only knows about flat (page-less) MDIO.  The Realtek GPHY keeps the
 * standard IEEE registers (0..0x1f) on page 0, so we present page 0 to
 * phylib.  Calibration/init that needs other pages uses int_mdio_*()
 * directly (see SoC bring-up).
 */
static int r8169soc_mdio_bus_read(struct mii_bus *bus, int phyaddr, int reg)
{
	struct rtl8169_private *tp = bus->priv;

	if (phyaddr != INT_PHY_ADDR)
		return -ENODEV;

	return int_mdio_read(tp, 0, reg) & 0xffff;
}

static int r8169soc_mdio_bus_write(struct mii_bus *bus, int phyaddr, int reg,
				   u16 val)
{
	struct rtl8169_private *tp = bus->priv;

	if (phyaddr != INT_PHY_ADDR)
		return -ENODEV;

	int_mdio_write(tp, 0, reg, val);
	return 0;
}

/* Program the MAC for the speed/duplex phylib negotiated. */
static void rtl8169_adjust_link(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct phy_device *phydev = tp->phydev;

	if (!phydev->link) {
		if (tp->cur_speed != -1) {
			tp->cur_speed = -1;
			netif_carrier_off(dev);
		}
		return;
	}

	if (phydev->speed == tp->cur_speed &&
	    phydev->duplex == tp->cur_duplex)
		return;

	tp->cur_speed = phydev->speed;
	tp->cur_duplex = phydev->duplex;

	/* The RTL8169 MAC follows the PHY's resolved speed/duplex
	 * automatically (it samples PHYstatus); nothing extra to poke for
	 * the embedded-PHY path.  Keep carrier in sync and report.
	 */
	netif_carrier_on(dev);
	phy_print_status(phydev);
}

/* ------------------------------------------------------------------------- *
 * 5. SoC bring-up: PLL / clock / reset and PHY power-up + calibration
 *
 * Ported from vendor r8169soc.c CONFIG_ARCH_RTD129x: the clock/reset/PHY
 * power-up that lives inline in the vendor rtl_init_one() (the "clk not
 * already enabled" branch), plus rtl8168g_2_hw_mac_mcu_patch() (EEE MCU
 * patch + data-path select) and rtl_phy_reinit() (PHY ready wait).
 * ------------------------------------------------------------------------- */

/*
 * Power up the embedded PHY and the GMAC through the ISO syscon and the
 * clk/reset framework.  This reproduces the numbered vendor sequence:
 *   1. deassert gphy reset
 *   2. wait 200us
 *   3. ISO 0x60[1]=0   - disable "boot bypass gphy ready" mode
 *   4. ISO 0xfc0[0]=0  - disable dbus clock gating
 *   5. wait 200us
 *   6-7. pulse etn clocks on then off
 *   8. deassert gmac reset
 *   9. enable etn clocks, wait 100ms for the GMAC uC to settle
 */
static void r8169soc_pll_clock_init(struct rtl8169_private *tp)
{
	void __iomem *iso = tp->mmio_clkaddr;
	u32 tmp;

	/* ISO spec: clear ETN PHY interrupt latch (used as MDIO-ready flag). */
	writel(BIT(27), iso + ISO_UMSK_ISR);

	/* 1. reg_0x98007088[10] = 1: reset bit of gphy */
	reset_control_deassert(tp->rstc_gphy);

	/* 2. wait 200us for the PHY PLL */
	usleep_range(200, 210);

	/* 3. reg_0x98007060[1] = 0: Ethernet boot-up, bypass gphy-ready mode */
	tmp = readl(iso + ISO_ETN_TESTIO);
	tmp &= ~BIT(1);
	writel(tmp, iso + ISO_ETN_TESTIO);

	/* 4. reg_0x98007fc0[0] = 0: disable dbus clock gating */
	tmp = readl(iso + ISO_ETN_DBUS_CTRL);
	tmp &= ~BIT(0);
	writel(tmp, iso + ISO_ETN_DBUS_CTRL);

	/* 5. wait 200us */
	usleep_range(200, 210);

	/* 6. enable etn clock & etn 250MHz */
	clk_prepare_enable(tp->clk_etn_sys);
	clk_prepare_enable(tp->clk_etn_250m);

	/* 7. disable them again (vendor toggles before gmac reset) */
	clk_disable_unprepare(tp->clk_etn_sys);
	clk_disable_unprepare(tp->clk_etn_250m);

	/* 8. reg_0x98007088[9] = 1: reset bit of gmac */
	reset_control_deassert(tp->rstc_gmac);

	/* 9. re-enable etn clocks for good */
	clk_prepare_enable(tp->clk_etn_sys);
	clk_prepare_enable(tp->clk_etn_250m);

	msleep(100);	/* wait for the GMAC uC to be stable */
}

/*
 * Embedded FE/GE PHY electrical (IOL) tuning, revision specific.
 * Ported verbatim from vendor r8169soc_phy_iol_tuning() RTD129x cuts.
 * These paged-register writes adjust idac/abiq/ldvbias/vcm to meet IOL.
 */
static void r8169soc_phy_iol_tuning(struct rtl8169_private *tp)
{
	switch (get_rtd_chip_revision()) {
	case RTD_CHIP_A00:	/* TSMC, cut A */
	case RTD_CHIP_A01:	/* TSMC, cut B */
		int_mdio_write(tp, 0x0bc0, 23, 0x0088);	/* idacfine */
		int_mdio_write(tp, 0x0bc0, 21, 0x0004);	/* abiq */
		int_mdio_write(tp, 0x0bc0, 22, 0x0777);	/* ldvbias */
		int_mdio_write(tp, 0x0bd0, 16, 0x0300);	/* iatt */
		int_mdio_write(tp, 0x0bd0, 17, 0xe8ca);	/* vcm_ref, cf_l */
		break;
	case RTD_CHIP_A02:	/* UMC, cut C */
		int_mdio_write(tp, 0x0bc0, 23, 0x0044);	/* 100M swing */
		int_mdio_write(tp, 0x0bc0, 21, 0x0046);	/* 100M Tr/Tf */
		int_mdio_write(tp, 0x0bc0, 22, 0x0744);	/* 10M */
		int_mdio_write(tp, 0x0bd0, 17, 0x18ca);	/* vcmref, cf_l */
		int_mdio_write(tp, 0x0bd0, 16, 0x0200);	/* iatt */
		break;
	default:
		/* Newer cuts (B00+) need no IOL override. */
		break;
	}
}

/*
 * Bring the embedded PHY MDIO online and select the embedded-PHY data path.
 * Ported from vendor r8169soc_mdio_init() + rtl_phy_reinit() (RTD129x,
 * OUTPUT_EMBEDDED_PHY).  Must run after r8169soc_pll_clock_init().
 */
static void r8169soc_mdio_init(struct rtl8169_private *tp)
{
	void __iomem *iso = tp->mmio_clkaddr;
	u32 tmp;
	int i;

	/* Optionally wait for the ETN PHY interrupt latch (ISO_UMSK_ISR bit27),
	 * which the PHY may raise once its MDIO interface is ready. On this
	 * hardware the latch frequently never asserts even though MDIO works
	 * fine immediately afterwards -- the vendor driver hits the same
	 * timeout and simply carries on -- so treat a timeout as benign and
	 * only log it at debug level.
	 */
	for (i = 0; i < 100; i++) {
		if (readl(iso + ISO_UMSK_ISR) & BIT(27))
			break;
		mdelay(1);
	}
	if (i >= 100)
		netdev_dbg(tp->dev, "PHY MDIO-ready latch not set, continuing\n");

	/* Electrical tuning before bringing the PHY fully up. */
	r8169soc_phy_iol_tuning(tp);

	/* fill fuse_rdy & rg_ext_ini_done (PHY page 0x0a46 reg 20). */
	rtl_phy_write(tp, 0x0a46, 20,
		      rtl_phy_read(tp, 0x0a46, 20) | (BIT(1) | BIT(0)));

	/* init_autoload_done = 1 (OCP 0xe004 bit7). */
	tmp = rtl_ocp_read(tp, 0xe004);
	tmp |= BIT(7);
	rtl_ocp_write(tp, 0xe004, tmp);

	/* ee_mode = 3 (unlock config registers). */
	RTL_W8(tp, Cfg9346, Cfg9346_Unlock);

	/* Wait LAN-ON: PHY status (page 0x0a42 reg 16, bits[2:0]) == 0x3. */
	for (i = 0; i < 2000; i++) {
		if ((rtl_phy_read(tp, 0x0a42, 16) & 0x07) == 0x3)
			break;
		mdelay(1);
	}
	if (i >= 2000)
		netdev_warn(tp->dev, "PHY not LAN-ON, status=0x%02x\n",
			    rtl_phy_read(tp, 0x0a42, 16) & 0x07);

	/* Embedded-PHY data path selection (vendor OUTPUT_EMBEDDED_PHY):
	 *   ISO 0x705c[7]=0, [5]=0 : internal MDIO accesses the embedded PHY
	 *   ISO 0x705c[4]=0        : data path to the embedded PHY
	 */
	tmp = readl(iso + ISO_PWRCUT_ETN);
	tmp &= ~(BIT(7) | BIT(5) | BIT(4));
	writel(tmp, iso + ISO_PWRCUT_ETN);

	/* ETN spec: GMAC data path select MII-like (embedded GPHY), OCP
	 * 0xea34 bits[1:0] = 0b10.
	 */
	tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
	tmp |= BIT(1);
	rtl_ocp_write(tp, 0xea34, tmp);
}

/*
 * MAC MCU (firmware) patch enabling EEE/EEE+ for the RTD129x embedded PHY.
 * Ported verbatim from vendor rtl8168g_2_hw_mac_mcu_patch() (RTD129x);
 * the opcode stream is loaded into the MAC MCU code RAM and a breakpoint
 * is armed.  Applied for cuts A00/A01 and B00+.
 */
static void r8169soc_mac_mcu_patch(struct rtl8169_private *tp)
{
	static const u16 patch[] = {
		0xE008, 0xE012, 0xE044, 0xE046, 0xE048, 0xE04A, 0xE04C, 0xE04E,
		0x44E3, 0xC708, 0x75E0, 0x485D, 0x9DE0, 0xC705, 0xC502, 0xBD00,
		0x01EE, 0xE85A, 0xE000, 0xC72D, 0x76E0, 0x49ED, 0xF026, 0xC02A,
		0x7400, 0xC526, 0xC228, 0x9AA0, 0x73A2, 0x49BE, 0xF11E, 0xC324,
		0x9BA2, 0x73A2, 0x49BE, 0xF0FE, 0x73A2, 0x49BE, 0xF1FE, 0x1A02,
		0x49C9, 0xF003, 0x4821, 0xE002, 0x48A1, 0x73A2, 0x49BE, 0xF10D,
		0xC313, 0x9AA0, 0xC312, 0x9BA2, 0x73A2, 0x49BE, 0xF0FE, 0x73A2,
		0x49BE, 0xF1FE, 0x48ED, 0x9EE0, 0xC602, 0xBE00, 0x0532, 0xDE00,
		0xE85A, 0xE086, 0x0A44, 0x801F, 0x8015, 0x0015, 0xC602, 0xBE00,
		0x0000, 0xC602, 0xBE00, 0x0000, 0xC602, 0xBE00, 0x0000, 0xC602,
		0xBE00, 0x0000, 0xC602, 0xBE00, 0x0000, 0xC602, 0xBE00, 0x0000,
	};
	unsigned int rev = get_rtd_chip_revision();
	int i;

	if (!(rev == RTD_CHIP_A00 || rev == RTD_CHIP_A01 ||
	      rev >= RTD_CHIP_B00))
		return;

	/* Power down PHY while patching the MAC MCU. */
	rtl_phy_write(tp, 0, MII_BMCR,
		      rtl_phy_read(tp, 0, MII_BMCR) | BMCR_PDOWN);

	/* Disable break points (OCP 0xfc28..0xfc36) and base address. */
	for (i = 0xfc28; i <= 0xfc36; i += 2)
		rtl_ocp_write(tp, i, 0);
	mdelay(3);
	rtl_ocp_write(tp, 0xfc26, 0);

	/* Load patch code into the MCU code RAM starting at OCP 0xf800. */
	for (i = 0; i < ARRAY_SIZE(patch); i++)
		rtl_ocp_write(tp, 0xf800 + i * 2, patch[i]);

	/* Enable base address and arm the breakpoint. */
	rtl_ocp_write(tp, 0xfc26, 0x8000);
	rtl_ocp_write(tp, 0xfc28, 0x01ED);
	rtl_ocp_write(tp, 0xfc2a, 0x0531);

	/* Power the PHY back up so phylib can drive it. */
	rtl_phy_write(tp, 0, MII_BMCR,
		      rtl_phy_read(tp, 0, MII_BMCR) & ~BMCR_PDOWN);
}

/*
 * PHY config knobs ported from vendor rtl8168g_2_hw_phy_config() (RTD129x):
 * keep dis_mcu_clroob set, enable ALDPS and 10M EEE.
 */
static void r8169soc_phy_config(struct rtl8169_private *tp)
{
	/* Avoid WOL fail when ALDPS is enabled. */
	RTL_W8(tp, MCU, RTL_R8(tp, MCU) | DIS_MCU_CLROOB);

	/* Enable ALDPS mode (page 0x0a43 reg 0x18): set bit2, clear 12/1/0. */
	rtl_phy_write(tp, 0x0a43, 0x18,
		      (rtl_phy_read(tp, 0x0a43, 0x18) | BIT(2)) &
		      ~(BIT(12) | BIT(1) | BIT(0)));

	/* Enable EEE for 10Mbps (page 0x0a43 reg 0x19 bit4). */
	rtl_phy_write(tp, 0x0a43, 0x19,
		      rtl_phy_read(tp, 0x0a43, 0x19) | BIT(4));

	/* Enable ALDPS (page 0x0a43 reg 24 bit2), as in vendor probe tail. */
	rtl_phy_write(tp, 0x0a43, 24,
		      rtl_phy_read(tp, 0x0a43, 24) | BIT(2));
}

/* ------------------------------------------------------------------------- *
 * 6. MAC init / rtl_hw_start (ported, mac_version 42 / RTL8168g family)
 * ------------------------------------------------------------------------- */

static void rtl_hw_reset(struct rtl8169_private *tp)
{
	RTL_W8(tp, ChipCmd, CmdReset);
	rtl_loop_wait_8(tp, ChipCmd, CmdReset, false, 100, 100);
}

static void rtl_rx_close(struct rtl8169_private *tp)
{
	RTL_W32(tp, RxConfig, RTL_R32(tp, RxConfig) & ~RX_CONFIG_ACCEPT_MASK);
}

static void rtl8169_irq_mask_and_ack(struct rtl8169_private *tp)
{
	RTL_W16(tp, IntrMask, 0);
	RTL_W16(tp, IntrStatus, 0xffff);
}

static void rtl_irq_enable(struct rtl8169_private *tp)
{
	RTL_W16(tp, IntrMask, tp->irq_mask);
}

static void rtl_irq_disable(struct rtl8169_private *tp)
{
	RTL_W16(tp, IntrMask, 0);
}

static u16 rtl_get_events(struct rtl8169_private *tp)
{
	return RTL_R16(tp, IntrStatus);
}

static void rtl_ack_events(struct rtl8169_private *tp, u16 bits)
{
	RTL_W16(tp, IntrStatus, bits);
}

static void rtl8169_hw_reset(struct rtl8169_private *tp)
{
	rtl8169_irq_mask_and_ack(tp);
	rtl_rx_close(tp);

	RTL_W8(tp, ChipCmd, RTL_R8(tp, ChipCmd) | StopReq);
	rtl_loop_wait(tp, TxConfig, TXCFG_EMPTY, true, 100, 666);

	rtl_hw_reset(tp);
}

static void rtl_init_rxcfg(struct rtl8169_private *tp)
{
	/* Disable RX128_INT_EN to reduce CPU load (vendor rtl_init_rxcfg). */
	RTL_W32(tp, RxConfig, RX_DMA_BURST | RX_EARLY_OFF);
}

static void rtl_set_rx_tx_desc_registers(struct rtl8169_private *tp)
{
	/* High word must be written before the low word on some buses. */
	RTL_W32(tp, TxDescStartAddrHigh, (u64)tp->TxPhyAddr >> 32);
	RTL_W32(tp, TxDescStartAddrLow, (u64)tp->TxPhyAddr & DMA_BIT_MASK(32));
	RTL_W32(tp, RxDescAddrHigh, (u64)tp->RxPhyAddr >> 32);
	RTL_W32(tp, RxDescAddrLow, (u64)tp->RxPhyAddr & DMA_BIT_MASK(32));
}

static void rtl_set_rx_tx_config_registers(struct rtl8169_private *tp)
{
	RTL_W32(tp, TxConfig, (7 << TxDMAShift) | (3 << TxInterFrameGapShift));
}

static void rtl_set_rx_mode(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 mc_filter[2];
	int rx_mode;
	u32 tmp;

	if (dev->flags & IFF_PROMISC) {
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
			  AcceptAllPhys;
		mc_filter[0] = mc_filter[1] = 0xffffffff;
	} else if ((netdev_mc_count(dev) > 32) || (dev->flags & IFF_ALLMULTI)) {
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[0] = mc_filter[1] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;

		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[0] = mc_filter[1] = 0;
		netdev_for_each_mc_addr(ha, dev) {
			u32 bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;

			mc_filter[bit_nr >> 5] |= BIT(bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	if (dev->features & NETIF_F_RXALL)
		rx_mode |= AcceptErr | AcceptRunt;

	tmp = (RTL_R32(tp, RxConfig) & ~RX_CONFIG_ACCEPT_MASK) | rx_mode;

	/* The two hash words are byte-swapped relative to mc_filter[]. */
	RTL_W32(tp, MAR0 + 4, swab32(mc_filter[0]));
	RTL_W32(tp, MAR0 + 0, swab32(mc_filter[1]));

	RTL_W32(tp, RxConfig, tmp);
}

static void rtl_rar_set(struct rtl8169_private *tp, const u8 *addr)
{
	RTL_W8(tp, Cfg9346, Cfg9346_Unlock);
	RTL_W32(tp, MAC4, addr[4] | addr[5] << 8);
	RTL_R32(tp, MAC4);
	RTL_W32(tp, MAC0, addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	RTL_R32(tp, MAC0);
	RTL_W8(tp, Cfg9346, Cfg9346_Lock);
}

static void rtl_led_set(struct rtl8169_private *tp)
{
	/* Vendor default LED config for RTD129x. */
	RTL_W32(tp, LEDSEL, 0x0006804f);
}

/*
 * RTL8168g family MAC start sequence (vendor rtl_hw_start_8168g_1/_2 +
 * rtl_hw_start_8168), reduced to the embedded-PHY RTD129x path.
 */
static void rtl_hw_start_8168g(struct rtl8169_private *tp)
{
	RTL_W32(tp, TxConfig, RTL_R32(tp, TxConfig) | TXCFG_AUTO_FIFO);

	rtl_eri_write(tp, 0xc8, ERIAR_MASK_0101, 0x080002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_0001, 0x38, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_0001, 0x48, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00100006, ERIAR_EXGMAC);

	/* csi access enable (vendor rtl_csi_access_enable_1). */
	rtl_csi_write(tp, 0x070c, (rtl_csi_read(tp, 0x070c) & 0x00ffffff) |
		      0x17000000);

	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0x2f8, ERIAR_MASK_0011, 0x1d8f, ERIAR_EXGMAC);

	RTL_W8(tp, ChipCmd, CmdTxEnb | CmdRxEnb);
	RTL_W32(tp, MISC, RTL_R32(tp, MISC) & ~RXDV_GATED_EN);
	RTL_W8(tp, MaxTxPacketSize, 0x27);	/* EarlySize */

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);

	/* Adjust EEE LED frequency. */
	RTL_W8(tp, EEE_LED, RTL_R8(tp, EEE_LED) & ~0x07);

	rtl_w0w1_eri(tp, 0x2fc, ERIAR_MASK_0001, 0x01, 0x06, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_0011, 0x0000, 0x1000, ERIAR_EXGMAC);

	rtl_led_set(tp);

	/* Disable ASPM and clock request before further config. */
	RTL_W8(tp, Config2, RTL_R8(tp, Config2) & ~ClkReqEn);
	RTL_W8(tp, Config5, RTL_R8(tp, Config5) & ~ASPM_en);
}

static void rtl_hw_start(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	RTL_W8(tp, Cfg9346, Cfg9346_Unlock);

	RTL_W8(tp, MaxTxPacketSize, 0x3f);	/* TxPacketMax (8064 >> 7) */
	RTL_W16(tp, RxMaxSize, R8169_RX_BUF_SIZE);

	tp->cp_cmd |= RTL_R16(tp, CPlusCmd) | PktCntrDisable | INTT_1;
	RTL_W16(tp, CPlusCmd, tp->cp_cmd);

	RTL_W16(tp, IntrMitigate, 0x5151);

	rtl_set_rx_tx_desc_registers(tp);
	rtl_set_rx_tx_config_registers(tp);

	RTL_R8(tp, IntrMask);

	rtl_hw_start_8168g(tp);

	RTL_W8(tp, Cfg9346, Cfg9346_Lock);

	RTL_W8(tp, ChipCmd, CmdTxEnb | CmdRxEnb);

	rtl_init_rxcfg(tp);
	rtl_set_rx_mode(dev);

	/* Clear and enable interrupts. */
	rtl_ack_events(tp, 0xffff);
	rtl_irq_enable(tp);
}

/* ------------------------------------------------------------------------- *
 * 7. TX/RX rings, NAPI, interrupt, start_xmit
 * ------------------------------------------------------------------------- */

static void rtl8169_init_ring_indexes(struct rtl8169_private *tp)
{
	tp->dirty_tx = 0;
	tp->cur_tx = 0;
	tp->cur_rx = 0;
}

static bool rtl_tx_slots_avail(struct rtl8169_private *tp)
{
	u32 slots = READ_ONCE(tp->dirty_tx) + NUM_TX_DESC - READ_ONCE(tp->cur_tx);

	return slots >= R8169_TX_STOP_THRS;
}

static void rtl8169_mark_to_asic(struct RxDesc *desc)
{
	u32 eor = le32_to_cpu(desc->opts1) & RingEnd;

	desc->opts2 = 0;
	dma_wmb();
	WRITE_ONCE(desc->opts1, cpu_to_le32(DescOwn | eor | R8169_RX_BUF_SIZE));
}

static struct page *rtl8169_alloc_rx_data(struct rtl8169_private *tp,
					  struct RxDesc *desc)
{
	struct device *d = tp_to_dev(tp);
	dma_addr_t mapping;
	struct page *data;

	data = alloc_pages_node(dev_to_node(d), GFP_KERNEL,
				get_order(R8169_RX_BUF_SIZE));
	if (!data)
		return NULL;

	mapping = dma_map_page(d, data, 0, R8169_RX_BUF_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(d, mapping))) {
		netdev_err(tp->dev, "Failed to map RX DMA!\n");
		__free_pages(data, get_order(R8169_RX_BUF_SIZE));
		return NULL;
	}

	desc->addr = cpu_to_le64(mapping);
	rtl8169_mark_to_asic(desc);
	return data;
}

static void rtl8169_rx_clear(struct rtl8169_private *tp)
{
	int i;

	for (i = 0; i < NUM_RX_DESC && tp->Rx_databuff[i]; i++) {
		dma_unmap_page(tp_to_dev(tp),
			       le64_to_cpu(tp->RxDescArray[i].addr),
			       R8169_RX_BUF_SIZE, DMA_FROM_DEVICE);
		__free_pages(tp->Rx_databuff[i], get_order(R8169_RX_BUF_SIZE));
		tp->Rx_databuff[i] = NULL;
		tp->RxDescArray[i].addr = 0;
		tp->RxDescArray[i].opts1 = 0;
	}
}

static int rtl8169_rx_fill(struct rtl8169_private *tp)
{
	int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		struct page *data;

		data = rtl8169_alloc_rx_data(tp, tp->RxDescArray + i);
		if (!data) {
			rtl8169_rx_clear(tp);
			return -ENOMEM;
		}
		tp->Rx_databuff[i] = data;
	}

	/* Mark the last descriptor as end-of-ring. */
	tp->RxDescArray[NUM_RX_DESC - 1].opts1 |= cpu_to_le32(RingEnd);
	return 0;
}

static int rtl8169_init_ring(struct rtl8169_private *tp)
{
	rtl8169_init_ring_indexes(tp);

	memset(tp->tx_skb, 0, sizeof(tp->tx_skb));
	memset(tp->Rx_databuff, 0, sizeof(tp->Rx_databuff));

	return rtl8169_rx_fill(tp);
}

static void rtl8169_unmap_tx_skb(struct rtl8169_private *tp, unsigned int entry)
{
	struct ring_info *tx_skb = tp->tx_skb + entry;
	struct TxDesc *desc = tp->TxDescArray + entry;

	dma_unmap_single(tp_to_dev(tp), le64_to_cpu(desc->addr), tx_skb->len,
			 DMA_TO_DEVICE);
	memset(desc, 0, sizeof(*desc));
	memset(tx_skb, 0, sizeof(*tx_skb));
}

static void rtl8169_tx_clear_range(struct rtl8169_private *tp, u32 start,
				   unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		unsigned int entry = (start + i) % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;

		if (!tx_skb->skb && !tp->tx_skb[entry].len)
			continue;

		rtl8169_unmap_tx_skb(tp, entry);
		if (tx_skb->skb)
			dev_kfree_skb_any(tx_skb->skb);
	}
}

static void rtl8169_tx_clear(struct rtl8169_private *tp)
{
	rtl8169_tx_clear_range(tp, tp->dirty_tx, NUM_TX_DESC);
	tp->cur_tx = tp->dirty_tx = 0;
	netdev_reset_queue(tp->dev);
}

static void rtl8169_doorbell(struct rtl8169_private *tp)
{
	RTL_W8(tp, TxPoll, NPQ);
}

static int rtl8169_tx_map(struct rtl8169_private *tp, u32 opts0, u32 len,
			  void *addr, unsigned int entry, bool desc_own)
{
	struct TxDesc *txd = tp->TxDescArray + entry;
	struct device *d = tp_to_dev(tp);
	dma_addr_t mapping;
	u32 opts1;

	mapping = dma_map_single(d, addr, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(d, mapping))) {
		if (net_ratelimit())
			netdev_err(tp->dev, "Failed to map TX data!\n");
		return -ENOMEM;
	}

	txd->addr = cpu_to_le64(mapping);
	txd->opts2 = 0;

	opts1 = opts0 | len;
	if (entry == NUM_TX_DESC - 1)
		opts1 |= RingEnd;
	if (desc_own)
		opts1 |= DescOwn;
	txd->opts1 = cpu_to_le32(opts1);

	tp->tx_skb[entry].len = len;
	return 0;
}

static int rtl8169_xmit_frags(struct rtl8169_private *tp, struct sk_buff *skb,
			      u32 opts0, unsigned int entry)
{
	struct skb_shared_info *info = skb_shinfo(skb);
	unsigned int cur_frag;

	for (cur_frag = 0; cur_frag < info->nr_frags; cur_frag++) {
		const skb_frag_t *frag = info->frags + cur_frag;
		void *addr = skb_frag_address(frag);
		u32 len = skb_frag_size(frag);

		entry = (entry + 1) % NUM_TX_DESC;
		if (unlikely(rtl8169_tx_map(tp, opts0, len, addr, entry, true)))
			goto err_out;
	}
	return 0;

err_out:
	rtl8169_tx_clear_range(tp, tp->cur_tx + 1, cur_frag);
	return -EIO;
}

static netdev_tx_t rtl8169_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int entry = tp->cur_tx % NUM_TX_DESC;
	struct TxDesc *txd_first, *txd_last;
	unsigned int frags;

	if (unlikely(!rtl_tx_slots_avail(tp))) {
		if (net_ratelimit())
			netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(rtl8169_tx_map(tp, FirstFrag, skb_headlen(skb), skb->data,
				    entry, false)))
		goto err_drop;

	txd_first = tp->TxDescArray + entry;

	frags = skb_shinfo(skb)->nr_frags;
	if (frags) {
		if (rtl8169_xmit_frags(tp, skb, 0, entry))
			goto err_unmap_first;
		entry = (entry + frags) % NUM_TX_DESC;
	}

	txd_last = tp->TxDescArray + entry;
	txd_last->opts1 |= cpu_to_le32(LastFrag);
	tp->tx_skb[entry].skb = skb;

	skb_tx_timestamp(skb);

	/* Ensure descriptor writes complete before handing FirstFrag to NIC. */
	dma_wmb();

	netdev_sent_queue(dev, skb->len);

	txd_first->opts1 |= cpu_to_le32(DescOwn);

	/* rtl_tx() must observe descriptors before cur_tx advances. */
	smp_wmb();
	WRITE_ONCE(tp->cur_tx, tp->cur_tx + frags + 1);

	rtl8169_doorbell(tp);

	if (!rtl_tx_slots_avail(tp))
		netif_stop_queue(dev);

	return NETDEV_TX_OK;

err_unmap_first:
	rtl8169_unmap_tx_skb(tp, tp->cur_tx % NUM_TX_DESC);
err_drop:
	dev_kfree_skb_any(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static void rtl_tx(struct net_device *dev, struct rtl8169_private *tp,
		   int budget)
{
	unsigned int dirty_tx, bytes_compl = 0, pkts_compl = 0;
	struct sk_buff *skb;

	dirty_tx = tp->dirty_tx;

	while (READ_ONCE(tp->cur_tx) != dirty_tx) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		u32 status;

		status = le32_to_cpu(READ_ONCE(tp->TxDescArray[entry].opts1));
		if (status & DescOwn)
			break;

		skb = tp->tx_skb[entry].skb;
		rtl8169_unmap_tx_skb(tp, entry);

		if (skb) {
			pkts_compl++;
			bytes_compl += skb->len;
			napi_consume_skb(skb, budget);
		}
		dirty_tx++;
	}

	if (tp->dirty_tx != dirty_tx) {
		netdev_completed_queue(dev, pkts_compl, bytes_compl);
		dev->stats.tx_packets += pkts_compl;
		dev->stats.tx_bytes += bytes_compl;

		WRITE_ONCE(tp->dirty_tx, dirty_tx);

		if (netif_queue_stopped(dev) && rtl_tx_slots_avail(tp))
			netif_wake_queue(dev);

		/* Re-kick if more was queued meanwhile (vendor 8168 hack). */
		smp_rmb();
		if (READ_ONCE(tp->cur_tx) != dirty_tx && skb)
			rtl8169_doorbell(tp);
	}
}

static inline int rtl8169_fragmented_frame(u32 status)
{
	return (status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag);
}

static void rtl8169_rx_csum(struct sk_buff *skb, u32 opts1)
{
	u32 status = opts1 & (RxProtoMask | RxCSFailMask);

	if (status == RxProtoTCP || status == RxProtoUDP)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb_checksum_none_assert(skb);
}

static int rtl_rx(struct net_device *dev, struct rtl8169_private *tp, int budget)
{
	struct device *d = tp_to_dev(tp);
	int count;

	for (count = 0; count < budget; count++, tp->cur_rx++) {
		unsigned int pkt_size, entry = tp->cur_rx % NUM_RX_DESC;
		struct RxDesc *desc = tp->RxDescArray + entry;
		struct sk_buff *skb;
		const void *rx_buf;
		dma_addr_t addr;
		u32 status;

		status = le32_to_cpu(READ_ONCE(desc->opts1));
		if (status & DescOwn)
			break;

		/* Order DescOwn read before the rest of the descriptor. */
		dma_rmb();

		if (unlikely(status & RxRES)) {
			if (net_ratelimit())
				netdev_warn(dev, "Rx ERROR. status = %08x\n",
					    status);
			dev->stats.rx_errors++;
			if (status & (RxRWT | RxRUNT))
				dev->stats.rx_length_errors++;
			if (status & RxCRC)
				dev->stats.rx_crc_errors++;

			if (!(dev->features & NETIF_F_RXALL))
				goto release_descriptor;
		}

		pkt_size = status & GENMASK(13, 0);
		if (likely(!(dev->features & NETIF_F_RXFCS)))
			pkt_size -= ETH_FCS_LEN;

		if (unlikely(rtl8169_fragmented_frame(status))) {
			dev->stats.rx_dropped++;
			dev->stats.rx_length_errors++;
			goto release_descriptor;
		}

		skb = napi_alloc_skb(&tp->napi, pkt_size);
		if (unlikely(!skb)) {
			dev->stats.rx_dropped++;
			goto release_descriptor;
		}

		addr = le64_to_cpu(desc->addr);
		rx_buf = page_address(tp->Rx_databuff[entry]);

		dma_sync_single_for_cpu(d, addr, pkt_size, DMA_FROM_DEVICE);
		prefetch(rx_buf);
		skb_copy_to_linear_data(skb, rx_buf, pkt_size);
		skb->tail += pkt_size;
		skb->len = pkt_size;
		dma_sync_single_for_device(d, addr, pkt_size, DMA_FROM_DEVICE);

		rtl8169_rx_csum(skb, status);
		skb->protocol = eth_type_trans(skb, dev);

		if (skb->pkt_type == PACKET_MULTICAST)
			dev->stats.multicast++;

		napi_gro_receive(&tp->napi, skb);

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += pkt_size;
release_descriptor:
		rtl8169_mark_to_asic(desc);
	}

	return count;
}

static irqreturn_t rtl8169_interrupt(int irq, void *dev_instance)
{
	struct rtl8169_private *tp = dev_instance;
	u16 status = rtl_get_events(tp);

	if (status == 0xffff || !(status & tp->irq_mask))
		return IRQ_NONE;

	rtl_ack_events(tp, status);

	if (status & LinkChg && tp->phydev)
		phy_mac_interrupt(tp->phydev);

	rtl_irq_disable(tp);
	napi_schedule(&tp->napi);

	return IRQ_HANDLED;
}

static int rtl8169_poll(struct napi_struct *napi, int budget)
{
	struct rtl8169_private *tp = container_of(napi, struct rtl8169_private,
						  napi);
	struct net_device *dev = tp->dev;
	int work_done;

	rtl_tx(dev, tp, budget);
	work_done = rtl_rx(dev, tp, budget);

	if (work_done < budget && napi_complete_done(napi, work_done))
		rtl_irq_enable(tp);

	return work_done;
}

/* ------------------------------------------------------------------------- *
 * 8. net_device ops, ethtool, open/stop
 * ------------------------------------------------------------------------- */

static int rtl8169_alloc_rings(struct rtl8169_private *tp)
{
	struct device *d = tp_to_dev(tp);

	tp->TxDescArray = dma_alloc_coherent(d, R8169_TX_RING_BYTES,
					     &tp->TxPhyAddr, GFP_KERNEL);
	if (!tp->TxDescArray)
		return -ENOMEM;

	tp->RxDescArray = dma_alloc_coherent(d, R8169_RX_RING_BYTES,
					     &tp->RxPhyAddr, GFP_KERNEL);
	if (!tp->RxDescArray) {
		dma_free_coherent(d, R8169_TX_RING_BYTES, tp->TxDescArray,
				  tp->TxPhyAddr);
		tp->TxDescArray = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void rtl8169_free_rings(struct rtl8169_private *tp)
{
	struct device *d = tp_to_dev(tp);

	if (tp->RxDescArray) {
		dma_free_coherent(d, R8169_RX_RING_BYTES, tp->RxDescArray,
				  tp->RxPhyAddr);
		tp->RxDescArray = NULL;
	}
	if (tp->TxDescArray) {
		dma_free_coherent(d, R8169_TX_RING_BYTES, tp->TxDescArray,
				  tp->TxPhyAddr);
		tp->TxDescArray = NULL;
	}
}

static int rtl8169_open(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	ret = rtl8169_alloc_rings(tp);
	if (ret)
		return ret;

	ret = rtl8169_init_ring(tp);
	if (ret)
		goto err_free_rings;

	rtl8169_hw_reset(tp);

	napi_enable(&tp->napi);

	ret = request_irq(tp->irq, rtl8169_interrupt, IRQF_SHARED, dev->name,
			  tp);
	if (ret)
		goto err_napi_off;

	rtl_hw_start(dev);

	phy_start(tp->phydev);
	netif_start_queue(dev);

	return 0;

err_napi_off:
	napi_disable(&tp->napi);
	rtl8169_rx_clear(tp);
err_free_rings:
	rtl8169_free_rings(tp);
	return ret;
}

static int rtl8169_close(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	phy_stop(tp->phydev);

	netif_stop_queue(dev);
	napi_disable(&tp->napi);

	rtl8169_hw_reset(tp);

	synchronize_irq(tp->irq);
	free_irq(tp->irq, tp);

	rtl8169_tx_clear(tp);
	rtl8169_rx_clear(tp);
	rtl8169_free_rings(tp);

	return 0;
}

static void rtl8169_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	netdev_err(dev, "Tx timeout\n");

	/* Restart the MAC: reset, rebuild rings, start again. */
	napi_disable(&tp->napi);
	rtl8169_hw_reset(tp);
	rtl8169_tx_clear(tp);
	rtl8169_init_ring_indexes(tp);
	napi_enable(&tp->napi);
	rtl_hw_start(dev);
	netif_wake_queue(dev);
}

static void rtl8169_get_stats64(struct net_device *dev,
				struct rtnl_link_stats64 *stats)
{
	netdev_stats_to_stats64(stats, &dev->stats);
}

static int rtl8169_change_mtu(struct net_device *dev, int new_mtu)
{
	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}

static int rtl8169_set_mac_address(struct net_device *dev, void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(dev, addr->sa_data);
	rtl_rar_set(tp, dev->dev_addr);

	return 0;
}

static const struct net_device_ops rtl_netdev_ops = {
	.ndo_open		= rtl8169_open,
	.ndo_stop		= rtl8169_close,
	.ndo_start_xmit		= rtl8169_start_xmit,
	.ndo_tx_timeout		= rtl8169_tx_timeout,
	.ndo_get_stats64	= rtl8169_get_stats64,
	.ndo_change_mtu		= rtl8169_change_mtu,
	.ndo_set_mac_address	= rtl8169_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= rtl_set_rx_mode,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
};

/* ethtool: drvinfo, link, and link settings delegated to phylib. */
static void rtl8169_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, dev_name(tp_to_dev(tp)),
		sizeof(info->bus_info));
}

static const struct ethtool_ops rtl8169_ethtool_ops = {
	.get_drvinfo		= rtl8169_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
};

/* ------------------------------------------------------------------------- *
 * 9. platform_driver probe / remove
 * ------------------------------------------------------------------------- */

static int r8169soc_register_mdio(struct rtl8169_private *tp)
{
	struct device *dev = tp_to_dev(tp);
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "r8169soc-mii";
	bus->priv = tp;
	bus->read = r8169soc_mdio_bus_read;
	bus->write = r8169soc_mdio_bus_write;
	bus->parent = dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "r8169soc-%s", dev_name(dev));

	/* Only the embedded PHY at INT_PHY_ADDR exists on this bus. */
	bus->phy_mask = (u32)~BIT(INT_PHY_ADDR);

	ret = devm_mdiobus_register(dev, bus);
	if (ret)
		return ret;

	tp->mii_bus = bus;
	return 0;
}

static int r8169soc_connect_phy(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	struct phy_device *phydev;

	phydev = mdiobus_get_phy(tp->mii_bus, INT_PHY_ADDR);
	if (!phydev) {
		netdev_err(dev, "embedded PHY not found at addr %d\n",
			   INT_PHY_ADDR);
		return -ENODEV;
	}

	phydev = phy_connect(dev, phydev_name(phydev), rtl8169_adjust_link,
			     PHY_INTERFACE_MODE_INTERNAL);
	if (IS_ERR(phydev)) {
		netdev_err(dev, "could not connect to embedded PHY\n");
		return PTR_ERR(phydev);
	}

	phy_set_max_speed(phydev, SPEED_1000);
	phy_support_asym_pause(phydev);

	tp->phydev = phydev;
	phy_attached_info(phydev);

	return 0;
}

static int r8169soc_get_clocks_resets(struct rtl8169_private *tp)
{
	struct device *dev = tp_to_dev(tp);

	tp->clk_etn_sys = devm_clk_get(dev, "etn_sys");
	if (IS_ERR(tp->clk_etn_sys))
		return dev_err_probe(dev, PTR_ERR(tp->clk_etn_sys),
				     "failed to get etn_sys clock\n");

	tp->clk_etn_250m = devm_clk_get(dev, "etn_250m");
	if (IS_ERR(tp->clk_etn_250m))
		return dev_err_probe(dev, PTR_ERR(tp->clk_etn_250m),
				     "failed to get etn_250m clock\n");

	tp->rstc_gmac = devm_reset_control_get_exclusive(dev, "gmac");
	if (IS_ERR(tp->rstc_gmac))
		return dev_err_probe(dev, PTR_ERR(tp->rstc_gmac),
				     "failed to get gmac reset\n");

	tp->rstc_gphy = devm_reset_control_get_exclusive(dev, "gphy");
	if (IS_ERR(tp->rstc_gphy))
		return dev_err_probe(dev, PTR_ERR(tp->rstc_gphy),
				     "failed to get gphy reset\n");

	return 0;
}

static void r8169soc_get_mac_address(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	struct device_node *factory;
	const char *ethaddr;
	u8 mac[ETH_ALEN];
	u32 l, h;

	/* 1. Standard mac-address / local-mac-address on the MAC node. */
	if (of_get_mac_address(tp->pdev->dev.of_node, mac) == 0) {
		eth_hw_addr_set(dev, mac);
		return;
	}

	/*
	 * 2. The Realtek vendor U-Boot does not patch the ethernet node;
	 *    instead it injects the board's address as an "XX:XX:..." string
	 *    in the /factory node's "ethaddr" property. Honour that.
	 */
	factory = of_find_node_by_path("/factory");
	if (factory) {
		if (of_property_read_string(factory, "ethaddr", &ethaddr) == 0 &&
		    mac_pton(ethaddr, mac) && is_valid_ether_addr(mac)) {
			eth_hw_addr_set(dev, mac);
			of_node_put(factory);
			netdev_info(dev, "using U-Boot /factory MAC %pM\n", mac);
			return;
		}
		of_node_put(factory);
	}

	/*
	 * 3. U-Boot may also have left the address programmed in the MAC0/MAC4
	 *    registers (this is the path the 4.9 vendor driver relied on).
	 */
	l = RTL_R32(tp, MAC0);
	h = RTL_R32(tp, MAC4);
	mac[0] = l;
	mac[1] = l >> 8;
	mac[2] = l >> 16;
	mac[3] = l >> 24;
	mac[4] = h;
	mac[5] = h >> 8;
	if (is_valid_ether_addr(mac)) {
		eth_hw_addr_set(dev, mac);
		netdev_info(dev, "using MAC %pM from MAC0 registers\n", mac);
		return;
	}

	/* 4. Last resort. */
	eth_hw_addr_random(dev);
	netdev_info(dev, "using random MAC address %pM\n", dev->dev_addr);
}

static int r8169soc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtl8169_private *tp;
	struct net_device *ndev;
	struct resource *res;
	int ret;

	ndev = devm_alloc_etherdev(dev, sizeof(*tp));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, dev);
	tp = netdev_priv(ndev);
	tp->dev = ndev;
	tp->pdev = pdev;
	tp->mac_version = RTL_MAC_VER_42;
	tp->irq_mask = RTL_INTR_MASK;
	tp->cur_speed = -1;
	tp->cur_duplex = -1;
	platform_set_drvdata(pdev, tp);

	/* reg[0] = MAC registers, reg[1] = ISO syscon (PHY/PLL control). */
	tp->mmio_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tp->mmio_addr))
		return PTR_ERR(tp->mmio_addr);

	/*
	 * The ISO syscon block is shared with other peripherals (uart0 etc.
	 * live in the same 4 KiB window), so map it non-exclusively instead of
	 * requesting the region, which would clash with those drivers.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;
	tp->mmio_clkaddr = devm_ioremap(dev, res->start, resource_size(res));
	if (!tp->mmio_clkaddr)
		return -ENOMEM;

	tp->irq = platform_get_irq(pdev, 0);
	if (tp->irq < 0)
		return tp->irq;
	ndev->irq = tp->irq;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = r8169soc_get_clocks_resets(tp);
	if (ret)
		return ret;

	/* --- SoC bring-up: power up PHY + GMAC, init internal MDIO --- */
	r8169soc_pll_clock_init(tp);
	r8169soc_mdio_init(tp);
	r8169soc_mac_mcu_patch(tp);
	r8169soc_phy_config(tp);

	rtl8169_hw_reset(tp);
	rtl_init_rxcfg(tp);

	r8169soc_get_mac_address(tp);
	rtl_rar_set(tp, ndev->dev_addr);

	/* --- net_device setup --- */
	ndev->netdev_ops = &rtl_netdev_ops;
	ndev->ethtool_ops = &rtl8169_ethtool_ops;
	ndev->watchdog_timeo = RTL8169_TX_TIMEOUT;

	ndev->features |= NETIF_F_SG | NETIF_F_RXCSUM |
			  NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;
	ndev->hw_features = ndev->features | NETIF_F_RXALL | NETIF_F_RXFCS;
	ndev->vlan_features = NETIF_F_SG;

	ndev->min_mtu = ETH_MIN_MTU;
	ndev->max_mtu = ETH_DATA_LEN;

	netif_napi_add(ndev, &tp->napi, rtl8169_poll);

	ret = r8169soc_register_mdio(tp);
	if (ret)
		goto err_napi_del;

	ret = r8169soc_connect_phy(tp);
	if (ret)
		goto err_napi_del;

	ret = register_netdev(ndev);
	if (ret)
		goto err_phy_disconnect;

	netdev_info(ndev, "RTD1295 GMAC, embedded PHY, MAC %pM, IRQ %d\n",
		    ndev->dev_addr, tp->irq);

	return 0;

err_phy_disconnect:
	phy_disconnect(tp->phydev);
err_napi_del:
	netif_napi_del(&tp->napi);
	return ret;
}

static void r8169soc_remove(struct platform_device *pdev)
{
	struct rtl8169_private *tp = platform_get_drvdata(pdev);

	unregister_netdev(tp->dev);
	phy_disconnect(tp->phydev);
	netif_napi_del(&tp->napi);

	clk_disable_unprepare(tp->clk_etn_250m);
	clk_disable_unprepare(tp->clk_etn_sys);
}

static const struct of_device_id r8169soc_of_match[] = {
	{ .compatible = "realtek,rtd1295-gmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, r8169soc_of_match);

static struct platform_driver r8169soc_driver = {
	.probe	= r8169soc_probe,
	.remove	= r8169soc_remove,
	.driver	= {
		.name		= DRV_NAME,
		.of_match_table	= r8169soc_of_match,
	},
};
module_platform_driver(r8169soc_driver);

MODULE_DESCRIPTION("Realtek RTD1295 on-SoC Gigabit Ethernet (GMAC) driver");
MODULE_AUTHOR("Realtek Semiconductor Corp.");
MODULE_LICENSE("GPL");
