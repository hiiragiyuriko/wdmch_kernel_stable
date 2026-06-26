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


/* ===================== SB2 bank ===================== */
enum rtd1295_sb2_pins {
	RTD1295_SB2_GPIO_0 = 0,
	RTD1295_SB2_GPIO_1 = 1,
	RTD1295_SB2_GPIO_2 = 2,
	RTD1295_SB2_GPIO_3 = 3,
	RTD1295_SB2_GPIO_4 = 4,
	RTD1295_SB2_GPIO_5 = 5,
	RTD1295_SB2_GPIO_6 = 6,
	RTD1295_SB2_GPIO_7 = 7,
	RTD1295_SB2_GPIO_8 = 8,
	RTD1295_SB2_GPIO_9 = 9,
	RTD1295_SB2_TP1_SYNC = 10,
	RTD1295_SB2_I2C_SCL_4 = 11,
	RTD1295_SB2_I2C_SDA_4 = 12,
	RTD1295_SB2_I2C_SCL_5 = 13,
	RTD1295_SB2_I2C_SDA_5 = 14,
	RTD1295_SB2_USB_ID = 15,
	RTD1295_SB2_SENSOR_CKO_0 = 16,
	RTD1295_SB2_SENSOR_CKO_1 = 17,
	RTD1295_SB2_SENSOR_RST = 18,
	RTD1295_SB2_SENSOR_STB_0 = 19,
	RTD1295_SB2_SENSOR_STB_1 = 20,
	RTD1295_SB2_TP0_DATA = 21,
	RTD1295_SB2_TP0_CLK = 22,
	RTD1295_SB2_TP0_VALID = 23,
	RTD1295_SB2_TP0_SYNC = 24,
	RTD1295_SB2_TP1_DATA = 25,
	RTD1295_SB2_TP1_CLK = 26,
	RTD1295_SB2_TP1_VALID = 27,
	RTD1295_SB2_RGMII0_TXC = 28,
	RTD1295_SB2_RGMII0_TX_CTL = 29,
	RTD1295_SB2_RGMII0_TXD_0 = 30,
	RTD1295_SB2_RGMII0_TXD_1 = 31,
	RTD1295_SB2_RGMII0_TXD_2 = 32,
	RTD1295_SB2_RGMII0_TXD_3 = 33,
	RTD1295_SB2_RGMII0_RXC = 34,
	RTD1295_SB2_RGMII0_RX_CTL = 35,
	RTD1295_SB2_RGMII0_RXD_0 = 36,
	RTD1295_SB2_RGMII0_RXD_1 = 37,
	RTD1295_SB2_RGMII0_RXD_2 = 38,
	RTD1295_SB2_RGMII0_RXD_3 = 39,
	RTD1295_SB2_RGMII0_MDIO = 40,
	RTD1295_SB2_RGMII0_MDC = 41,
	RTD1295_SB2_RGMII1_TXC = 42,
	RTD1295_SB2_RGMII1_TX_CTL = 43,
	RTD1295_SB2_RGMII1_TXD_0 = 44,
	RTD1295_SB2_RGMII1_TXD_1 = 45,
	RTD1295_SB2_RGMII1_TXD_2 = 46,
	RTD1295_SB2_RGMII1_TXD_3 = 47,
	RTD1295_SB2_RGMII1_RXC = 48,
	RTD1295_SB2_RGMII1_RX_CTL = 49,
	RTD1295_SB2_RGMII1_RXD_0 = 50,
	RTD1295_SB2_RGMII1_RXD_1 = 51,
	RTD1295_SB2_RGMII1_RXD_2 = 52,
	RTD1295_SB2_RGMII1_RXD_3 = 53,
	RTD1295_SB2_HI_LOC = 54,
	RTD1295_SB2_EJTAG_SCPU_LOC = 55,
	RTD1295_SB2_SF_EN = 56,
	RTD1295_SB2_ARM_TRACE_DBG_EN = 57,
	RTD1295_SB2_DEBUG_P2S_ENABLE = 58,
	RTD1295_SB2_TP0_LOC = 59,
	RTD1295_SB2_TP1_LOC = 60,
	RTD1295_SB2_NUM
};

static const struct pinctrl_pin_desc rtd1295_sb2_pins[] = {
	PINCTRL_PIN(RTD1295_SB2_GPIO_0, "gpio_0"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_1, "gpio_1"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_2, "gpio_2"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_3, "gpio_3"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_4, "gpio_4"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_5, "gpio_5"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_6, "gpio_6"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_7, "gpio_7"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_8, "gpio_8"),
	PINCTRL_PIN(RTD1295_SB2_GPIO_9, "gpio_9"),
	PINCTRL_PIN(RTD1295_SB2_TP1_SYNC, "tp1_sync"),
	PINCTRL_PIN(RTD1295_SB2_I2C_SCL_4, "i2c_scl_4"),
	PINCTRL_PIN(RTD1295_SB2_I2C_SDA_4, "i2c_sda_4"),
	PINCTRL_PIN(RTD1295_SB2_I2C_SCL_5, "i2c_scl_5"),
	PINCTRL_PIN(RTD1295_SB2_I2C_SDA_5, "i2c_sda_5"),
	PINCTRL_PIN(RTD1295_SB2_USB_ID, "usb_id"),
	PINCTRL_PIN(RTD1295_SB2_SENSOR_CKO_0, "sensor_cko_0"),
	PINCTRL_PIN(RTD1295_SB2_SENSOR_CKO_1, "sensor_cko_1"),
	PINCTRL_PIN(RTD1295_SB2_SENSOR_RST, "sensor_rst"),
	PINCTRL_PIN(RTD1295_SB2_SENSOR_STB_0, "sensor_stb_0"),
	PINCTRL_PIN(RTD1295_SB2_SENSOR_STB_1, "sensor_stb_1"),
	PINCTRL_PIN(RTD1295_SB2_TP0_DATA, "tp0_data"),
	PINCTRL_PIN(RTD1295_SB2_TP0_CLK, "tp0_clk"),
	PINCTRL_PIN(RTD1295_SB2_TP0_VALID, "tp0_valid"),
	PINCTRL_PIN(RTD1295_SB2_TP0_SYNC, "tp0_sync"),
	PINCTRL_PIN(RTD1295_SB2_TP1_DATA, "tp1_data"),
	PINCTRL_PIN(RTD1295_SB2_TP1_CLK, "tp1_clk"),
	PINCTRL_PIN(RTD1295_SB2_TP1_VALID, "tp1_valid"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_TXC, "rgmii0_txc"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_TX_CTL, "rgmii0_tx_ctl"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_TXD_0, "rgmii0_txd_0"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_TXD_1, "rgmii0_txd_1"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_TXD_2, "rgmii0_txd_2"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_TXD_3, "rgmii0_txd_3"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_RXC, "rgmii0_rxc"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_RX_CTL, "rgmii0_rx_ctl"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_RXD_0, "rgmii0_rxd_0"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_RXD_1, "rgmii0_rxd_1"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_RXD_2, "rgmii0_rxd_2"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_RXD_3, "rgmii0_rxd_3"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_MDIO, "rgmii0_mdio"),
	PINCTRL_PIN(RTD1295_SB2_RGMII0_MDC, "rgmii0_mdc"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_TXC, "rgmii1_txc"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_TX_CTL, "rgmii1_tx_ctl"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_TXD_0, "rgmii1_txd_0"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_TXD_1, "rgmii1_txd_1"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_TXD_2, "rgmii1_txd_2"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_TXD_3, "rgmii1_txd_3"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_RXC, "rgmii1_rxc"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_RX_CTL, "rgmii1_rx_ctl"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_RXD_0, "rgmii1_rxd_0"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_RXD_1, "rgmii1_rxd_1"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_RXD_2, "rgmii1_rxd_2"),
	PINCTRL_PIN(RTD1295_SB2_RGMII1_RXD_3, "rgmii1_rxd_3"),
	PINCTRL_PIN(RTD1295_SB2_HI_LOC, "hi_loc"),
	PINCTRL_PIN(RTD1295_SB2_EJTAG_SCPU_LOC, "ejtag_scpu_loc"),
	PINCTRL_PIN(RTD1295_SB2_SF_EN, "sf_en"),
	PINCTRL_PIN(RTD1295_SB2_ARM_TRACE_DBG_EN, "arm_trace_dbg_en"),
	PINCTRL_PIN(RTD1295_SB2_DEBUG_P2S_ENABLE, "debug_p2s_enable"),
	PINCTRL_PIN(RTD1295_SB2_TP0_LOC, "tp0_loc"),
	PINCTRL_PIN(RTD1295_SB2_TP1_LOC, "tp1_loc"),
};

/* one group per pin */
static const unsigned int sb2_gpio_0_pins[] = { RTD1295_SB2_GPIO_0 };
static const unsigned int sb2_gpio_1_pins[] = { RTD1295_SB2_GPIO_1 };
static const unsigned int sb2_gpio_2_pins[] = { RTD1295_SB2_GPIO_2 };
static const unsigned int sb2_gpio_3_pins[] = { RTD1295_SB2_GPIO_3 };
static const unsigned int sb2_gpio_4_pins[] = { RTD1295_SB2_GPIO_4 };
static const unsigned int sb2_gpio_5_pins[] = { RTD1295_SB2_GPIO_5 };
static const unsigned int sb2_gpio_6_pins[] = { RTD1295_SB2_GPIO_6 };
static const unsigned int sb2_gpio_7_pins[] = { RTD1295_SB2_GPIO_7 };
static const unsigned int sb2_gpio_8_pins[] = { RTD1295_SB2_GPIO_8 };
static const unsigned int sb2_gpio_9_pins[] = { RTD1295_SB2_GPIO_9 };
static const unsigned int sb2_tp1_sync_pins[] = { RTD1295_SB2_TP1_SYNC };
static const unsigned int sb2_i2c_scl_4_pins[] = { RTD1295_SB2_I2C_SCL_4 };
static const unsigned int sb2_i2c_sda_4_pins[] = { RTD1295_SB2_I2C_SDA_4 };
static const unsigned int sb2_i2c_scl_5_pins[] = { RTD1295_SB2_I2C_SCL_5 };
static const unsigned int sb2_i2c_sda_5_pins[] = { RTD1295_SB2_I2C_SDA_5 };
static const unsigned int sb2_usb_id_pins[] = { RTD1295_SB2_USB_ID };
static const unsigned int sb2_sensor_cko_0_pins[] = { RTD1295_SB2_SENSOR_CKO_0 };
static const unsigned int sb2_sensor_cko_1_pins[] = { RTD1295_SB2_SENSOR_CKO_1 };
static const unsigned int sb2_sensor_rst_pins[] = { RTD1295_SB2_SENSOR_RST };
static const unsigned int sb2_sensor_stb_0_pins[] = { RTD1295_SB2_SENSOR_STB_0 };
static const unsigned int sb2_sensor_stb_1_pins[] = { RTD1295_SB2_SENSOR_STB_1 };
static const unsigned int sb2_tp0_data_pins[] = { RTD1295_SB2_TP0_DATA };
static const unsigned int sb2_tp0_clk_pins[] = { RTD1295_SB2_TP0_CLK };
static const unsigned int sb2_tp0_valid_pins[] = { RTD1295_SB2_TP0_VALID };
static const unsigned int sb2_tp0_sync_pins[] = { RTD1295_SB2_TP0_SYNC };
static const unsigned int sb2_tp1_data_pins[] = { RTD1295_SB2_TP1_DATA };
static const unsigned int sb2_tp1_clk_pins[] = { RTD1295_SB2_TP1_CLK };
static const unsigned int sb2_tp1_valid_pins[] = { RTD1295_SB2_TP1_VALID };
static const unsigned int sb2_rgmii0_txc_pins[] = { RTD1295_SB2_RGMII0_TXC };
static const unsigned int sb2_rgmii0_tx_ctl_pins[] = { RTD1295_SB2_RGMII0_TX_CTL };
static const unsigned int sb2_rgmii0_txd_0_pins[] = { RTD1295_SB2_RGMII0_TXD_0 };
static const unsigned int sb2_rgmii0_txd_1_pins[] = { RTD1295_SB2_RGMII0_TXD_1 };
static const unsigned int sb2_rgmii0_txd_2_pins[] = { RTD1295_SB2_RGMII0_TXD_2 };
static const unsigned int sb2_rgmii0_txd_3_pins[] = { RTD1295_SB2_RGMII0_TXD_3 };
static const unsigned int sb2_rgmii0_rxc_pins[] = { RTD1295_SB2_RGMII0_RXC };
static const unsigned int sb2_rgmii0_rx_ctl_pins[] = { RTD1295_SB2_RGMII0_RX_CTL };
static const unsigned int sb2_rgmii0_rxd_0_pins[] = { RTD1295_SB2_RGMII0_RXD_0 };
static const unsigned int sb2_rgmii0_rxd_1_pins[] = { RTD1295_SB2_RGMII0_RXD_1 };
static const unsigned int sb2_rgmii0_rxd_2_pins[] = { RTD1295_SB2_RGMII0_RXD_2 };
static const unsigned int sb2_rgmii0_rxd_3_pins[] = { RTD1295_SB2_RGMII0_RXD_3 };
static const unsigned int sb2_rgmii0_mdio_pins[] = { RTD1295_SB2_RGMII0_MDIO };
static const unsigned int sb2_rgmii0_mdc_pins[] = { RTD1295_SB2_RGMII0_MDC };
static const unsigned int sb2_rgmii1_txc_pins[] = { RTD1295_SB2_RGMII1_TXC };
static const unsigned int sb2_rgmii1_tx_ctl_pins[] = { RTD1295_SB2_RGMII1_TX_CTL };
static const unsigned int sb2_rgmii1_txd_0_pins[] = { RTD1295_SB2_RGMII1_TXD_0 };
static const unsigned int sb2_rgmii1_txd_1_pins[] = { RTD1295_SB2_RGMII1_TXD_1 };
static const unsigned int sb2_rgmii1_txd_2_pins[] = { RTD1295_SB2_RGMII1_TXD_2 };
static const unsigned int sb2_rgmii1_txd_3_pins[] = { RTD1295_SB2_RGMII1_TXD_3 };
static const unsigned int sb2_rgmii1_rxc_pins[] = { RTD1295_SB2_RGMII1_RXC };
static const unsigned int sb2_rgmii1_rx_ctl_pins[] = { RTD1295_SB2_RGMII1_RX_CTL };
static const unsigned int sb2_rgmii1_rxd_0_pins[] = { RTD1295_SB2_RGMII1_RXD_0 };
static const unsigned int sb2_rgmii1_rxd_1_pins[] = { RTD1295_SB2_RGMII1_RXD_1 };
static const unsigned int sb2_rgmii1_rxd_2_pins[] = { RTD1295_SB2_RGMII1_RXD_2 };
static const unsigned int sb2_rgmii1_rxd_3_pins[] = { RTD1295_SB2_RGMII1_RXD_3 };
static const unsigned int sb2_hi_loc_pins[] = { RTD1295_SB2_HI_LOC };
static const unsigned int sb2_ejtag_scpu_loc_pins[] = { RTD1295_SB2_EJTAG_SCPU_LOC };
static const unsigned int sb2_sf_en_pins[] = { RTD1295_SB2_SF_EN };
static const unsigned int sb2_arm_trace_dbg_en_pins[] = { RTD1295_SB2_ARM_TRACE_DBG_EN };
static const unsigned int sb2_debug_p2s_enable_pins[] = { RTD1295_SB2_DEBUG_P2S_ENABLE };
static const unsigned int sb2_tp0_loc_pins[] = { RTD1295_SB2_TP0_LOC };
static const unsigned int sb2_tp1_loc_pins[] = { RTD1295_SB2_TP1_LOC };

static const struct rtd_pin_group_desc rtd1295_sb2_groups[] = {
	{ .name = "gpio_0", .pins = sb2_gpio_0_pins, .num_pins = 1 },
	{ .name = "gpio_1", .pins = sb2_gpio_1_pins, .num_pins = 1 },
	{ .name = "gpio_2", .pins = sb2_gpio_2_pins, .num_pins = 1 },
	{ .name = "gpio_3", .pins = sb2_gpio_3_pins, .num_pins = 1 },
	{ .name = "gpio_4", .pins = sb2_gpio_4_pins, .num_pins = 1 },
	{ .name = "gpio_5", .pins = sb2_gpio_5_pins, .num_pins = 1 },
	{ .name = "gpio_6", .pins = sb2_gpio_6_pins, .num_pins = 1 },
	{ .name = "gpio_7", .pins = sb2_gpio_7_pins, .num_pins = 1 },
	{ .name = "gpio_8", .pins = sb2_gpio_8_pins, .num_pins = 1 },
	{ .name = "gpio_9", .pins = sb2_gpio_9_pins, .num_pins = 1 },
	{ .name = "tp1_sync", .pins = sb2_tp1_sync_pins, .num_pins = 1 },
	{ .name = "i2c_scl_4", .pins = sb2_i2c_scl_4_pins, .num_pins = 1 },
	{ .name = "i2c_sda_4", .pins = sb2_i2c_sda_4_pins, .num_pins = 1 },
	{ .name = "i2c_scl_5", .pins = sb2_i2c_scl_5_pins, .num_pins = 1 },
	{ .name = "i2c_sda_5", .pins = sb2_i2c_sda_5_pins, .num_pins = 1 },
	{ .name = "usb_id", .pins = sb2_usb_id_pins, .num_pins = 1 },
	{ .name = "sensor_cko_0", .pins = sb2_sensor_cko_0_pins, .num_pins = 1 },
	{ .name = "sensor_cko_1", .pins = sb2_sensor_cko_1_pins, .num_pins = 1 },
	{ .name = "sensor_rst", .pins = sb2_sensor_rst_pins, .num_pins = 1 },
	{ .name = "sensor_stb_0", .pins = sb2_sensor_stb_0_pins, .num_pins = 1 },
	{ .name = "sensor_stb_1", .pins = sb2_sensor_stb_1_pins, .num_pins = 1 },
	{ .name = "tp0_data", .pins = sb2_tp0_data_pins, .num_pins = 1 },
	{ .name = "tp0_clk", .pins = sb2_tp0_clk_pins, .num_pins = 1 },
	{ .name = "tp0_valid", .pins = sb2_tp0_valid_pins, .num_pins = 1 },
	{ .name = "tp0_sync", .pins = sb2_tp0_sync_pins, .num_pins = 1 },
	{ .name = "tp1_data", .pins = sb2_tp1_data_pins, .num_pins = 1 },
	{ .name = "tp1_clk", .pins = sb2_tp1_clk_pins, .num_pins = 1 },
	{ .name = "tp1_valid", .pins = sb2_tp1_valid_pins, .num_pins = 1 },
	{ .name = "rgmii0_txc", .pins = sb2_rgmii0_txc_pins, .num_pins = 1 },
	{ .name = "rgmii0_tx_ctl", .pins = sb2_rgmii0_tx_ctl_pins, .num_pins = 1 },
	{ .name = "rgmii0_txd_0", .pins = sb2_rgmii0_txd_0_pins, .num_pins = 1 },
	{ .name = "rgmii0_txd_1", .pins = sb2_rgmii0_txd_1_pins, .num_pins = 1 },
	{ .name = "rgmii0_txd_2", .pins = sb2_rgmii0_txd_2_pins, .num_pins = 1 },
	{ .name = "rgmii0_txd_3", .pins = sb2_rgmii0_txd_3_pins, .num_pins = 1 },
	{ .name = "rgmii0_rxc", .pins = sb2_rgmii0_rxc_pins, .num_pins = 1 },
	{ .name = "rgmii0_rx_ctl", .pins = sb2_rgmii0_rx_ctl_pins, .num_pins = 1 },
	{ .name = "rgmii0_rxd_0", .pins = sb2_rgmii0_rxd_0_pins, .num_pins = 1 },
	{ .name = "rgmii0_rxd_1", .pins = sb2_rgmii0_rxd_1_pins, .num_pins = 1 },
	{ .name = "rgmii0_rxd_2", .pins = sb2_rgmii0_rxd_2_pins, .num_pins = 1 },
	{ .name = "rgmii0_rxd_3", .pins = sb2_rgmii0_rxd_3_pins, .num_pins = 1 },
	{ .name = "rgmii0_mdio", .pins = sb2_rgmii0_mdio_pins, .num_pins = 1 },
	{ .name = "rgmii0_mdc", .pins = sb2_rgmii0_mdc_pins, .num_pins = 1 },
	{ .name = "rgmii1_txc", .pins = sb2_rgmii1_txc_pins, .num_pins = 1 },
	{ .name = "rgmii1_tx_ctl", .pins = sb2_rgmii1_tx_ctl_pins, .num_pins = 1 },
	{ .name = "rgmii1_txd_0", .pins = sb2_rgmii1_txd_0_pins, .num_pins = 1 },
	{ .name = "rgmii1_txd_1", .pins = sb2_rgmii1_txd_1_pins, .num_pins = 1 },
	{ .name = "rgmii1_txd_2", .pins = sb2_rgmii1_txd_2_pins, .num_pins = 1 },
	{ .name = "rgmii1_txd_3", .pins = sb2_rgmii1_txd_3_pins, .num_pins = 1 },
	{ .name = "rgmii1_rxc", .pins = sb2_rgmii1_rxc_pins, .num_pins = 1 },
	{ .name = "rgmii1_rx_ctl", .pins = sb2_rgmii1_rx_ctl_pins, .num_pins = 1 },
	{ .name = "rgmii1_rxd_0", .pins = sb2_rgmii1_rxd_0_pins, .num_pins = 1 },
	{ .name = "rgmii1_rxd_1", .pins = sb2_rgmii1_rxd_1_pins, .num_pins = 1 },
	{ .name = "rgmii1_rxd_2", .pins = sb2_rgmii1_rxd_2_pins, .num_pins = 1 },
	{ .name = "rgmii1_rxd_3", .pins = sb2_rgmii1_rxd_3_pins, .num_pins = 1 },
	{ .name = "hi_loc", .pins = sb2_hi_loc_pins, .num_pins = 1 },
	{ .name = "ejtag_scpu_loc", .pins = sb2_ejtag_scpu_loc_pins, .num_pins = 1 },
	{ .name = "sf_en", .pins = sb2_sf_en_pins, .num_pins = 1 },
	{ .name = "arm_trace_dbg_en", .pins = sb2_arm_trace_dbg_en_pins, .num_pins = 1 },
	{ .name = "debug_p2s_enable", .pins = sb2_debug_p2s_enable_pins, .num_pins = 1 },
	{ .name = "tp0_loc", .pins = sb2_tp0_loc_pins, .num_pins = 1 },
	{ .name = "tp1_loc", .pins = sb2_tp1_loc_pins, .num_pins = 1 },
};

static const char * const sb2_AI_grps[] = { "tp0_data", "tp0_clk", "tp0_valid", "tp0_sync" };
static const char * const sb2_arm_trace_disable_grps[] = { "arm_trace_dbg_en" };
static const char * const sb2_arm_trace_enable_grps[] = { "arm_trace_dbg_en" };
static const char * const sb2_dc_fan_sensor_grps[] = { "gpio_9" };
static const char * const sb2_eth_gpy_grps[] = { "rgmii0_mdio", "rgmii0_mdc" };
static const char * const sb2_gpio_grps[] = { "gpio_0", "gpio_1", "gpio_2", "gpio_3", "gpio_4", "gpio_5", "gpio_6", "gpio_7", "gpio_8", "gpio_9", "tp1_sync", "i2c_scl_4", "i2c_sda_4", "i2c_scl_5", "i2c_sda_5", "usb_id", "sensor_cko_0", "sensor_cko_1", "sensor_rst", "sensor_stb_0", "sensor_stb_1", "tp0_data", "tp0_clk", "tp0_valid", "tp0_sync", "tp1_data", "tp1_clk", "tp1_valid", "rgmii0_txc", "rgmii0_tx_ctl", "rgmii0_txd_0", "rgmii0_txd_1", "rgmii0_txd_2", "rgmii0_txd_3", "rgmii0_rxc", "rgmii0_rx_ctl", "rgmii0_rxd_0", "rgmii0_rxd_1", "rgmii0_rxd_2", "rgmii0_rxd_3", "rgmii0_mdio", "rgmii0_mdc", "rgmii1_txc", "rgmii1_tx_ctl", "rgmii1_txd_0", "rgmii1_txd_1", "rgmii1_txd_2", "rgmii1_txd_3", "rgmii1_rxc", "rgmii1_rx_ctl", "rgmii1_rxd_0", "rgmii1_rxd_1", "rgmii1_rxd_2", "rgmii1_rxd_3", "sf_en" };
static const char * const sb2_gspi_grps[] = { "gpio_4", "gpio_5", "gpio_6", "gpio_7" };
static const char * const sb2_hi_loc_misc_grps[] = { "hi_loc" };
static const char * const sb2_hi_loc_nf_grps[] = { "hi_loc" };
static const char * const sb2_i2c2_grps[] = { "tp1_sync", "tp1_clk" };
static const char * const sb2_i2c3_grps[] = { "tp1_data", "tp1_valid" };
static const char * const sb2_i2c4_grps[] = { "i2c_scl_4", "i2c_sda_4" };
static const char * const sb2_i2c5_grps[] = { "i2c_scl_5", "i2c_sda_5" };
static const char * const sb2_nand_grps[] = { "i2c_scl_5", "i2c_sda_5" };
static const char * const sb2_p2s_disable_grps[] = { "debug_p2s_enable" };
static const char * const sb2_p2s_enable_grps[] = { "debug_p2s_enable" };
static const char * const sb2_rgmii_grps[] = { "rgmii0_txc", "rgmii0_tx_ctl", "rgmii0_txd_0", "rgmii0_txd_1", "rgmii0_txd_2", "rgmii0_txd_3", "rgmii0_rxc", "rgmii0_rx_ctl", "rgmii0_rxd_0", "rgmii0_rxd_1", "rgmii0_rxd_2", "rgmii0_rxd_3", "rgmii0_mdio", "rgmii0_mdc", "rgmii1_txc", "rgmii1_tx_ctl", "rgmii1_txd_0", "rgmii1_txd_1", "rgmii1_txd_2", "rgmii1_txd_3", "rgmii1_rxc", "rgmii1_rx_ctl", "rgmii1_rxd_0", "rgmii1_rxd_1", "rgmii1_rxd_2", "rgmii1_rxd_3" };
static const char * const sb2_scpu_ejtag_loc_cr_grps[] = { "ejtag_scpu_loc" };
static const char * const sb2_scpu_ejtag_loc_gpio_grps[] = { "gpio_4", "gpio_5", "gpio_6", "gpio_7", "gpio_8", "ejtag_scpu_loc" };
static const char * const sb2_sensor_cko_output_grps[] = { "sensor_cko_0", "sensor_cko_1" };
static const char * const sb2_spi_grps[] = { "gpio_0", "gpio_1", "gpio_2", "gpio_3", "sf_en" };
static const char * const sb2_test_loop_dis_grps[] = { "usb_id" };
static const char * const sb2_tp0_loc_rgmii0_tx_grps[] = { "rgmii0_txd_0", "rgmii0_txd_1", "rgmii0_txd_2", "rgmii0_txd_3", "tp0_loc" };
static const char * const sb2_tp0_loc_tp0_grps[] = { "tp0_data", "tp0_clk", "tp0_valid", "tp0_sync", "tp0_loc" };
static const char * const sb2_tp0_loc_tp1_grps[] = { "tp0_data", "tp0_clk", "tp0_valid", "tp0_sync", "tp0_loc" };
static const char * const sb2_tp1_loc_rgmii0_rx_grps[] = { "rgmii0_rxd_0", "rgmii0_rxd_1", "rgmii0_rxd_2", "rgmii0_rxd_3", "tp1_loc" };
static const char * const sb2_tp1_loc_tp0_grps[] = { "tp1_sync", "tp1_data", "tp1_clk", "tp1_valid", "tp1_loc" };
static const char * const sb2_tp1_loc_tp1_grps[] = { "tp1_sync", "tp1_data", "tp1_clk", "tp1_valid", "tp1_loc" };
static const char * const sb2_usb_clock_output_grps[] = { "sensor_cko_1" };

static const struct rtd_pin_func_desc rtd1295_sb2_functions[] = {
	{ .name = "AI", .groups = sb2_AI_grps, .num_groups = ARRAY_SIZE(sb2_AI_grps) },
	{ .name = "arm_trace_disable", .groups = sb2_arm_trace_disable_grps, .num_groups = ARRAY_SIZE(sb2_arm_trace_disable_grps) },
	{ .name = "arm_trace_enable", .groups = sb2_arm_trace_enable_grps, .num_groups = ARRAY_SIZE(sb2_arm_trace_enable_grps) },
	{ .name = "dc_fan_sensor", .groups = sb2_dc_fan_sensor_grps, .num_groups = ARRAY_SIZE(sb2_dc_fan_sensor_grps) },
	{ .name = "eth_gpy", .groups = sb2_eth_gpy_grps, .num_groups = ARRAY_SIZE(sb2_eth_gpy_grps) },
	{ .name = "gpio", .groups = sb2_gpio_grps, .num_groups = ARRAY_SIZE(sb2_gpio_grps) },
	{ .name = "gspi", .groups = sb2_gspi_grps, .num_groups = ARRAY_SIZE(sb2_gspi_grps) },
	{ .name = "hi_loc_misc", .groups = sb2_hi_loc_misc_grps, .num_groups = ARRAY_SIZE(sb2_hi_loc_misc_grps) },
	{ .name = "hi_loc_nf", .groups = sb2_hi_loc_nf_grps, .num_groups = ARRAY_SIZE(sb2_hi_loc_nf_grps) },
	{ .name = "i2c2", .groups = sb2_i2c2_grps, .num_groups = ARRAY_SIZE(sb2_i2c2_grps) },
	{ .name = "i2c3", .groups = sb2_i2c3_grps, .num_groups = ARRAY_SIZE(sb2_i2c3_grps) },
	{ .name = "i2c4", .groups = sb2_i2c4_grps, .num_groups = ARRAY_SIZE(sb2_i2c4_grps) },
	{ .name = "i2c5", .groups = sb2_i2c5_grps, .num_groups = ARRAY_SIZE(sb2_i2c5_grps) },
	{ .name = "nand", .groups = sb2_nand_grps, .num_groups = ARRAY_SIZE(sb2_nand_grps) },
	{ .name = "p2s_disable", .groups = sb2_p2s_disable_grps, .num_groups = ARRAY_SIZE(sb2_p2s_disable_grps) },
	{ .name = "p2s_enable", .groups = sb2_p2s_enable_grps, .num_groups = ARRAY_SIZE(sb2_p2s_enable_grps) },
	{ .name = "rgmii", .groups = sb2_rgmii_grps, .num_groups = ARRAY_SIZE(sb2_rgmii_grps) },
	{ .name = "scpu_ejtag_loc_cr", .groups = sb2_scpu_ejtag_loc_cr_grps, .num_groups = ARRAY_SIZE(sb2_scpu_ejtag_loc_cr_grps) },
	{ .name = "scpu_ejtag_loc_gpio", .groups = sb2_scpu_ejtag_loc_gpio_grps, .num_groups = ARRAY_SIZE(sb2_scpu_ejtag_loc_gpio_grps) },
	{ .name = "sensor_cko_output", .groups = sb2_sensor_cko_output_grps, .num_groups = ARRAY_SIZE(sb2_sensor_cko_output_grps) },
	{ .name = "spi", .groups = sb2_spi_grps, .num_groups = ARRAY_SIZE(sb2_spi_grps) },
	{ .name = "test_loop_dis", .groups = sb2_test_loop_dis_grps, .num_groups = ARRAY_SIZE(sb2_test_loop_dis_grps) },
	{ .name = "tp0_loc_rgmii0_tx", .groups = sb2_tp0_loc_rgmii0_tx_grps, .num_groups = ARRAY_SIZE(sb2_tp0_loc_rgmii0_tx_grps) },
	{ .name = "tp0_loc_tp0", .groups = sb2_tp0_loc_tp0_grps, .num_groups = ARRAY_SIZE(sb2_tp0_loc_tp0_grps) },
	{ .name = "tp0_loc_tp1", .groups = sb2_tp0_loc_tp1_grps, .num_groups = ARRAY_SIZE(sb2_tp0_loc_tp1_grps) },
	{ .name = "tp1_loc_rgmii0_rx", .groups = sb2_tp1_loc_rgmii0_rx_grps, .num_groups = ARRAY_SIZE(sb2_tp1_loc_rgmii0_rx_grps) },
	{ .name = "tp1_loc_tp0", .groups = sb2_tp1_loc_tp0_grps, .num_groups = ARRAY_SIZE(sb2_tp1_loc_tp0_grps) },
	{ .name = "tp1_loc_tp1", .groups = sb2_tp1_loc_tp1_grps, .num_groups = ARRAY_SIZE(sb2_tp1_loc_tp1_grps) },
	{ .name = "usb_clock_output", .groups = sb2_usb_clock_output_grps, .num_groups = ARRAY_SIZE(sb2_usb_clock_output_grps) },
};

static const struct rtd_pin_desc rtd1295_sb2_muxes[RTD1295_SB2_NUM] = {
	[RTD1295_SB2_GPIO_0] = RTK_PIN_MUX(gpio_0, 0x910, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "spi")),
	[RTD1295_SB2_GPIO_1] = RTK_PIN_MUX(gpio_1, 0x910, GENMASK(5, 3),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 3), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 3), "spi")),
	[RTD1295_SB2_GPIO_2] = RTK_PIN_MUX(gpio_2, 0x910, GENMASK(8, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 6), "spi")),
	[RTD1295_SB2_GPIO_3] = RTK_PIN_MUX(gpio_3, 0x910, GENMASK(11, 9),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 9), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 9), "spi")),
	[RTD1295_SB2_GPIO_4] = RTK_PIN_MUX(gpio_4, 0x910, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "scpu_ejtag_loc_gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 12), "gspi")),
	[RTD1295_SB2_GPIO_5] = RTK_PIN_MUX(gpio_5, 0x910, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 14), "scpu_ejtag_loc_gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 14), "gspi")),
	[RTD1295_SB2_GPIO_6] = RTK_PIN_MUX(gpio_6, 0x910, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "scpu_ejtag_loc_gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "gspi")),
	[RTD1295_SB2_GPIO_7] = RTK_PIN_MUX(gpio_7, 0x910, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "scpu_ejtag_loc_gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "gspi")),
	[RTD1295_SB2_GPIO_8] = RTK_PIN_MUX(gpio_8, 0x910, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "scpu_ejtag_loc_gpio")),
	[RTD1295_SB2_GPIO_9] = RTK_PIN_MUX(gpio_9, 0x910, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 22), "dc_fan_sensor")),
	[RTD1295_SB2_TP1_SYNC] = RTK_PIN_MUX(tp1_sync, 0x908, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "tp1_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "tp1_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "i2c2")),
	[RTD1295_SB2_I2C_SCL_4] = RTK_PIN_MUX(i2c_scl_4, 0x90c, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "i2c4")),
	[RTD1295_SB2_I2C_SDA_4] = RTK_PIN_MUX(i2c_sda_4, 0x90c, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "i2c4")),
	[RTD1295_SB2_I2C_SCL_5] = RTK_PIN_MUX(i2c_scl_5, 0x90c, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 10), "nand")),
	[RTD1295_SB2_I2C_SDA_5] = RTK_PIN_MUX(i2c_sda_5, 0x90c, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "i2c5"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "nand")),
	[RTD1295_SB2_USB_ID] = RTK_PIN_MUX(usb_id, 0x90c, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "test_loop_dis")),
	[RTD1295_SB2_SENSOR_CKO_0] = RTK_PIN_MUX(sensor_cko_0, 0x90c, GENMASK(31, 30),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 30), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 30), "sensor_cko_output")),
	[RTD1295_SB2_SENSOR_CKO_1] = RTK_PIN_MUX(sensor_cko_1, 0x90c, GENMASK(29, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "sensor_cko_output"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "usb_clock_output")),
	[RTD1295_SB2_SENSOR_RST] = RTK_PIN_MUX(sensor_rst, 0x90c, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio")),
	[RTD1295_SB2_SENSOR_STB_0] = RTK_PIN_MUX(sensor_stb_0, 0x90c, GENMASK(25, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio")),
	[RTD1295_SB2_SENSOR_STB_1] = RTK_PIN_MUX(sensor_stb_1, 0x90c, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio")),
	[RTD1295_SB2_TP0_DATA] = RTK_PIN_MUX(tp0_data, 0x908, GENMASK(2, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "tp0_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "tp0_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 0), "AI")),
	[RTD1295_SB2_TP0_CLK] = RTK_PIN_MUX(tp0_clk, 0x908, GENMASK(11, 9),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 9), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 9), "tp0_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 9), "tp0_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 9), "AI")),
	[RTD1295_SB2_TP0_VALID] = RTK_PIN_MUX(tp0_valid, 0x908, GENMASK(8, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "tp0_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "tp0_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 6), "AI")),
	[RTD1295_SB2_TP0_SYNC] = RTK_PIN_MUX(tp0_sync, 0x908, GENMASK(5, 3),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 3), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 3), "tp0_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 3), "tp0_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 3), "AI")),
	[RTD1295_SB2_TP1_DATA] = RTK_PIN_MUX(tp1_data, 0x908, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "tp1_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "tp1_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "i2c3")),
	[RTD1295_SB2_TP1_CLK] = RTK_PIN_MUX(tp1_clk, 0x908, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "tp1_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 22), "tp1_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 22), "i2c2")),
	[RTD1295_SB2_TP1_VALID] = RTK_PIN_MUX(tp1_valid, 0x908, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "tp1_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "tp1_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "i2c3")),
	[RTD1295_SB2_RGMII0_TXC] = RTK_PIN_MUX(rgmii0_txc, 0x96c, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "rgmii")),
	[RTD1295_SB2_RGMII0_TX_CTL] = RTK_PIN_MUX(rgmii0_tx_ctl, 0x96c, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "rgmii")),
	[RTD1295_SB2_RGMII0_TXD_0] = RTK_PIN_MUX(rgmii0_txd_0, 0x96c, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "tp0_loc_rgmii0_tx")),
	[RTD1295_SB2_RGMII0_TXD_1] = RTK_PIN_MUX(rgmii0_txd_1, 0x96c, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "tp0_loc_rgmii0_tx")),
	[RTD1295_SB2_RGMII0_TXD_2] = RTK_PIN_MUX(rgmii0_txd_2, 0x96c, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "tp0_loc_rgmii0_tx")),
	[RTD1295_SB2_RGMII0_TXD_3] = RTK_PIN_MUX(rgmii0_txd_3, 0x96c, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "tp0_loc_rgmii0_tx")),
	[RTD1295_SB2_RGMII0_RXC] = RTK_PIN_MUX(rgmii0_rxc, 0x96c, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "rgmii")),
	[RTD1295_SB2_RGMII0_RX_CTL] = RTK_PIN_MUX(rgmii0_rx_ctl, 0x96c, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "rgmii")),
	[RTD1295_SB2_RGMII0_RXD_0] = RTK_PIN_MUX(rgmii0_rxd_0, 0x96c, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "tp1_loc_rgmii0_rx")),
	[RTD1295_SB2_RGMII0_RXD_1] = RTK_PIN_MUX(rgmii0_rxd_1, 0x96c, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "tp1_loc_rgmii0_rx")),
	[RTD1295_SB2_RGMII0_RXD_2] = RTK_PIN_MUX(rgmii0_rxd_2, 0x96c, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "tp1_loc_rgmii0_rx")),
	[RTD1295_SB2_RGMII0_RXD_3] = RTK_PIN_MUX(rgmii0_rxd_3, 0x96c, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 22), "tp1_loc_rgmii0_rx")),
	[RTD1295_SB2_RGMII0_MDIO] = RTK_PIN_MUX(rgmii0_mdio, 0x96c, GENMASK(25, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "eth_gpy")),
	[RTD1295_SB2_RGMII0_MDC] = RTK_PIN_MUX(rgmii0_mdc, 0x96c, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "rgmii"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "eth_gpy")),
	[RTD1295_SB2_RGMII1_TXC] = RTK_PIN_MUX(rgmii1_txc, 0x97c, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "rgmii")),
	[RTD1295_SB2_RGMII1_TX_CTL] = RTK_PIN_MUX(rgmii1_tx_ctl, 0x97c, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "rgmii")),
	[RTD1295_SB2_RGMII1_TXD_0] = RTK_PIN_MUX(rgmii1_txd_0, 0x97c, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "rgmii")),
	[RTD1295_SB2_RGMII1_TXD_1] = RTK_PIN_MUX(rgmii1_txd_1, 0x97c, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "rgmii")),
	[RTD1295_SB2_RGMII1_TXD_2] = RTK_PIN_MUX(rgmii1_txd_2, 0x97c, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "rgmii")),
	[RTD1295_SB2_RGMII1_TXD_3] = RTK_PIN_MUX(rgmii1_txd_3, 0x97c, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "rgmii")),
	[RTD1295_SB2_RGMII1_RXC] = RTK_PIN_MUX(rgmii1_rxc, 0x97c, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "rgmii")),
	[RTD1295_SB2_RGMII1_RX_CTL] = RTK_PIN_MUX(rgmii1_rx_ctl, 0x97c, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "rgmii")),
	[RTD1295_SB2_RGMII1_RXD_0] = RTK_PIN_MUX(rgmii1_rxd_0, 0x97c, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "rgmii")),
	[RTD1295_SB2_RGMII1_RXD_1] = RTK_PIN_MUX(rgmii1_rxd_1, 0x97c, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "rgmii")),
	[RTD1295_SB2_RGMII1_RXD_2] = RTK_PIN_MUX(rgmii1_rxd_2, 0x97c, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "rgmii")),
	[RTD1295_SB2_RGMII1_RXD_3] = RTK_PIN_MUX(rgmii1_rxd_3, 0x97c, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "rgmii")),
	[RTD1295_SB2_HI_LOC] = RTK_PIN_MUX(hi_loc, 0x90c, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "hi_loc_misc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "hi_loc_nf")),
	[RTD1295_SB2_EJTAG_SCPU_LOC] = RTK_PIN_MUX(ejtag_scpu_loc, 0x90c, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "scpu_ejtag_loc_gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "scpu_ejtag_loc_cr")),
	[RTD1295_SB2_SF_EN] = RTK_PIN_MUX(sf_en, 0x914, GENMASK(0, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "spi")),
	[RTD1295_SB2_ARM_TRACE_DBG_EN] = RTK_PIN_MUX(arm_trace_dbg_en, 0x914, GENMASK(1, 1),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 1), "arm_trace_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 1), "arm_trace_enable")),
	[RTD1295_SB2_DEBUG_P2S_ENABLE] = RTK_PIN_MUX(debug_p2s_enable, 0x914, GENMASK(6, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "p2s_disable"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "p2s_enable")),
	[RTD1295_SB2_TP0_LOC] = RTK_PIN_MUX(tp0_loc, 0x914, GENMASK(7, 7),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 7), "tp0_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 7), "tp0_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 7), "tp0_loc_rgmii0_tx")),
	[RTD1295_SB2_TP1_LOC] = RTK_PIN_MUX(tp1_loc, 0x914, GENMASK(8, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "tp1_loc_tp1"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "tp1_loc_tp0"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "tp1_loc_rgmii0_rx")),
};

static const struct rtd_pin_config_desc rtd1295_sb2_configs[RTD1295_SB2_NUM] = { };

static const struct rtd_pinctrl_desc rtd1295_sb2_pinctrl_desc = {
	.pins = rtd1295_sb2_pins,
	.num_pins = ARRAY_SIZE(rtd1295_sb2_pins),
	.groups = rtd1295_sb2_groups,
	.num_groups = ARRAY_SIZE(rtd1295_sb2_groups),
	.functions = rtd1295_sb2_functions,
	.num_functions = ARRAY_SIZE(rtd1295_sb2_functions),
	.muxes = rtd1295_sb2_muxes,
	.num_muxes = ARRAY_SIZE(rtd1295_sb2_muxes),
	.configs = rtd1295_sb2_configs,
	.num_configs = ARRAY_SIZE(rtd1295_sb2_configs),
};

/* ===================== CR bank ===================== */
enum rtd1295_cr_pins {
	RTD1295_CR_NF_CLE = 0,
	RTD1295_CR_NF_ALE = 1,
	RTD1295_CR_NF_RD_N = 2,
	RTD1295_CR_NF_WR_N = 3,
	RTD1295_CR_NF_RDY = 4,
	RTD1295_CR_NF_DD_7 = 5,
	RTD1295_CR_NF_DD_6 = 6,
	RTD1295_CR_NF_DD_5 = 7,
	RTD1295_CR_NF_DD_4 = 8,
	RTD1295_CR_NF_DD_3 = 9,
	RTD1295_CR_NF_DD_2 = 10,
	RTD1295_CR_NF_DD_1 = 11,
	RTD1295_CR_NF_DD_0 = 12,
	RTD1295_CR_NF_DQS = 13,
	RTD1295_CR_NF_CE_N_0 = 14,
	RTD1295_CR_NF_CE_N_1 = 15,
	RTD1295_CR_EMMC_DD_SB = 16,
	RTD1295_CR_MMC_CMD = 17,
	RTD1295_CR_MMC_CLK = 18,
	RTD1295_CR_MMC_WP = 19,
	RTD1295_CR_MMC_CD = 20,
	RTD1295_CR_MMC_DATA_0 = 21,
	RTD1295_CR_MMC_DATA_1 = 22,
	RTD1295_CR_MMC_DATA_2 = 23,
	RTD1295_CR_MMC_DATA_3 = 24,
	RTD1295_CR_SDIO_CMD = 25,
	RTD1295_CR_SDIO_CLK = 26,
	RTD1295_CR_SDIO_DATA_0 = 27,
	RTD1295_CR_SDIO_DATA_1 = 28,
	RTD1295_CR_SDIO_DATA_2 = 29,
	RTD1295_CR_SDIO_DATA_3 = 30,
	RTD1295_CR_PCIE_CLKREQ_0 = 31,
	RTD1295_CR_PCIE_CLKREQ_1 = 32,
	RTD1295_CR_PROB_0 = 33,
	RTD1295_CR_PROB_1 = 34,
	RTD1295_CR_PROB_2 = 35,
	RTD1295_CR_PROB_3 = 36,
	RTD1295_CR_SDIO_LOC = 37,
	RTD1295_CR_NUM
};

static const struct pinctrl_pin_desc rtd1295_cr_pins[] = {
	PINCTRL_PIN(RTD1295_CR_NF_CLE, "nf_cle"),
	PINCTRL_PIN(RTD1295_CR_NF_ALE, "nf_ale"),
	PINCTRL_PIN(RTD1295_CR_NF_RD_N, "nf_rd_n"),
	PINCTRL_PIN(RTD1295_CR_NF_WR_N, "nf_wr_n"),
	PINCTRL_PIN(RTD1295_CR_NF_RDY, "nf_rdy"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_7, "nf_dd_7"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_6, "nf_dd_6"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_5, "nf_dd_5"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_4, "nf_dd_4"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_3, "nf_dd_3"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_2, "nf_dd_2"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_1, "nf_dd_1"),
	PINCTRL_PIN(RTD1295_CR_NF_DD_0, "nf_dd_0"),
	PINCTRL_PIN(RTD1295_CR_NF_DQS, "nf_dqs"),
	PINCTRL_PIN(RTD1295_CR_NF_CE_N_0, "nf_ce_n_0"),
	PINCTRL_PIN(RTD1295_CR_NF_CE_N_1, "nf_ce_n_1"),
	PINCTRL_PIN(RTD1295_CR_EMMC_DD_SB, "emmc_dd_sb"),
	PINCTRL_PIN(RTD1295_CR_MMC_CMD, "mmc_cmd"),
	PINCTRL_PIN(RTD1295_CR_MMC_CLK, "mmc_clk"),
	PINCTRL_PIN(RTD1295_CR_MMC_WP, "mmc_wp"),
	PINCTRL_PIN(RTD1295_CR_MMC_CD, "mmc_cd"),
	PINCTRL_PIN(RTD1295_CR_MMC_DATA_0, "mmc_data_0"),
	PINCTRL_PIN(RTD1295_CR_MMC_DATA_1, "mmc_data_1"),
	PINCTRL_PIN(RTD1295_CR_MMC_DATA_2, "mmc_data_2"),
	PINCTRL_PIN(RTD1295_CR_MMC_DATA_3, "mmc_data_3"),
	PINCTRL_PIN(RTD1295_CR_SDIO_CMD, "sdio_cmd"),
	PINCTRL_PIN(RTD1295_CR_SDIO_CLK, "sdio_clk"),
	PINCTRL_PIN(RTD1295_CR_SDIO_DATA_0, "sdio_data_0"),
	PINCTRL_PIN(RTD1295_CR_SDIO_DATA_1, "sdio_data_1"),
	PINCTRL_PIN(RTD1295_CR_SDIO_DATA_2, "sdio_data_2"),
	PINCTRL_PIN(RTD1295_CR_SDIO_DATA_3, "sdio_data_3"),
	PINCTRL_PIN(RTD1295_CR_PCIE_CLKREQ_0, "pcie_clkreq_0"),
	PINCTRL_PIN(RTD1295_CR_PCIE_CLKREQ_1, "pcie_clkreq_1"),
	PINCTRL_PIN(RTD1295_CR_PROB_0, "prob_0"),
	PINCTRL_PIN(RTD1295_CR_PROB_1, "prob_1"),
	PINCTRL_PIN(RTD1295_CR_PROB_2, "prob_2"),
	PINCTRL_PIN(RTD1295_CR_PROB_3, "prob_3"),
	PINCTRL_PIN(RTD1295_CR_SDIO_LOC, "sdio_loc"),
};

/* one group per pin */
static const unsigned int cr_nf_cle_pins[] = { RTD1295_CR_NF_CLE };
static const unsigned int cr_nf_ale_pins[] = { RTD1295_CR_NF_ALE };
static const unsigned int cr_nf_rd_n_pins[] = { RTD1295_CR_NF_RD_N };
static const unsigned int cr_nf_wr_n_pins[] = { RTD1295_CR_NF_WR_N };
static const unsigned int cr_nf_rdy_pins[] = { RTD1295_CR_NF_RDY };
static const unsigned int cr_nf_dd_7_pins[] = { RTD1295_CR_NF_DD_7 };
static const unsigned int cr_nf_dd_6_pins[] = { RTD1295_CR_NF_DD_6 };
static const unsigned int cr_nf_dd_5_pins[] = { RTD1295_CR_NF_DD_5 };
static const unsigned int cr_nf_dd_4_pins[] = { RTD1295_CR_NF_DD_4 };
static const unsigned int cr_nf_dd_3_pins[] = { RTD1295_CR_NF_DD_3 };
static const unsigned int cr_nf_dd_2_pins[] = { RTD1295_CR_NF_DD_2 };
static const unsigned int cr_nf_dd_1_pins[] = { RTD1295_CR_NF_DD_1 };
static const unsigned int cr_nf_dd_0_pins[] = { RTD1295_CR_NF_DD_0 };
static const unsigned int cr_nf_dqs_pins[] = { RTD1295_CR_NF_DQS };
static const unsigned int cr_nf_ce_n_0_pins[] = { RTD1295_CR_NF_CE_N_0 };
static const unsigned int cr_nf_ce_n_1_pins[] = { RTD1295_CR_NF_CE_N_1 };
static const unsigned int cr_emmc_dd_sb_pins[] = { RTD1295_CR_EMMC_DD_SB };
static const unsigned int cr_mmc_cmd_pins[] = { RTD1295_CR_MMC_CMD };
static const unsigned int cr_mmc_clk_pins[] = { RTD1295_CR_MMC_CLK };
static const unsigned int cr_mmc_wp_pins[] = { RTD1295_CR_MMC_WP };
static const unsigned int cr_mmc_cd_pins[] = { RTD1295_CR_MMC_CD };
static const unsigned int cr_mmc_data_0_pins[] = { RTD1295_CR_MMC_DATA_0 };
static const unsigned int cr_mmc_data_1_pins[] = { RTD1295_CR_MMC_DATA_1 };
static const unsigned int cr_mmc_data_2_pins[] = { RTD1295_CR_MMC_DATA_2 };
static const unsigned int cr_mmc_data_3_pins[] = { RTD1295_CR_MMC_DATA_3 };
static const unsigned int cr_sdio_cmd_pins[] = { RTD1295_CR_SDIO_CMD };
static const unsigned int cr_sdio_clk_pins[] = { RTD1295_CR_SDIO_CLK };
static const unsigned int cr_sdio_data_0_pins[] = { RTD1295_CR_SDIO_DATA_0 };
static const unsigned int cr_sdio_data_1_pins[] = { RTD1295_CR_SDIO_DATA_1 };
static const unsigned int cr_sdio_data_2_pins[] = { RTD1295_CR_SDIO_DATA_2 };
static const unsigned int cr_sdio_data_3_pins[] = { RTD1295_CR_SDIO_DATA_3 };
static const unsigned int cr_pcie_clkreq_0_pins[] = { RTD1295_CR_PCIE_CLKREQ_0 };
static const unsigned int cr_pcie_clkreq_1_pins[] = { RTD1295_CR_PCIE_CLKREQ_1 };
static const unsigned int cr_prob_0_pins[] = { RTD1295_CR_PROB_0 };
static const unsigned int cr_prob_1_pins[] = { RTD1295_CR_PROB_1 };
static const unsigned int cr_prob_2_pins[] = { RTD1295_CR_PROB_2 };
static const unsigned int cr_prob_3_pins[] = { RTD1295_CR_PROB_3 };
static const unsigned int cr_sdio_loc_pins[] = { RTD1295_CR_SDIO_LOC };

static const struct rtd_pin_group_desc rtd1295_cr_groups[] = {
	{ .name = "nf_cle", .pins = cr_nf_cle_pins, .num_pins = 1 },
	{ .name = "nf_ale", .pins = cr_nf_ale_pins, .num_pins = 1 },
	{ .name = "nf_rd_n", .pins = cr_nf_rd_n_pins, .num_pins = 1 },
	{ .name = "nf_wr_n", .pins = cr_nf_wr_n_pins, .num_pins = 1 },
	{ .name = "nf_rdy", .pins = cr_nf_rdy_pins, .num_pins = 1 },
	{ .name = "nf_dd_7", .pins = cr_nf_dd_7_pins, .num_pins = 1 },
	{ .name = "nf_dd_6", .pins = cr_nf_dd_6_pins, .num_pins = 1 },
	{ .name = "nf_dd_5", .pins = cr_nf_dd_5_pins, .num_pins = 1 },
	{ .name = "nf_dd_4", .pins = cr_nf_dd_4_pins, .num_pins = 1 },
	{ .name = "nf_dd_3", .pins = cr_nf_dd_3_pins, .num_pins = 1 },
	{ .name = "nf_dd_2", .pins = cr_nf_dd_2_pins, .num_pins = 1 },
	{ .name = "nf_dd_1", .pins = cr_nf_dd_1_pins, .num_pins = 1 },
	{ .name = "nf_dd_0", .pins = cr_nf_dd_0_pins, .num_pins = 1 },
	{ .name = "nf_dqs", .pins = cr_nf_dqs_pins, .num_pins = 1 },
	{ .name = "nf_ce_n_0", .pins = cr_nf_ce_n_0_pins, .num_pins = 1 },
	{ .name = "nf_ce_n_1", .pins = cr_nf_ce_n_1_pins, .num_pins = 1 },
	{ .name = "emmc_dd_sb", .pins = cr_emmc_dd_sb_pins, .num_pins = 1 },
	{ .name = "mmc_cmd", .pins = cr_mmc_cmd_pins, .num_pins = 1 },
	{ .name = "mmc_clk", .pins = cr_mmc_clk_pins, .num_pins = 1 },
	{ .name = "mmc_wp", .pins = cr_mmc_wp_pins, .num_pins = 1 },
	{ .name = "mmc_cd", .pins = cr_mmc_cd_pins, .num_pins = 1 },
	{ .name = "mmc_data_0", .pins = cr_mmc_data_0_pins, .num_pins = 1 },
	{ .name = "mmc_data_1", .pins = cr_mmc_data_1_pins, .num_pins = 1 },
	{ .name = "mmc_data_2", .pins = cr_mmc_data_2_pins, .num_pins = 1 },
	{ .name = "mmc_data_3", .pins = cr_mmc_data_3_pins, .num_pins = 1 },
	{ .name = "sdio_cmd", .pins = cr_sdio_cmd_pins, .num_pins = 1 },
	{ .name = "sdio_clk", .pins = cr_sdio_clk_pins, .num_pins = 1 },
	{ .name = "sdio_data_0", .pins = cr_sdio_data_0_pins, .num_pins = 1 },
	{ .name = "sdio_data_1", .pins = cr_sdio_data_1_pins, .num_pins = 1 },
	{ .name = "sdio_data_2", .pins = cr_sdio_data_2_pins, .num_pins = 1 },
	{ .name = "sdio_data_3", .pins = cr_sdio_data_3_pins, .num_pins = 1 },
	{ .name = "pcie_clkreq_0", .pins = cr_pcie_clkreq_0_pins, .num_pins = 1 },
	{ .name = "pcie_clkreq_1", .pins = cr_pcie_clkreq_1_pins, .num_pins = 1 },
	{ .name = "prob_0", .pins = cr_prob_0_pins, .num_pins = 1 },
	{ .name = "prob_1", .pins = cr_prob_1_pins, .num_pins = 1 },
	{ .name = "prob_2", .pins = cr_prob_2_pins, .num_pins = 1 },
	{ .name = "prob_3", .pins = cr_prob_3_pins, .num_pins = 1 },
	{ .name = "sdio_loc", .pins = cr_sdio_loc_pins, .num_pins = 1 },
};

static const char * const cr_avcpu_ej_grps[] = { "nf_rd_n", "nf_rdy", "nf_dd_7", "nf_dd_6", "nf_dd_5" };
static const char * const cr_emmc_grps[] = { "nf_cle", "nf_rd_n", "nf_rdy", "nf_dd_7", "nf_dd_6", "nf_dd_5", "nf_dd_4", "nf_dd_3", "nf_dd_2", "nf_dd_1", "nf_dd_0", "emmc_dd_sb" };
static const char * const cr_gpio_grps[] = { "nf_cle", "nf_ale", "nf_rd_n", "nf_wr_n", "nf_rdy", "nf_dd_7", "nf_dd_6", "nf_dd_5", "nf_dd_4", "nf_dd_3", "nf_dd_2", "nf_dd_1", "nf_dd_0", "nf_dqs", "nf_ce_n_0", "nf_ce_n_1", "emmc_dd_sb", "mmc_cmd", "mmc_clk", "mmc_wp", "mmc_cd", "mmc_data_0", "mmc_data_1", "mmc_data_2", "mmc_data_3", "sdio_cmd", "sdio_clk", "sdio_data_0", "sdio_data_1", "sdio_data_2", "sdio_data_3", "pcie_clkreq_0", "pcie_clkreq_1", "prob_0", "prob_1", "prob_2", "prob_3" };
static const char * const cr_hif_grps[] = { "nf_cle", "nf_ale", "nf_wr_n", "nf_dd_4" };
static const char * const cr_nand_grps[] = { "nf_cle", "nf_ale", "nf_rd_n", "nf_wr_n", "nf_rdy", "nf_dd_7", "nf_dd_6", "nf_dd_5", "nf_dd_4", "nf_dd_3", "nf_dd_2", "nf_dd_1", "nf_dd_0", "nf_dqs", "nf_ce_n_0", "nf_ce_n_1" };
static const char * const cr_p2s_grps[] = { "prob_0", "prob_1" };
static const char * const cr_pcie_grps[] = { "pcie_clkreq_0", "pcie_clkreq_1" };
static const char * const cr_pll_test_grps[] = { "prob_0", "prob_1", "prob_2", "prob_3" };
static const char * const cr_scpu_ejtag_loc_cr_grps[] = { "mmc_cmd", "mmc_clk", "mmc_wp", "mmc_data_0", "mmc_data_3" };
static const char * const cr_sd_card_grps[] = { "mmc_cmd", "mmc_clk", "mmc_wp", "mmc_cd", "mmc_data_0", "mmc_data_1", "mmc_data_2", "mmc_data_3", "sdio_clk" };
static const char * const cr_sdio_grps[] = { "mmc_cmd", "mmc_clk", "mmc_data_0", "mmc_data_1", "mmc_data_2", "mmc_data_3", "sdio_cmd", "sdio_data_0", "sdio_data_1", "sdio_data_2", "sdio_data_3" };
static const char * const cr_sdio_loc_mmc_grps[] = { "sdio_loc" };
static const char * const cr_sdio_loc_sdio_grps[] = { "sdio_loc" };

static const struct rtd_pin_func_desc rtd1295_cr_functions[] = {
	{ .name = "avcpu_ej", .groups = cr_avcpu_ej_grps, .num_groups = ARRAY_SIZE(cr_avcpu_ej_grps) },
	{ .name = "emmc", .groups = cr_emmc_grps, .num_groups = ARRAY_SIZE(cr_emmc_grps) },
	{ .name = "gpio", .groups = cr_gpio_grps, .num_groups = ARRAY_SIZE(cr_gpio_grps) },
	{ .name = "hif", .groups = cr_hif_grps, .num_groups = ARRAY_SIZE(cr_hif_grps) },
	{ .name = "nand", .groups = cr_nand_grps, .num_groups = ARRAY_SIZE(cr_nand_grps) },
	{ .name = "p2s", .groups = cr_p2s_grps, .num_groups = ARRAY_SIZE(cr_p2s_grps) },
	{ .name = "pcie", .groups = cr_pcie_grps, .num_groups = ARRAY_SIZE(cr_pcie_grps) },
	{ .name = "pll_test", .groups = cr_pll_test_grps, .num_groups = ARRAY_SIZE(cr_pll_test_grps) },
	{ .name = "scpu_ejtag_loc_cr", .groups = cr_scpu_ejtag_loc_cr_grps, .num_groups = ARRAY_SIZE(cr_scpu_ejtag_loc_cr_grps) },
	{ .name = "sd_card", .groups = cr_sd_card_grps, .num_groups = ARRAY_SIZE(cr_sd_card_grps) },
	{ .name = "sdio", .groups = cr_sdio_grps, .num_groups = ARRAY_SIZE(cr_sdio_grps) },
	{ .name = "sdio_loc_mmc", .groups = cr_sdio_loc_mmc_grps, .num_groups = ARRAY_SIZE(cr_sdio_loc_mmc_grps) },
	{ .name = "sdio_loc_sdio", .groups = cr_sdio_loc_sdio_grps, .num_groups = ARRAY_SIZE(cr_sdio_loc_sdio_grps) },
};

static const struct rtd_pin_desc rtd1295_cr_muxes[RTD1295_CR_NUM] = {
	[RTD1295_CR_NF_CLE] = RTK_PIN_MUX(nf_cle, 0x600, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 10), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 10), "hif")),
	[RTD1295_CR_NF_ALE] = RTK_PIN_MUX(nf_ale, 0x600, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 8), "hif")),
	[RTD1295_CR_NF_RD_N] = RTK_PIN_MUX(nf_rd_n, 0x600, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 4), "avcpu_ej")),
	[RTD1295_CR_NF_WR_N] = RTK_PIN_MUX(nf_wr_n, 0x600, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 6), "hif")),
	[RTD1295_CR_NF_RDY] = RTK_PIN_MUX(nf_rdy, 0x600, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 2), "avcpu_ej")),
	[RTD1295_CR_NF_DD_7] = RTK_PIN_MUX(nf_dd_7, 0x600, GENMASK(31, 30),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 30), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 30), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 30), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 30), "avcpu_ej")),
	[RTD1295_CR_NF_DD_6] = RTK_PIN_MUX(nf_dd_6, 0x600, GENMASK(29, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 28), "avcpu_ej")),
	[RTD1295_CR_NF_DD_5] = RTK_PIN_MUX(nf_dd_5, 0x600, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 26), "avcpu_ej")),
	[RTD1295_CR_NF_DD_4] = RTK_PIN_MUX(nf_dd_4, 0x600, GENMASK(25, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "emmc"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "hif")),
	[RTD1295_CR_NF_DD_3] = RTK_PIN_MUX(nf_dd_3, 0x600, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 22), "emmc")),
	[RTD1295_CR_NF_DD_2] = RTK_PIN_MUX(nf_dd_2, 0x600, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "emmc")),
	[RTD1295_CR_NF_DD_1] = RTK_PIN_MUX(nf_dd_1, 0x600, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "emmc")),
	[RTD1295_CR_NF_DD_0] = RTK_PIN_MUX(nf_dd_0, 0x600, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "nand"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "emmc")),
	[RTD1295_CR_NF_DQS] = RTK_PIN_MUX(nf_dqs, 0x600, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "nand")),
	[RTD1295_CR_NF_CE_N_0] = RTK_PIN_MUX(nf_ce_n_0, 0x600, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "nand")),
	[RTD1295_CR_NF_CE_N_1] = RTK_PIN_MUX(nf_ce_n_1, 0x600, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "nand")),
	[RTD1295_CR_EMMC_DD_SB] = RTK_PIN_MUX(emmc_dd_sb, 0x604, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 12), "emmc")),
	[RTD1295_CR_MMC_CMD] = RTK_PIN_MUX(mmc_cmd, 0x604, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 16), "scpu_ejtag_loc_cr")),
	[RTD1295_CR_MMC_CLK] = RTK_PIN_MUX(mmc_clk, 0x604, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 18), "scpu_ejtag_loc_cr")),
	[RTD1295_CR_MMC_WP] = RTK_PIN_MUX(mmc_wp, 0x604, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 20), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 20), "scpu_ejtag_loc_cr")),
	[RTD1295_CR_MMC_CD] = RTK_PIN_MUX(mmc_cd, 0x604, GENMASK(23, 22),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 22), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 22), "sd_card")),
	[RTD1295_CR_MMC_DATA_0] = RTK_PIN_MUX(mmc_data_0, 0x604, GENMASK(25, 24),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 24), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 24), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 24), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 24), "scpu_ejtag_loc_cr")),
	[RTD1295_CR_MMC_DATA_1] = RTK_PIN_MUX(mmc_data_1, 0x604, GENMASK(27, 26),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 26), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 26), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 26), "sdio")),
	[RTD1295_CR_MMC_DATA_2] = RTK_PIN_MUX(mmc_data_2, 0x604, GENMASK(29, 28),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 28), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 28), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 28), "sdio")),
	[RTD1295_CR_MMC_DATA_3] = RTK_PIN_MUX(mmc_data_3, 0x604, GENMASK(31, 30),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 30), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 30), "sd_card"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 30), "sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x3, 30), "scpu_ejtag_loc_cr")),
	[RTD1295_CR_SDIO_CMD] = RTK_PIN_MUX(sdio_cmd, 0x604, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "sdio")),
	[RTD1295_CR_SDIO_CLK] = RTK_PIN_MUX(sdio_clk, 0x604, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "sd_card")),
	[RTD1295_CR_SDIO_DATA_0] = RTK_PIN_MUX(sdio_data_0, 0x604, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "sdio")),
	[RTD1295_CR_SDIO_DATA_1] = RTK_PIN_MUX(sdio_data_1, 0x604, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "sdio")),
	[RTD1295_CR_SDIO_DATA_2] = RTK_PIN_MUX(sdio_data_2, 0x604, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "sdio")),
	[RTD1295_CR_SDIO_DATA_3] = RTK_PIN_MUX(sdio_data_3, 0x604, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "sdio")),
	[RTD1295_CR_PCIE_CLKREQ_0] = RTK_PIN_MUX(pcie_clkreq_0, 0x61c, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "pcie")),
	[RTD1295_CR_PCIE_CLKREQ_1] = RTK_PIN_MUX(pcie_clkreq_1, 0x61c, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "pcie")),
	[RTD1295_CR_PROB_0] = RTK_PIN_MUX(prob_0, 0x61c, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "pll_test"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 6), "p2s")),
	[RTD1295_CR_PROB_1] = RTK_PIN_MUX(prob_1, 0x61c, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "pll_test"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 8), "p2s")),
	[RTD1295_CR_PROB_2] = RTK_PIN_MUX(prob_2, 0x61c, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "pll_test")),
	[RTD1295_CR_PROB_3] = RTK_PIN_MUX(prob_3, 0x61c, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "pll_test")),
	[RTD1295_CR_SDIO_LOC] = RTK_PIN_MUX(sdio_loc, 0x61c, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "sdio_loc_sdio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 0), "sdio_loc_mmc")),
};

static const struct rtd_pin_config_desc rtd1295_cr_configs[RTD1295_CR_NUM] = { };

static const struct rtd_pinctrl_desc rtd1295_cr_pinctrl_desc = {
	.pins = rtd1295_cr_pins,
	.num_pins = ARRAY_SIZE(rtd1295_cr_pins),
	.groups = rtd1295_cr_groups,
	.num_groups = ARRAY_SIZE(rtd1295_cr_groups),
	.functions = rtd1295_cr_functions,
	.num_functions = ARRAY_SIZE(rtd1295_cr_functions),
	.muxes = rtd1295_cr_muxes,
	.num_muxes = ARRAY_SIZE(rtd1295_cr_muxes),
	.configs = rtd1295_cr_configs,
	.num_configs = ARRAY_SIZE(rtd1295_cr_configs),
};

/* ===================== DISP bank ===================== */
enum rtd1295_disp_pins {
	RTD1295_DISP_SPDIF = 0,
	RTD1295_DISP_DMIC_CLK = 1,
	RTD1295_DISP_DMIC_DATA = 2,
	RTD1295_DISP_AO_LRCK = 3,
	RTD1295_DISP_AO_BCK = 4,
	RTD1295_DISP_AOCK = 5,
	RTD1295_DISP_AO_SD_0 = 6,
	RTD1295_DISP_AO_SD_1 = 7,
	RTD1295_DISP_AO_SD_2 = 8,
	RTD1295_DISP_AO_SD_3 = 9,
	RTD1295_DISP_AI_LOC = 10,
	RTD1295_DISP_NUM
};

static const struct pinctrl_pin_desc rtd1295_disp_pins[] = {
	PINCTRL_PIN(RTD1295_DISP_SPDIF, "spdif"),
	PINCTRL_PIN(RTD1295_DISP_DMIC_CLK, "dmic_clk"),
	PINCTRL_PIN(RTD1295_DISP_DMIC_DATA, "dmic_data"),
	PINCTRL_PIN(RTD1295_DISP_AO_LRCK, "ao_lrck"),
	PINCTRL_PIN(RTD1295_DISP_AO_BCK, "ao_bck"),
	PINCTRL_PIN(RTD1295_DISP_AOCK, "aock"),
	PINCTRL_PIN(RTD1295_DISP_AO_SD_0, "ao_sd_0"),
	PINCTRL_PIN(RTD1295_DISP_AO_SD_1, "ao_sd_1"),
	PINCTRL_PIN(RTD1295_DISP_AO_SD_2, "ao_sd_2"),
	PINCTRL_PIN(RTD1295_DISP_AO_SD_3, "ao_sd_3"),
	PINCTRL_PIN(RTD1295_DISP_AI_LOC, "ai_loc"),
};

/* one group per pin */
static const unsigned int disp_spdif_pins[] = { RTD1295_DISP_SPDIF };
static const unsigned int disp_dmic_clk_pins[] = { RTD1295_DISP_DMIC_CLK };
static const unsigned int disp_dmic_data_pins[] = { RTD1295_DISP_DMIC_DATA };
static const unsigned int disp_ao_lrck_pins[] = { RTD1295_DISP_AO_LRCK };
static const unsigned int disp_ao_bck_pins[] = { RTD1295_DISP_AO_BCK };
static const unsigned int disp_aock_pins[] = { RTD1295_DISP_AOCK };
static const unsigned int disp_ao_sd_0_pins[] = { RTD1295_DISP_AO_SD_0 };
static const unsigned int disp_ao_sd_1_pins[] = { RTD1295_DISP_AO_SD_1 };
static const unsigned int disp_ao_sd_2_pins[] = { RTD1295_DISP_AO_SD_2 };
static const unsigned int disp_ao_sd_3_pins[] = { RTD1295_DISP_AO_SD_3 };
static const unsigned int disp_ai_loc_pins[] = { RTD1295_DISP_AI_LOC };

static const struct rtd_pin_group_desc rtd1295_disp_groups[] = {
	{ .name = "spdif", .pins = disp_spdif_pins, .num_pins = 1 },
	{ .name = "dmic_clk", .pins = disp_dmic_clk_pins, .num_pins = 1 },
	{ .name = "dmic_data", .pins = disp_dmic_data_pins, .num_pins = 1 },
	{ .name = "ao_lrck", .pins = disp_ao_lrck_pins, .num_pins = 1 },
	{ .name = "ao_bck", .pins = disp_ao_bck_pins, .num_pins = 1 },
	{ .name = "aock", .pins = disp_aock_pins, .num_pins = 1 },
	{ .name = "ao_sd_0", .pins = disp_ao_sd_0_pins, .num_pins = 1 },
	{ .name = "ao_sd_1", .pins = disp_ao_sd_1_pins, .num_pins = 1 },
	{ .name = "ao_sd_2", .pins = disp_ao_sd_2_pins, .num_pins = 1 },
	{ .name = "ao_sd_3", .pins = disp_ao_sd_3_pins, .num_pins = 1 },
	{ .name = "ai_loc", .pins = disp_ai_loc_pins, .num_pins = 1 },
};

static const char * const disp_ai_grps[] = { "dmic_clk", "dmic_data", "ao_sd_2", "ao_sd_3" };
static const char * const disp_ai_loc_disp_grps[] = { "ai_loc" };
static const char * const disp_ai_loc_main_grps[] = { "ai_loc" };
static const char * const disp_ao_grps[] = { "ao_lrck", "ao_bck", "aock", "ao_sd_0", "ao_sd_1", "ao_sd_2", "ao_sd_3" };
static const char * const disp_dmic_grps[] = { "dmic_clk", "dmic_data" };
static const char * const disp_gpio_grps[] = { "spdif", "dmic_clk", "dmic_data", "ao_lrck", "ao_bck", "aock", "ao_sd_0", "ao_sd_1", "ao_sd_2", "ao_sd_3" };
static const char * const disp_spdif_out_grps[] = { "spdif" };

static const struct rtd_pin_func_desc rtd1295_disp_functions[] = {
	{ .name = "ai", .groups = disp_ai_grps, .num_groups = ARRAY_SIZE(disp_ai_grps) },
	{ .name = "ai_loc_disp", .groups = disp_ai_loc_disp_grps, .num_groups = ARRAY_SIZE(disp_ai_loc_disp_grps) },
	{ .name = "ai_loc_main", .groups = disp_ai_loc_main_grps, .num_groups = ARRAY_SIZE(disp_ai_loc_main_grps) },
	{ .name = "ao", .groups = disp_ao_grps, .num_groups = ARRAY_SIZE(disp_ao_grps) },
	{ .name = "dmic", .groups = disp_dmic_grps, .num_groups = ARRAY_SIZE(disp_dmic_grps) },
	{ .name = "gpio", .groups = disp_gpio_grps, .num_groups = ARRAY_SIZE(disp_gpio_grps) },
	{ .name = "spdif_out", .groups = disp_spdif_out_grps, .num_groups = ARRAY_SIZE(disp_spdif_out_grps) },
};

static const struct rtd_pin_desc rtd1295_disp_muxes[RTD1295_DISP_NUM] = {
	[RTD1295_DISP_SPDIF] = RTK_PIN_MUX(spdif, 0x008, GENMASK(1, 0),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 0), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 0), "spdif_out")),
	[RTD1295_DISP_DMIC_CLK] = RTK_PIN_MUX(dmic_clk, 0x008, GENMASK(3, 2),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 2), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 2), "dmic"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 2), "ai")),
	[RTD1295_DISP_DMIC_DATA] = RTK_PIN_MUX(dmic_data, 0x008, GENMASK(5, 4),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 4), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 4), "dmic"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 4), "ai")),
	[RTD1295_DISP_AO_LRCK] = RTK_PIN_MUX(ao_lrck, 0x008, GENMASK(7, 6),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 6), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 6), "ao")),
	[RTD1295_DISP_AO_BCK] = RTK_PIN_MUX(ao_bck, 0x008, GENMASK(9, 8),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 8), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 8), "ao")),
	[RTD1295_DISP_AOCK] = RTK_PIN_MUX(aock, 0x008, GENMASK(11, 10),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 10), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 10), "ao")),
	[RTD1295_DISP_AO_SD_0] = RTK_PIN_MUX(ao_sd_0, 0x008, GENMASK(13, 12),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 12), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 12), "ao")),
	[RTD1295_DISP_AO_SD_1] = RTK_PIN_MUX(ao_sd_1, 0x008, GENMASK(15, 14),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 14), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 14), "ao")),
	[RTD1295_DISP_AO_SD_2] = RTK_PIN_MUX(ao_sd_2, 0x008, GENMASK(17, 16),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 16), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 16), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 16), "ai")),
	[RTD1295_DISP_AO_SD_3] = RTK_PIN_MUX(ao_sd_3, 0x008, GENMASK(19, 18),
		RTK_PIN_FUNC(SHIFT_LEFT(0x0, 18), "gpio"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 18), "ao"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 18), "ai")),
	[RTD1295_DISP_AI_LOC] = RTK_PIN_MUX(ai_loc, 0x008, GENMASK(21, 20),
		RTK_PIN_FUNC(SHIFT_LEFT(0x1, 20), "ai_loc_main"),
		RTK_PIN_FUNC(SHIFT_LEFT(0x2, 20), "ai_loc_disp")),
};

static const struct rtd_pin_config_desc rtd1295_disp_configs[RTD1295_DISP_NUM] = { };

static const struct rtd_pinctrl_desc rtd1295_disp_pinctrl_desc = {
	.pins = rtd1295_disp_pins,
	.num_pins = ARRAY_SIZE(rtd1295_disp_pins),
	.groups = rtd1295_disp_groups,
	.num_groups = ARRAY_SIZE(rtd1295_disp_groups),
	.functions = rtd1295_disp_functions,
	.num_functions = ARRAY_SIZE(rtd1295_disp_functions),
	.muxes = rtd1295_disp_muxes,
	.num_muxes = ARRAY_SIZE(rtd1295_disp_muxes),
	.configs = rtd1295_disp_configs,
	.num_configs = ARRAY_SIZE(rtd1295_disp_configs),
};

static int rtd1295_pinctrl_probe(struct platform_device *pdev)
{
	const struct rtd_pinctrl_desc *desc = of_device_get_match_data(&pdev->dev);

	return rtd_pinctrl_probe(pdev, desc);
}

static const struct of_device_id rtd1295_pinctrl_of_match[] = {
	{ .compatible = "realtek,rtd1295-iso-pinctrl",  .data = &rtd1295_iso_pinctrl_desc },
	{ .compatible = "realtek,rtd1295-sb2-pinctrl",  .data = &rtd1295_sb2_pinctrl_desc },
	{ .compatible = "realtek,rtd1295-cr-pinctrl",   .data = &rtd1295_cr_pinctrl_desc },
	{ .compatible = "realtek,rtd1295-disp-pinctrl", .data = &rtd1295_disp_pinctrl_desc },
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
