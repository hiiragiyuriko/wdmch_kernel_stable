// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spin Table SMP initialisation
 *
 * Copyright (C) 2013 ARM Ltd.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>
#include <asm/cputype.h>
#include <asm/io.h>
#include <asm/smp_plat.h>

extern void secondary_holding_pen(void);
volatile unsigned long __section(".mmuoff.data.read")
secondary_holding_pen_release = INVALID_HWID;

static phys_addr_t cpu_release_addr[NR_CPUS];

/*
 * Write secondary_holding_pen_release in a way that is guaranteed to be
 * visible to all observers, irrespective of whether they're taking part
 * in coherency or not.  This is necessary for the hotplug code to work
 * reliably.
 */
static void write_pen_release(u64 val)
{
	void *start = (void *)&secondary_holding_pen_release;
	unsigned long size = sizeof(secondary_holding_pen_release);

	secondary_holding_pen_release = val;
	dcache_clean_inval_poc((unsigned long)start, (unsigned long)start + size);
}


static int smp_spin_table_cpu_init(unsigned int cpu)
{
	struct device_node *dn;
	int ret;

	dn = of_get_cpu_node(cpu, NULL);
	if (!dn)
		return -ENODEV;

	/*
	 * Determine the address from which the CPU is polling.
	 */
	ret = of_property_read_u64(dn, "cpu-release-addr",
				   &cpu_release_addr[cpu]);
	if (ret)
		pr_err("CPU %d: missing or invalid cpu-release-addr property\n",
		       cpu);

	of_node_put(dn);

	return ret;
}

static int smp_spin_table_cpu_prepare(unsigned int cpu)
{
	__le64 __iomem *release_addr;
	phys_addr_t pa_holding_pen = __pa_symbol(secondary_holding_pen);

	if (!cpu_release_addr[cpu])
		return -ENODEV;

	/*
	 * The cpu-release-addr may or may not be inside the linear mapping.
	 * As ioremap_cache will either give us a new mapping or reuse the
	 * existing linear mapping, we can use it to cover both cases. In
	 * either case the memory will be MT_NORMAL.
	 */
	release_addr = ioremap_cache(cpu_release_addr[cpu],
				     sizeof(*release_addr));
	if (!release_addr)
		return -ENOMEM;

	/*
	 * We write the release address as LE regardless of the native
	 * endianness of the kernel. Therefore, any boot-loaders that
	 * read this address need to convert this address to the
	 * boot-loader's endianness before jumping. This is mandated by
	 * the boot protocol.
	 */
	writeq_relaxed(pa_holding_pen, release_addr);
	dcache_clean_inval_poc((__force unsigned long)release_addr,
			    (__force unsigned long)release_addr +
				    sizeof(*release_addr));

	/*
	 * Send an event to wake up the secondary CPU.
	 */
	sev();

	iounmap(release_addr);

	return 0;
}

static int smp_spin_table_cpu_boot(unsigned int cpu)
{
	/*
	 * Update the pen release flag.
	 */
	write_pen_release(cpu_logical_map(cpu));

	/*
	 * Send an event, causing the secondaries to read pen_release.
	 */
	sev();

	return 0;
}

const struct cpu_operations smp_spin_table_ops = {
	.name		= "spin-table",
	.cpu_init	= smp_spin_table_cpu_init,
	.cpu_prepare	= smp_spin_table_cpu_prepare,
	.cpu_boot	= smp_spin_table_cpu_boot,
};

/*
 * Realtek RTD129x variant of the spin-table.
 *
 * On these SoCs (RTD1295/RTD1296, e.g. the WD My Cloud Home / Monarch board)
 * the cpu-release-addr does not point at DRAM but at a 32-bit register inside
 * the SB2 register block (0x9801aa44). The on-chip boot code / U-Boot holds
 * the secondary cores spinning on that register and polls it as a 32-bit
 * little-endian word. The generic spin-table writes a 64-bit value through a
 * cacheable (MT_NORMAL) mapping, which neither matches the register width nor
 * the device memory type, so the secondaries are never released.
 *
 * This variant maps the release address as device memory and writes the
 * holding-pen entry point as a 32-bit value, matching the behaviour of the
 * Realtek 4.9 vendor kernel's "rtk-spin-table" enable-method for cold boot.
 * The holding-pen/pen_release handshake is identical to the generic path, so
 * cpu_init and cpu_boot are reused as-is.
 */
static int rtk_smp_spin_table_cpu_prepare(unsigned int cpu)
{
	void __iomem *release_addr;
	phys_addr_t pa_holding_pen = __pa_symbol(secondary_holding_pen);

	if (!cpu_release_addr[cpu])
		return -ENODEV;

	if (pa_holding_pen > U32_MAX) {
		pr_err("CPU %d: holding pen %pa out of 32-bit range for rtk-spin-table\n",
		       cpu, &pa_holding_pen);
		return -EINVAL;
	}

	release_addr = ioremap(cpu_release_addr[cpu], sizeof(u32));
	if (!release_addr)
		return -ENOMEM;

	/*
	 * Write the holding-pen entry as a 32-bit LE word, regardless of the
	 * native endianness of the kernel, then kick the secondaries.
	 */
	writel_relaxed(lower_32_bits(pa_holding_pen), release_addr);

	sev();

	iounmap(release_addr);

	return 0;
}

const struct cpu_operations rtk_smp_spin_table_ops = {
	.name		= "rtk-spin-table",
	.cpu_init	= smp_spin_table_cpu_init,
	.cpu_prepare	= rtk_smp_spin_table_cpu_prepare,
	.cpu_boot	= smp_spin_table_cpu_boot,
};
