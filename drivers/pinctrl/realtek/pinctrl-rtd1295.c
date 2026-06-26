// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek RTD1295 pin controller driver (ISO pad bank).
 *
 * The RTD1295 spreads its pad mux across four register windows (ISO, CRT,
 * SB2 and DISP). The mainline pinctrl-rtd framework drives a single
 * register window per instance, so each bank is described as its own
 * device node; this file provides the ISO bank (0x98007000), which carries
 * the practically used low-speed pads: I2C 0/1/6, UART 0/1/2, IR, the
 * Ethernet/NAT LEDs and the ISO GPIOs.
 *
 * Pad data (mux register offset/bit/width and per-pin function values) is
 * taken from the Realtek 4.9 vendor pinctrl-rtd129x tables. Only pin muxing
 * is implemented; the RTD1295 pad-config (drive/pull) register layout does
 * not match the framework's config model, and the board relies on external
 * pulls and reset-default drive strengths, so pinconf is left unsupported.
 *
 * Copyright (c) 2024 Realtek bring-up for the WD Monarch port.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include "pinctrl-rtd.h"

enum rtd1295_iso_pins {
	RTD1295_ISO_GPIO_0 = 0,
	RTD1295_ISO_GPIO_1,
	RTD1295_ISO_GPIO_2,
	RTD1295_ISO_GPIO_3,
	RTD1295_ISO_GPIO_4,
	RTD1295_ISO_GPIO_5,
	RTD1295_ISO_HDMI_HPD,
	RTD1295_ISO_GPIO_7,
	RTD1295_ISO_IR_RX,
	RTD1295_ISO_IR_TX,
	RTD1295_ISO_UR0_RX,
	RTD1295_ISO_UR0_TX,
	RTD1295_ISO_UR1_RX,
	RTD1295_ISO_UR1_TX,
	RTD1295_ISO_UR1_CTS_N,
	RTD1295_ISO_UR1_RTS_N,
	RTD1295_ISO_I2C_SCL_0,
	RTD1295_ISO_I2C_SDA_0,
	RTD1295_ISO_I2C_SCL_1,
	RTD1295_ISO_I2C_SDA_1,
	RTD1295_ISO_I2C_SCL_6,
	RTD1295_ISO_GPIO_21,
	RTD1295_ISO_GPIO_22,
	RTD1295_ISO_GPIO_23,
	RTD1295_ISO_GPIO_24,
	RTD1295_ISO_GPIO_25,
	RTD1295_ISO_I2C_SDA_6,
	RTD1295_ISO_ETN_LED_LINK,
	RTD1295_ISO_ETN_LED_RXTX,
	RTD1295_ISO_NAT_LED_0,
	RTD1295_ISO_NAT_LED_1,
	RTD1295_ISO_NAT_LED_2,
	RTD1295_ISO_NAT_LED_3,
	RTD1295_ISO_GPIO_33,
	RTD1295_ISO_GPIO_34,
	RTD1295_ISO_NUM
};

static const struct pinctrl_pin_desc rtd1295_iso_pins[] = {
	PINCTRL_PIN(RTD1295_ISO_GPIO_0, "iso_gpio_0"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_1, "iso_gpio_1"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_2, "iso_gpio_2"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_3, "iso_gpio_3"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_4, "iso_gpio_4"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_5, "iso_gpio_5"),
	PINCTRL_PIN(RTD1295_ISO_HDMI_HPD, "hdmi_hpd"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_7, "iso_gpio_7"),
	PINCTRL_PIN(RTD1295_ISO_IR_RX, "ir_rx"),
	PINCTRL_PIN(RTD1295_ISO_IR_TX, "ir_tx"),
	PINCTRL_PIN(RTD1295_ISO_UR0_RX, "ur0_rx"),
	PINCTRL_PIN(RTD1295_ISO_UR0_TX, "ur0_tx"),
	PINCTRL_PIN(RTD1295_ISO_UR1_RX, "ur1_rx"),
	PINCTRL_PIN(RTD1295_ISO_UR1_TX, "ur1_tx"),
	PINCTRL_PIN(RTD1295_ISO_UR1_CTS_N, "ur1_cts_n"),
	PINCTRL_PIN(RTD1295_ISO_UR1_RTS_N, "ur1_rts_n"),
	PINCTRL_PIN(RTD1295_ISO_I2C_SCL_0, "i2c_scl_0"),
	PINCTRL_PIN(RTD1295_ISO_I2C_SDA_0, "i2c_sda_0"),
	PINCTRL_PIN(RTD1295_ISO_I2C_SCL_1, "i2c_scl_1"),
	PINCTRL_PIN(RTD1295_ISO_I2C_SDA_1, "i2c_sda_1"),
	PINCTRL_PIN(RTD1295_ISO_I2C_SCL_6, "i2c_scl_6"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_21, "iso_gpio_21"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_22, "iso_gpio_22"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_23, "iso_gpio_23"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_24, "iso_gpio_24"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_25, "iso_gpio_25"),
	PINCTRL_PIN(RTD1295_ISO_I2C_SDA_6, "i2c_sda_6"),
	PINCTRL_PIN(RTD1295_ISO_ETN_LED_LINK, "etn_led_link"),
	PINCTRL_PIN(RTD1295_ISO_ETN_LED_RXTX, "etn_led_rxtx"),
	PINCTRL_PIN(RTD1295_ISO_NAT_LED_0, "nat_led_0"),
	PINCTRL_PIN(RTD1295_ISO_NAT_LED_1, "nat_led_1"),
	PINCTRL_PIN(RTD1295_ISO_NAT_LED_2, "nat_led_2"),
	PINCTRL_PIN(RTD1295_ISO_NAT_LED_3, "nat_led_3"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_33, "iso_gpio_33"),
	PINCTRL_PIN(RTD1295_ISO_GPIO_34, "iso_gpio_34"),
};

/* One group per pin, named after the pin. */
#define RTD1295_PIN_GROUP(_name, _pin) \
	static const unsigned int _name ## _pins[] = { _pin }

RTD1295_PIN_GROUP(iso_gpio_0, RTD1295_ISO_GPIO_0);
RTD1295_PIN_GROUP(iso_gpio_1, RTD1295_ISO_GPIO_1);
RTD1295_PIN_GROUP(iso_gpio_2, RTD1295_ISO_GPIO_2);
RTD1295_PIN_GROUP(iso_gpio_3, RTD1295_ISO_GPIO_3);
RTD1295_PIN_GROUP(iso_gpio_4, RTD1295_ISO_GPIO_4);
RTD1295_PIN_GROUP(iso_gpio_5, RTD1295_ISO_GPIO_5);
RTD1295_PIN_GROUP(hdmi_hpd, RTD1295_ISO_HDMI_HPD);
RTD1295_PIN_GROUP(iso_gpio_7, RTD1295_ISO_GPIO_7);
RTD1295_PIN_GROUP(ir_rx, RTD1295_ISO_IR_RX);
RTD1295_PIN_GROUP(ir_tx, RTD1295_ISO_IR_TX);
RTD1295_PIN_GROUP(ur0_rx, RTD1295_ISO_UR0_RX);
RTD1295_PIN_GROUP(ur0_tx, RTD1295_ISO_UR0_TX);
RTD1295_PIN_GROUP(ur1_rx, RTD1295_ISO_UR1_RX);
RTD1295_PIN_GROUP(ur1_tx, RTD1295_ISO_UR1_TX);
RTD1295_PIN_GROUP(ur1_cts_n, RTD1295_ISO_UR1_CTS_N);
RTD1295_PIN_GROUP(ur1_rts_n, RTD1295_ISO_UR1_RTS_N);
RTD1295_PIN_GROUP(i2c_scl_0, RTD1295_ISO_I2C_SCL_0);
RTD1295_PIN_GROUP(i2c_sda_0, RTD1295_ISO_I2C_SDA_0);
RTD1295_PIN_GROUP(i2c_scl_1, RTD1295_ISO_I2C_SCL_1);
RTD1295_PIN_GROUP(i2c_sda_1, RTD1295_ISO_I2C_SDA_1);
RTD1295_PIN_GROUP(i2c_scl_6, RTD1295_ISO_I2C_SCL_6);
RTD1295_PIN_GROUP(iso_gpio_21, RTD1295_ISO_GPIO_21);
RTD1295_PIN_GROUP(iso_gpio_22, RTD1295_ISO_GPIO_22);
RTD1295_PIN_GROUP(iso_gpio_23, RTD1295_ISO_GPIO_23);
RTD1295_PIN_GROUP(iso_gpio_24, RTD1295_ISO_GPIO_24);
RTD1295_PIN_GROUP(iso_gpio_25, RTD1295_ISO_GPIO_25);
RTD1295_PIN_GROUP(i2c_sda_6, RTD1295_ISO_I2C_SDA_6);
RTD1295_PIN_GROUP(etn_led_link, RTD1295_ISO_ETN_LED_LINK);
RTD1295_PIN_GROUP(etn_led_rxtx, RTD1295_ISO_ETN_LED_RXTX);
RTD1295_PIN_GROUP(nat_led_0, RTD1295_ISO_NAT_LED_0);
RTD1295_PIN_GROUP(nat_led_1, RTD1295_ISO_NAT_LED_1);
RTD1295_PIN_GROUP(nat_led_2, RTD1295_ISO_NAT_LED_2);
RTD1295_PIN_GROUP(nat_led_3, RTD1295_ISO_NAT_LED_3);
RTD1295_PIN_GROUP(iso_gpio_33, RTD1295_ISO_GPIO_33);
RTD1295_PIN_GROUP(iso_gpio_34, RTD1295_ISO_GPIO_34);

#define RTD1295_GROUP(_name) \
	{ \
		.name = # _name, \
		.pins = _name ## _pins, \
		.num_pins = ARRAY_SIZE(_name ## _pins), \
	}

static const struct rtd_pin_group_desc rtd1295_iso_groups[] = {
	RTD1295_GROUP(iso_gpio_0),
	RTD1295_GROUP(iso_gpio_1),
	RTD1295_GROUP(iso_gpio_2),
	RTD1295_GROUP(iso_gpio_3),
	RTD1295_GROUP(iso_gpio_4),
	RTD1295_GROUP(iso_gpio_5),
	RTD1295_GROUP(hdmi_hpd),
	RTD1295_GROUP(iso_gpio_7),
	RTD1295_GROUP(ir_rx),
	RTD1295_GROUP(ir_tx),
	RTD1295_GROUP(ur0_rx),
	RTD1295_GROUP(ur0_tx),
	RTD1295_GROUP(ur1_rx),
	RTD1295_GROUP(ur1_tx),
	RTD1295_GROUP(ur1_cts_n),
	RTD1295_GROUP(ur1_rts_n),
	RTD1295_GROUP(i2c_scl_0),
	RTD1295_GROUP(i2c_sda_0),
	RTD1295_GROUP(i2c_scl_1),
	RTD1295_GROUP(i2c_sda_1),
	RTD1295_GROUP(i2c_scl_6),
	RTD1295_GROUP(iso_gpio_21),
	RTD1295_GROUP(iso_gpio_22),
	RTD1295_GROUP(iso_gpio_23),
	RTD1295_GROUP(iso_gpio_24),
	RTD1295_GROUP(iso_gpio_25),
	RTD1295_GROUP(i2c_sda_6),
	RTD1295_GROUP(etn_led_link),
	RTD1295_GROUP(etn_led_rxtx),
	RTD1295_GROUP(nat_led_0),
	RTD1295_GROUP(nat_led_1),
	RTD1295_GROUP(nat_led_2),
	RTD1295_GROUP(nat_led_3),
	RTD1295_GROUP(iso_gpio_33),
	RTD1295_GROUP(iso_gpio_34),
};

static const char * const rtd1295_gpio_groups[] = {
	"iso_gpio_0", "iso_gpio_1", "iso_gpio_2", "iso_gpio_3", "iso_gpio_4",
	"iso_gpio_5", "hdmi_hpd", "iso_gpio_7", "ir_rx", "ir_tx", "ur0_rx",
	"ur0_tx", "ur1_rx", "ur1_tx", "ur1_cts_n", "ur1_rts_n", "i2c_scl_0",
	"i2c_sda_0", "i2c_scl_1", "i2c_sda_1", "i2c_scl_6", "iso_gpio_21",
	"iso_gpio_22", "iso_gpio_23", "iso_gpio_24", "iso_gpio_25", "i2c_sda_6",
	"etn_led_link", "etn_led_rxtx", "nat_led_0", "nat_led_1", "nat_led_2",
	"nat_led_3", "iso_gpio_33", "iso_gpio_34",
};

static const char * const rtd1295_standby_dbg_groups[] = {
	"iso_gpio_2", "iso_gpio_3", "ir_rx",
};

static const char * const rtd1295_acpu_ejtag_loc_iso_groups[] = {
	"iso_gpio_2", "iso_gpio_3", "iso_gpio_4", "iso_gpio_5", "iso_gpio_7",
};

static const char * const rtd1295_uart2_0_groups[] = {
	"iso_gpio_2", "iso_gpio_3", "iso_gpio_4", "iso_gpio_5",
};

static const char * const rtd1295_uart2_1_groups[] = {
	"iso_gpio_23", "iso_gpio_24", "iso_gpio_33", "iso_gpio_34",
};

static const char * const rtd1295_edp_hpd_groups[] = { "iso_gpio_7", };
static const char * const rtd1295_ir_rx_groups[] = { "ir_rx", };
static const char * const rtd1295_ir_tx_groups[] = { "ir_tx", };
static const char * const rtd1295_uart0_groups[] = { "ur0_rx", "ur0_tx", };
static const char * const rtd1295_uart1_groups[] = {
	"ur1_rx", "ur1_tx", "ur1_cts_n", "ur1_rts_n",
};
static const char * const rtd1295_i2c0_groups[] = { "i2c_scl_0", "i2c_sda_0", };
static const char * const rtd1295_i2c1_groups[] = { "i2c_scl_1", "i2c_sda_1", };
static const char * const rtd1295_i2c6_groups[] = { "i2c_scl_6", "i2c_sda_6", };
static const char * const rtd1295_rtc_groups[] = { "iso_gpio_25", };
static const char * const rtd1295_etn_led_groups[] = {
	"etn_led_link", "etn_led_rxtx",
};
static const char * const rtd1295_nat_led_groups[] = {
	"nat_led_0", "nat_led_1", "nat_led_2", "nat_led_3",
};
static const char * const rtd1295_sc_groups[] = {
	"nat_led_0", "nat_led_1", "nat_led_2", "nat_led_3",
};
static const char * const rtd1295_pwm_groups[] = {
	"iso_gpio_21", "iso_gpio_22", "iso_gpio_23", "iso_gpio_24",
	"etn_led_link", "etn_led_rxtx", "nat_led_0", "nat_led_1",
};

#define RTD1295_FUNC(_name) \
	{ \
		.name = # _name, \
		.groups = rtd1295_ ## _name ## _groups, \
		.num_groups = ARRAY_SIZE(rtd1295_ ## _name ## _groups), \
	}

static const struct rtd_pin_func_desc rtd1295_iso_functions[] = {
	RTD1295_FUNC(gpio),
	RTD1295_FUNC(standby_dbg),
	RTD1295_FUNC(acpu_ejtag_loc_iso),
	RTD1295_FUNC(uart2_0),
	RTD1295_FUNC(uart2_1),
	RTD1295_FUNC(edp_hpd),
	RTD1295_FUNC(ir_rx),
	RTD1295_FUNC(ir_tx),
	RTD1295_FUNC(uart0),
	RTD1295_FUNC(uart1),
	RTD1295_FUNC(i2c0),
	RTD1295_FUNC(i2c1),
	RTD1295_FUNC(i2c6),
	RTD1295_FUNC(rtc),
	RTD1295_FUNC(etn_led),
	RTD1295_FUNC(nat_led),
	RTD1295_FUNC(sc),
	RTD1295_FUNC(pwm),
};

static const struct rtd_pin_desc rtd1295_iso_muxes[RTD1295_ISO_NUM] = {
	[RTD1295_ISO_GPIO_0] = RTK_PIN_MUX(iso_gpio_0, 0x0, 0x0,
		RTK_PIN_FUNC(0x0, "gpio")),
	[RTD1295_ISO_GPIO_1] = RTK_PIN_MUX(iso_gpio_1, 0x0, 0x0,
		RTK_PIN_FUNC(0x0, "gpio")),
	[RTD1295_ISO_GPIO_2] = RTK_PIN_MUX(iso_gpio_2, 0x314, GENMASK(8, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 6), "acpu_ejtag_loc_iso"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 6), "uart2_0")),
	[RTD1295_ISO_GPIO_3] = RTK_PIN_MUX(iso_gpio_3, 0x314, GENMASK(11, 9),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 9), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 9), "standby_dbg"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 9), "acpu_ejtag_loc_iso"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x4, 9), "uart2_0")),
	[RTD1295_ISO_GPIO_4] = RTK_PIN_MUX(iso_gpio_4, 0x310, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "uart2_0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "acpu_ejtag_loc_iso")),
	[RTD1295_ISO_GPIO_5] = RTK_PIN_MUX(iso_gpio_5, 0x310, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "uart2_0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 2), "acpu_ejtag_loc_iso")),
	[RTD1295_ISO_HDMI_HPD] = RTK_PIN_MUX(hdmi_hpd, 0x314, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio")),
	[RTD1295_ISO_GPIO_7] = RTK_PIN_MUX(iso_gpio_7, 0x310, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "edp_hpd"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "acpu_ejtag_loc_iso")),
	[RTD1295_ISO_IR_RX] = RTK_PIN_MUX(ir_rx, 0x310, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "ir_rx"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "standby_dbg")),
	[RTD1295_ISO_IR_TX] = RTK_PIN_MUX(ir_tx, 0x310, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "ir_tx")),
	[RTD1295_ISO_UR0_RX] = RTK_PIN_MUX(ur0_rx, 0x310, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "uart0")),
	[RTD1295_ISO_UR0_TX] = RTK_PIN_MUX(ur0_tx, 0x310, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "uart0")),
	[RTD1295_ISO_UR1_RX] = RTK_PIN_MUX(ur1_rx, 0x310, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "uart1")),
	[RTD1295_ISO_UR1_TX] = RTK_PIN_MUX(ur1_tx, 0x310, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "uart1")),
	[RTD1295_ISO_UR1_CTS_N] = RTK_PIN_MUX(ur1_cts_n, 0x310, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "uart1")),
	[RTD1295_ISO_UR1_RTS_N] = RTK_PIN_MUX(ur1_rts_n, 0x310, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "uart1")),
	[RTD1295_ISO_I2C_SCL_0] = RTK_PIN_MUX(i2c_scl_0, 0x310, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "i2c0")),
	[RTD1295_ISO_I2C_SDA_0] = RTK_PIN_MUX(i2c_sda_0, 0x310, GENMASK(25, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "i2c0")),
	[RTD1295_ISO_I2C_SCL_1] = RTK_PIN_MUX(i2c_scl_1, 0x314, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "i2c1")),
	[RTD1295_ISO_I2C_SDA_1] = RTK_PIN_MUX(i2c_sda_1, 0x314, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "i2c1")),
	[RTD1295_ISO_I2C_SCL_6] = RTK_PIN_MUX(i2c_scl_6, 0x314, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "i2c6")),
	[RTD1295_ISO_GPIO_21] = RTK_PIN_MUX(iso_gpio_21, 0x31c, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "pwm")),
	[RTD1295_ISO_GPIO_22] = RTK_PIN_MUX(iso_gpio_22, 0x31c, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "pwm")),
	[RTD1295_ISO_GPIO_23] = RTK_PIN_MUX(iso_gpio_23, 0x31c, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "pwm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "uart2_1")),
	[RTD1295_ISO_GPIO_24] = RTK_PIN_MUX(iso_gpio_24, 0x31c, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "pwm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "uart2_1")),
	[RTD1295_ISO_GPIO_25] = RTK_PIN_MUX(iso_gpio_25, 0x31c, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "rtc")),
	[RTD1295_ISO_I2C_SDA_6] = RTK_PIN_MUX(i2c_sda_6, 0x314, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "i2c6")),
	[RTD1295_ISO_ETN_LED_LINK] = RTK_PIN_MUX(etn_led_link, 0x310, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "pwm")),
	[RTD1295_ISO_ETN_LED_RXTX] = RTK_PIN_MUX(etn_led_rxtx, 0x310, GENMASK(29, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "etn_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "pwm")),
	[RTD1295_ISO_NAT_LED_0] = RTK_PIN_MUX(nat_led_0, 0x314, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nat_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "pwm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "sc")),
	[RTD1295_ISO_NAT_LED_1] = RTK_PIN_MUX(nat_led_1, 0x314, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "nat_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "pwm"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "sc")),
	[RTD1295_ISO_NAT_LED_2] = RTK_PIN_MUX(nat_led_2, 0x314, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nat_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "sc")),
	[RTD1295_ISO_NAT_LED_3] = RTK_PIN_MUX(nat_led_3, 0x314, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "nat_led"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 22), "sc")),
	[RTD1295_ISO_GPIO_33] = RTK_PIN_MUX(iso_gpio_33, 0x31c, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "uart2_1")),
	[RTD1295_ISO_GPIO_34] = RTK_PIN_MUX(iso_gpio_34, 0x31c, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "uart2_1")),
};

/* pinconf is not implemented for the RTD1295 pad layout (mux only). */
static const struct rtd_pin_config_desc rtd1295_iso_configs[RTD1295_ISO_NUM] = { };

static const struct rtd_pinctrl_desc rtd1295_iso_pinctrl_desc = {
	.pins = rtd1295_iso_pins,
	.num_pins = ARRAY_SIZE(rtd1295_iso_pins),
	.groups = rtd1295_iso_groups,
	.num_groups = ARRAY_SIZE(rtd1295_iso_groups),
	.functions = rtd1295_iso_functions,
	.num_functions = ARRAY_SIZE(rtd1295_iso_functions),
	.muxes = rtd1295_iso_muxes,
	.num_muxes = ARRAY_SIZE(rtd1295_iso_muxes),
	.configs = rtd1295_iso_configs,
	.num_configs = ARRAY_SIZE(rtd1295_iso_configs),
};

static int rtd1295_pinctrl_probe(struct platform_device *pdev)
{
	return rtd_pinctrl_probe(pdev, &rtd1295_iso_pinctrl_desc);
}

static const struct of_device_id rtd1295_pinctrl_of_match[] = {
	{ .compatible = "realtek,rtd1295-iso-pinctrl" },
	{ }
};

static struct platform_driver rtd1295_pinctrl_driver = {
	.driver = {
		.name = "rtd1295-pinctrl",
		.of_match_table = rtd1295_pinctrl_of_match,
	},
	.probe = rtd1295_pinctrl_probe,
};

static int __init rtd1295_pinctrl_init(void)
{
	return platform_driver_register(&rtd1295_pinctrl_driver);
}
arch_initcall(rtd1295_pinctrl_init);

MODULE_DESCRIPTION("Realtek RTD1295 ISO pin controller driver");
MODULE_LICENSE("GPL");
