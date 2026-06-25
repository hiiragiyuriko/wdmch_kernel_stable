// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek RTD129x PWM controller
 *
 * A clean reimplementation for mainline of the Realtek 4.9 vendor PWM driver,
 * using the atomic PWM API and the standard #pwm-cells binding (the vendor
 * driver used a bespoke per-channel DT binding plus a large sysfs interface).
 *
 * Each of the 4 channels is programmed through three shared registers, with a
 * per-channel field in each:
 *
 *   OCD (output clock divider, 8 bits/ch) - "ocd"
 *   CD  (clock duty,           8 bits/ch) - high-time count, 0..ocd
 *   CSD (clock source divider, 4 bits/ch) - "csd"
 *
 * The output frequency is base_freq / (2^(csd+1) * (ocd+1)) and the duty cycle
 * is (cd+1)/(ocd+1). The base frequency is the 27 MHz oscillator.
 *
 * Copyright (c) 2017 Realtek Semiconductor Corp.
 * Copyright (c) 2024 (mainline port)
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/spinlock.h>

#define RTK_PWM_OCD		0x0
#define RTK_PWM_CD		0x4
#define RTK_PWM_CSD		0x8

#define RTK_PWM_OCD_SHIFT	8
#define RTK_PWM_CD_SHIFT	8
#define RTK_PWM_CSD_SHIFT	4

#define RTK_PWM_OCD_MASK	0xff
#define RTK_PWM_CD_MASK		0xff
#define RTK_PWM_CSD_MASK	0xf

#define RTK_PWM_NUM		4
#define RTK_PWM_BASE_FREQ	27000000	/* 27 MHz oscillator */

struct rtk_pwm {
	void __iomem	*base;
	spinlock_t	lock;
};

static inline struct rtk_pwm *to_rtk_pwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

/* Update one channel's field inside a shared register under the lock. */
static void rtk_pwm_update(struct rtk_pwm *rp, u32 reg, u32 mask, u32 shift,
			   unsigned int hwpwm, u32 val)
{
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&rp->lock, flags);
	v = readl(rp->base + reg);
	v &= ~(mask << (hwpwm * shift));
	v |= (val & mask) << (hwpwm * shift);
	writel(v, rp->base + reg);
	spin_unlock_irqrestore(&rp->lock, flags);
}

static u32 rtk_pwm_read_field(struct rtk_pwm *rp, u32 reg, u32 mask, u32 shift,
			      unsigned int hwpwm)
{
	return (readl(rp->base + reg) >> (hwpwm * shift)) & mask;
}

static int rtk_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct rtk_pwm *rp = to_rtk_pwm(chip);
	unsigned int hwpwm = pwm->hwpwm;
	u64 div, target_freq;
	int csd, ocd, cd;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/* Disabled or fully off: clear the channel's fields. */
	if (!state->enabled || state->duty_cycle == 0 || state->period == 0) {
		rtk_pwm_update(rp, RTK_PWM_OCD, RTK_PWM_OCD_MASK,
			       RTK_PWM_OCD_SHIFT, hwpwm, 0);
		rtk_pwm_update(rp, RTK_PWM_CD, RTK_PWM_CD_MASK,
			       RTK_PWM_CD_SHIFT, hwpwm, 0);
		rtk_pwm_update(rp, RTK_PWM_CSD, RTK_PWM_CSD_MASK,
			       RTK_PWM_CSD_SHIFT, hwpwm, 0);
		return 0;
	}

	/*
	 * div = base_freq / target_freq = base_freq * period / 1e9.
	 * Split into csd (power-of-two source divider) and ocd (linear output
	 * divider): div ~= 2^(csd+1) * (ocd+1). Pick the largest ocd (finest
	 * duty resolution) by giving csd just enough to keep ocd <= 255.
	 */
	target_freq = div64_u64(NSEC_PER_SEC, state->period);
	if (target_freq == 0)
		return -EINVAL;
	div = div64_u64(RTK_PWM_BASE_FREQ, target_freq);
	if (div < 2)
		div = 2;

	csd = fls64(div) - 9;
	if (csd < 0)
		csd = 0;
	else if (csd > RTK_PWM_CSD_MASK)
		csd = RTK_PWM_CSD_MASK;

	ocd = (div >> (csd + 1)) - 1;
	if (ocd < 0)
		ocd = 0;
	else if (ocd > RTK_PWM_OCD_MASK)
		ocd = RTK_PWM_OCD_MASK;

	/* cd (high-time count) = duty/period * (ocd+1) - 1, clamped to [0, ocd]. */
	cd = div64_u64((u64)state->duty_cycle * (ocd + 1), state->period) - 1;
	if (cd < 0)
		cd = 0;
	else if (cd > ocd)
		cd = ocd;

	rtk_pwm_update(rp, RTK_PWM_OCD, RTK_PWM_OCD_MASK, RTK_PWM_OCD_SHIFT,
		       hwpwm, ocd);
	rtk_pwm_update(rp, RTK_PWM_CD, RTK_PWM_CD_MASK, RTK_PWM_CD_SHIFT,
		       hwpwm, cd);
	rtk_pwm_update(rp, RTK_PWM_CSD, RTK_PWM_CSD_MASK, RTK_PWM_CSD_SHIFT,
		       hwpwm, csd);

	return 0;
}

static int rtk_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct rtk_pwm *rp = to_rtk_pwm(chip);
	unsigned int hwpwm = pwm->hwpwm;
	u32 ocd, cd, csd, div;

	ocd = rtk_pwm_read_field(rp, RTK_PWM_OCD, RTK_PWM_OCD_MASK,
				 RTK_PWM_OCD_SHIFT, hwpwm);
	cd = rtk_pwm_read_field(rp, RTK_PWM_CD, RTK_PWM_CD_MASK,
				RTK_PWM_CD_SHIFT, hwpwm);
	csd = rtk_pwm_read_field(rp, RTK_PWM_CSD, RTK_PWM_CSD_MASK,
				 RTK_PWM_CSD_SHIFT, hwpwm);

	div = BIT(csd + 1) * (ocd + 1);
	state->period = DIV_ROUND_UP_ULL((u64)NSEC_PER_SEC * div,
					 RTK_PWM_BASE_FREQ);
	state->duty_cycle = DIV_ROUND_UP_ULL((u64)state->period * (cd + 1),
					     ocd + 1);
	state->polarity = PWM_POLARITY_NORMAL;
	/* A channel with no output divider is effectively off. */
	state->enabled = ocd != 0;

	return 0;
}

static const struct pwm_ops rtk_pwm_ops = {
	.apply = rtk_pwm_apply,
	.get_state = rtk_pwm_get_state,
};

static int rtk_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct rtk_pwm *rp;

	chip = devm_pwmchip_alloc(&pdev->dev, RTK_PWM_NUM, sizeof(*rp));
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	rp = to_rtk_pwm(chip);
	spin_lock_init(&rp->lock);

	rp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rp->base))
		return PTR_ERR(rp->base);

	chip->ops = &rtk_pwm_ops;

	return devm_pwmchip_add(&pdev->dev, chip);
}

static const struct of_device_id rtk_pwm_of_match[] = {
	{ .compatible = "realtek,rtd1295-pwm" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtk_pwm_of_match);

static struct platform_driver rtk_pwm_driver = {
	.driver = {
		.name = "pwm-rtk",
		.of_match_table = rtk_pwm_of_match,
	},
	.probe = rtk_pwm_probe,
};
module_platform_driver(rtk_pwm_driver);

MODULE_DESCRIPTION("Realtek RTD129x PWM driver");
MODULE_LICENSE("GPL");
