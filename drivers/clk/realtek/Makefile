# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_COMMON_CLK_REALTEK) += clk-rtk.o

clk-rtk-y += common.o
clk-rtk-y += debugfs.o
clk-rtk-y += clk-regmap-mux.o
clk-rtk-y += clk-regmap-gate.o
clk-rtk-y += clk-pll.o
clk-rtk-y += reset.o
clk-rtk-$(CONFIG_COMMON_CLK_RTD1295) += clk-rtd1295-cc.o
clk-rtk-$(CONFIG_COMMON_CLK_RTD1295) += clk-rtd1295-ic.o
