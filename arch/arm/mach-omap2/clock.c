/*
 *  linux/arch/arm/mach-omap2/clock.c
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2008 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/bitops.h>
#include <linux/io.h>

#include <asm/arch/clock.h>
#include <asm/arch/clockdomain.h>
#include <asm/arch/sram.h>
#include <asm/arch/cpu.h>
#include <asm/arch/prcm.h>
#include <asm/div64.h>

#include <asm/arch/sdrc.h>
#include "sdrc.h"
#include "clock.h"
#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"

#define MAX_CLOCK_ENABLE_WAIT		100000

/* DPLL rate rounding: minimum DPLL multiplier, divider values */
#define DPLL_MIN_MULTIPLIER		1
#define DPLL_MIN_DIVIDER		1

/* Possible error results from _dpll_test_mult */
#define DPLL_MULT_UNDERFLOW		(1 << 0)

/*
 * Scale factor to mitigate roundoff errors in DPLL rate rounding.
 * The higher the scale factor, the greater the risk of arithmetic overflow,
 * but the closer the rounded rate to the target rate.  DPLL_SCALE_FACTOR
 * must be a power of DPLL_SCALE_BASE.
 */
#define DPLL_SCALE_FACTOR		64
#define DPLL_SCALE_BASE			2
#define DPLL_ROUNDING_VAL		((DPLL_SCALE_BASE / 2) * \
					 (DPLL_SCALE_FACTOR / DPLL_SCALE_BASE))

u8 cpu_mask;

/*-------------------------------------------------------------------------
 * OMAP2/3 specific clock functions
 *-------------------------------------------------------------------------*/

/**
 * omap2_init_clk_clkdm - look up a clockdomain name, store pointer in clk
 * @clk: OMAP clock struct ptr to use
 *
 * Convert a clockdomain name stored in a struct clk 'clk' into a
 * clockdomain pointer, and save it into the struct clk.  Intended to be
 * called during clk_register().  No return value.
 */
void omap2_init_clk_clkdm(struct clk *clk)
{
	struct clockdomain *clkdm;

	if (!clk->clkdm.name)
		return;

	clkdm = clkdm_lookup(clk->clkdm.name);
	if (clkdm) {
		pr_debug("clock: associated clk %s to clkdm %s\n",
			 clk->name, clk->clkdm.name);
		clk->clkdm.ptr = clkdm;
	} else {
		pr_debug("clock: could not associate clk %s to "
			 "clkdm %s\n", clk->name, clk->clkdm.name);
	}
}

/**
 * omap2_init_clksel_parent - set a clksel clk's parent field from the hardware
 * @clk: OMAP clock struct ptr to use
 *
 * Given a pointer to a source-selectable struct clk, read the hardware
 * register and determine what its parent is currently set to.  Update the
 * clk->parent field with the appropriate clk ptr.
 */
void omap2_init_clksel_parent(struct clk *clk)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;
	u32 r, found = 0;

	if (!clk->clksel)
		return;

	r = __raw_readl(clk->clksel_reg) & clk->clksel_mask;
	r >>= __ffs(clk->clksel_mask);

	for (clks = clk->clksel; clks->parent && !found; clks++) {
		for (clkr = clks->rates; clkr->div && !found; clkr++) {
			if ((clkr->flags & cpu_mask) && (clkr->val == r)) {
				if (clk->parent != clks->parent) {
					pr_debug("clock: inited %s parent "
						 "to %s (was %s)\n",
						 clk->name, clks->parent->name,
						 ((clk->parent) ?
						  clk->parent->name : "NULL"));
					clk->parent = clks->parent;
				};
				found = 1;
			}
		}
	}

	if (!found)
		printk(KERN_ERR "clock: init parent: could not find "
		       "regval %0x for clock %s\n", r,  clk->name);

	return;
}

/* Returns the DPLL rate */
u32 omap2_get_dpll_rate(struct clk *clk)
{
	long long dpll_clk;
	u32 dpll_mult, dpll_div, dpll;
	struct dpll_data *dd;

	dd = clk->dpll_data;
	/* REVISIT: What do we return on error? */
	if (!dd)
		return 0;

	dpll = __raw_readl(dd->mult_div1_reg);
	dpll_mult = dpll & dd->mult_mask;
	dpll_mult >>= __ffs(dd->mult_mask);
	dpll_div = dpll & dd->div1_mask;
	dpll_div >>= __ffs(dd->div1_mask);

	dpll_clk = (long long)clk->parent->rate * dpll_mult;
	do_div(dpll_clk, dpll_div + 1);

	return dpll_clk;
}

/*
 * Used for clocks that have the same value as the parent clock,
 * divided by some factor
 */
void omap2_fixed_divisor_recalc(struct clk *clk)
{
	WARN_ON(!clk->fixed_div);

	clk->rate = clk->parent->rate / clk->fixed_div;

	if (clk->flags & RATE_PROPAGATES)
		propagate_rate(clk);
}

/**
 * omap2_wait_clock_ready - wait for clock to enable
 * @reg: physical address of clock IDLEST register
 * @mask: value to mask against to determine if the clock is active
 * @name: name of the clock (for printk)
 *
 * Returns 1 if the clock enabled in time, or 0 if it failed to enable
 * in roughly MAX_CLOCK_ENABLE_WAIT microseconds.
 */
int omap2_wait_clock_ready(void __iomem *reg, u32 mask, const char *name)
{
	int i = 0;
	int ena = 0;

	/*
	 * 24xx uses 0 to indicate not ready, and 1 to indicate ready.
	 * 34xx reverses this, just to keep us on our toes
	 */
	if (cpu_mask & (RATE_IN_242X | RATE_IN_243X))
		ena = mask;
	else if (cpu_mask & RATE_IN_343X)
		ena = 0;

	/* Wait for lock */
	while (((__raw_readl(reg) & mask) != ena) &&
	       (i++ < MAX_CLOCK_ENABLE_WAIT)) {
		udelay(1);
	}

	if (i < MAX_CLOCK_ENABLE_WAIT)
		pr_debug("Clock %s stable after %d loops\n", name, i);
	else
		printk(KERN_ERR "Clock %s didn't enable in %d tries\n",
		       name, MAX_CLOCK_ENABLE_WAIT);


	return (i < MAX_CLOCK_ENABLE_WAIT) ? 1 : 0;
};


/*
 * Note: We don't need special code here for INVERT_ENABLE
 * for the time being since INVERT_ENABLE only applies to clocks enabled by
 * CM_CLKEN_PLL
 *
 * REVISIT: This code is ugly and does not belong here.
 */
static void omap2_clk_wait_ready(struct clk *clk)
{
	u32 other_bit, idlest_bit;
	unsigned long reg, other_reg, idlest_reg, prcm_mod, prcm_regid;

	reg = (unsigned long)clk->enable_reg;
	prcm_mod = reg & ~0xff;
	prcm_regid = reg & 0xff;

	if (prcm_regid >= CM_FCLKEN1 && prcm_regid <= OMAP24XX_CM_FCLKEN2)
		other_reg = ((reg & ~0xf0) | 0x10); /* CM_ICLKEN* */
	else if (prcm_regid >= CM_ICLKEN1 && prcm_regid <= OMAP24XX_CM_ICLKEN4)
		other_reg = ((reg & ~0xf0) | 0x00); /* CM_FCLKEN* */
	else
		return;

	/* Covers most of the cases - a few exceptions are below */
	other_bit = 1 << clk->enable_bit;
	idlest_bit = other_bit;

	/* 24xx: DSS and CAM have no idlest bits for their target agents */
	if (cpu_is_omap24xx() &&
	    (prcm_mod == OMAP2420_CM_REGADDR(CORE_MOD, 0) ||
	     prcm_mod == OMAP2430_CM_REGADDR(CORE_MOD, 0)) &&
	    (reg & 0x0f) == 0) { /* CM_{F,I}CLKEN1 */

		if (clk->enable_bit == OMAP24XX_EN_DSS2_SHIFT ||
		    clk->enable_bit == OMAP24XX_EN_DSS1_SHIFT ||
		    clk->enable_bit == OMAP24XX_EN_CAM_SHIFT)
			return;

	}

	/* REVISIT: What are the appropriate exclusions for 34XX? */
	if (cpu_is_omap34xx()) {

		/* SSI */
		if (prcm_mod == OMAP34XX_CM_REGADDR(CORE_MOD, 0) &&
		    (reg & 0x0f) == 0 &&
		    clk->enable_bit == OMAP3430_EN_SSI_SHIFT) {

			if (is_sil_rev_equal_to(OMAP3430_REV_ES1_0))
				return;

			idlest_bit = OMAP3430ES2_ST_SSI_IDLE;
		}

		/* DSS */
		if (prcm_mod == OMAP34XX_CM_REGADDR(OMAP3430_DSS_MOD, 0)) {

			/* 3430ES1 DSS has no target idlest bits */
			if (is_sil_rev_equal_to(OMAP3430_REV_ES1_0))
				return;

			/*
			 * For 3430ES2+ DSS, only wait once (dss1_alwon_fclk,
			 * dss_l3_iclk, dss_l4_iclk) are enabled
			 */
			if (clk->enable_bit != OMAP3430_EN_DSS1_SHIFT)
				return;

			idlest_bit = OMAP3430ES2_ST_DSS_IDLE;
		}

		/* USBHOST */
		if (is_sil_rev_greater_than(OMAP3430_REV_ES1_0) &&
		    prcm_mod == OMAP34XX_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, 0)) {

			/*
			 * The 120MHz clock apparently has nothing to do with
			 * USBHOST module accessibility
			 */
			if (clk->enable_bit == OMAP3430ES2_EN_USBHOST2_SHIFT)
				return;

			idlest_bit = OMAP3430ES2_ST_USBHOST_IDLE;

		}
	}

	/* Check if both functional and interface clocks
	 * are running. */
	if (!(__raw_readl((void __iomem *)other_reg) & other_bit))
		return;

	idlest_reg = ((other_reg & ~0xf0) | 0x20); /* CM_IDLEST* */

	omap2_wait_clock_ready((void __iomem *)idlest_reg, idlest_bit,
			       clk->name);
}

/* Enables clock without considering parent dependencies or use count
 * REVISIT: Maybe change this to use clk->enable like on omap1?
 */
static int _omap2_clk_enable(struct clk *clk)
{
	u32 regval32;

	if (clk->flags & (ALWAYS_ENABLED | PARENT_CONTROLS_CLOCK))
		return 0;

	if (clk->enable)
		return clk->enable(clk);

	if (!clk->enable_reg) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return 0; /* REVISIT: -EINVAL */
	}

	regval32 = __raw_readl(clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		regval32 &= ~(1 << clk->enable_bit);
	else
		regval32 |= (1 << clk->enable_bit);
	__raw_writel(regval32, clk->enable_reg);
	wmb();

	omap2_clk_wait_ready(clk);

	return 0;
}

/* Disables clock without considering parent dependencies or use count */
static void _omap2_clk_disable(struct clk *clk)
{
	u32 regval32;

	if (clk->flags & (ALWAYS_ENABLED | PARENT_CONTROLS_CLOCK))
		return;

	if (clk->disable) {
		clk->disable(clk);
		return;
	}

	if (!clk->enable_reg) {
		/*
		 * 'Independent' here refers to a clock which is not
		 * controlled by its parent.
		 */
		printk(KERN_ERR "clock: clk_disable called on independent "
		       "clock %s which has no enable_reg\n", clk->name);
		return;
	}

	regval32 = __raw_readl(clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		regval32 |= (1 << clk->enable_bit);
	else
		regval32 &= ~(1 << clk->enable_bit);
	__raw_writel(regval32, clk->enable_reg);
	wmb();
}

void omap2_clk_disable(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		_omap2_clk_disable(clk);
		if (clk->parent)
			omap2_clk_disable(clk->parent);
		if (clk->clkdm.ptr)
			omap2_clkdm_clk_disable(clk->clkdm.ptr, clk);

	}
}

int omap2_clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount++ == 0) {
		if (clk->parent)
			ret = omap2_clk_enable(clk->parent);

		if (ret != 0) {
			clk->usecount--;
			return ret;
		}

		if (clk->clkdm.ptr)
			omap2_clkdm_clk_enable(clk->clkdm.ptr, clk);

		ret = _omap2_clk_enable(clk);

		if (ret != 0) {
			if (clk->clkdm.ptr)
				omap2_clkdm_clk_disable(clk->clkdm.ptr, clk);

			if (clk->parent) {
				omap2_clk_disable(clk->parent);
				clk->usecount--;
			}
		}
	}

	return ret;
}

/*
 * Used for clocks that are part of CLKSEL_xyz governed clocks.
 * REVISIT: Maybe change to use clk->enable() functions like on omap1?
 */
void omap2_clksel_recalc(struct clk *clk)
{
	u32 div = 0;

	pr_debug("clock: recalc'ing clksel clk %s\n", clk->name);

	div = omap2_clksel_get_divisor(clk);
	if (div == 0)
		return;

	if (clk->rate == (clk->parent->rate / div))
		return;
	clk->rate = clk->parent->rate / div;

	pr_debug("clock: new clock rate is %ld (div %d)\n", clk->rate, div);

	if (clk->flags & RATE_PROPAGATES)
		propagate_rate(clk);
}

/**
 * omap2_get_clksel_by_parent - return clksel struct for a given clk & parent
 * @clk: OMAP struct clk ptr to inspect
 * @src_clk: OMAP struct clk ptr of the parent clk to search for
 *
 * Scan the struct clksel array associated with the clock to find
 * the element associated with the supplied parent clock address.
 * Returns a pointer to the struct clksel on success or NULL on error.
 */
static const struct clksel *omap2_get_clksel_by_parent(struct clk *clk,
						       struct clk *src_clk)
{
	const struct clksel *clks;

	if (!clk->clksel)
		return NULL;

	for (clks = clk->clksel; clks->parent; clks++) {
		if (clks->parent == src_clk)
			break; /* Found the requested parent */
	}

	if (!clks->parent) {
		printk(KERN_ERR "clock: Could not find parent clock %s in "
		       "clksel array of clock %s\n", src_clk->name,
		       clk->name);
		return NULL;
	}

	return clks;
}

/**
 * omap2_clksel_round_rate_div - find divisor for the given clock and rate
 * @clk: OMAP struct clk to use
 * @target_rate: desired clock rate
 * @new_div: ptr to where we should store the divisor
 *
 * Finds 'best' divider value in an array based on the source and target
 * rates.  The divider array must be sorted with smallest divider first.
 * Note that this will not work for clocks which are part of CONFIG_PARTICIPANT,
 * they are only settable as part of virtual_prcm set.
 *
 * Returns the rounded clock rate or returns 0xffffffff on error.
 */
u32 omap2_clksel_round_rate_div(struct clk *clk, unsigned long target_rate,
				u32 *new_div)
{
	unsigned long test_rate;
	const struct clksel *clks;
	const struct clksel_rate *clkr;
	u32 last_div = 0;

	printk(KERN_INFO "clock: clksel_round_rate_div: %s target_rate %ld\n",
	       clk->name, target_rate);

	*new_div = 1;

	clks = omap2_get_clksel_by_parent(clk, clk->parent);
	if (!clks)
		return ~0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if (!(clkr->flags & cpu_mask))
		    continue;

		/* Sanity check */
		if (clkr->div <= last_div)
			printk(KERN_ERR "clock: clksel_rate table not sorted "
			       "for clock %s", clk->name);

		last_div = clkr->div;

		test_rate = clk->parent->rate / clkr->div;

		if (test_rate <= target_rate)
			break; /* found it */
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find divisor for target "
		       "rate %ld for clock %s parent %s\n", target_rate,
		       clk->name, clk->parent->name);
		return ~0;
	}

	*new_div = clkr->div;

	printk(KERN_INFO "clock: new_div = %d, new_rate = %ld\n", *new_div,
	       (clk->parent->rate / clkr->div));

	return (clk->parent->rate / clkr->div);
}

/**
 * omap2_clksel_round_rate - find rounded rate for the given clock and rate
 * @clk: OMAP struct clk to use
 * @target_rate: desired clock rate
 *
 * Compatibility wrapper for OMAP clock framework
 * Finds best target rate based on the source clock and possible dividers.
 * rates. The divider array must be sorted with smallest divider first.
 * Note that this will not work for clocks which are part of CONFIG_PARTICIPANT,
 * they are only settable as part of virtual_prcm set.
 *
 * Returns the rounded clock rate or returns 0xffffffff on error.
 */
long omap2_clksel_round_rate(struct clk *clk, unsigned long target_rate)
{
	u32 new_div;

	return omap2_clksel_round_rate_div(clk, target_rate, &new_div);
}


/* Given a clock and a rate apply a clock specific rounding function */
long omap2_clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	if (clk->flags & RATE_FIXED)
		printk(KERN_ERR "clock: generic omap2_clk_round_rate called "
		       "on fixed-rate clock %s\n", clk->name);

	return clk->rate;
}

/**
 * omap2_clksel_to_divisor() - turn clksel field value into integer divider
 * @clk: OMAP struct clk to use
 * @field_val: register field value to find
 *
 * Given a struct clk of a rate-selectable clksel clock, and a register field
 * value to search for, find the corresponding clock divisor.  The register
 * field value should be pre-masked and shifted down so the LSB is at bit 0
 * before calling.  Returns 0 on error
 */
u32 omap2_clksel_to_divisor(struct clk *clk, u32 field_val)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;

	clks = omap2_get_clksel_by_parent(clk, clk->parent);
	if (!clks)
		return 0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if ((clkr->flags & cpu_mask) && (clkr->val == field_val))
			break;
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find fieldval %d for "
		       "clock %s parent %s\n", field_val, clk->name,
		       clk->parent->name);
		return 0;
	}

	return clkr->div;
}

/**
 * omap2_divisor_to_clksel() - turn clksel integer divisor into a field value
 * @clk: OMAP struct clk to use
 * @div: integer divisor to search for
 *
 * Given a struct clk of a rate-selectable clksel clock, and a clock divisor,
 * find the corresponding register field value.  The return register value is
 * the value before left-shifting.  Returns 0xffffffff on error
 */
u32 omap2_divisor_to_clksel(struct clk *clk, u32 div)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;

	/* should never happen */
	WARN_ON(div == 0);

	clks = omap2_get_clksel_by_parent(clk, clk->parent);
	if (!clks)
		return 0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if ((clkr->flags & cpu_mask) && (clkr->div == div))
			break;
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find divisor %d for "
		       "clock %s parent %s\n", div, clk->name,
		       clk->parent->name);
		return 0;
	}

	return clkr->val;
}

/**
 * omap2_get_clksel - find clksel register addr & field mask for a clk
 * @clk: struct clk to use
 * @field_mask: ptr to u32 to store the register field mask
 *
 * Returns the address of the clksel register upon success or NULL on error.
 */
static void __iomem *omap2_get_clksel(struct clk *clk, u32 *field_mask)
{
	if (!clk->clksel_reg || (clk->clksel_mask == 0))
		return NULL;

	*field_mask = clk->clksel_mask;

	return clk->clksel_reg;
}

/**
 * omap2_clksel_get_divisor - get current divider applied to parent clock.
 * @clk: OMAP struct clk to use.
 *
 * Returns the integer divisor upon success or 0 on error.
 */
u32 omap2_clksel_get_divisor(struct clk *clk)
{
	u32 field_mask, field_val;
	void __iomem *div_addr;

	div_addr = omap2_get_clksel(clk, &field_mask);
	if (!div_addr)
		return 0;

	field_val = __raw_readl(div_addr) & field_mask;
	field_val >>= __ffs(field_mask);

	return omap2_clksel_to_divisor(clk, field_val);
}

int omap2_clksel_set_rate(struct clk *clk, unsigned long rate)
{
	u32 field_mask, field_val, validrate, new_div = 0;
	void __iomem *div_addr;
	u32 v;

	validrate = omap2_clksel_round_rate_div(clk, rate, &new_div);
	if (validrate != rate)
		return -EINVAL;

	div_addr = omap2_get_clksel(clk, &field_mask);
	if (!div_addr)
		return -EINVAL;

	field_val = omap2_divisor_to_clksel(clk, new_div);
	if (field_val == ~0)
		return -EINVAL;

	v = __raw_readl(div_addr);
	v &= ~field_mask;
	v |= field_val << __ffs(field_mask);
	__raw_writel(v, div_addr);

	wmb();

	clk->rate = clk->parent->rate / new_div;

	if (clk->flags & DELAYED_APP && cpu_is_omap24xx()) {
		prm_write_mod_reg(OMAP24XX_VALID_CONFIG,
			OMAP24XX_GR_MOD, OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET);
		wmb();
	}

	return 0;
}


/* Set the clock rate for a clock source */
int omap2_clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	pr_debug("clock: set_rate for clock %s to rate %ld\n", clk->name, rate);

	/* CONFIG_PARTICIPANT clocks are changed only in sets via the
	   rate table mechanism, driven by mpu_speed  */
	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	/* dpll_ck, core_ck, virt_prcm_set; plus all clksel clocks */
	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);

	if (ret == 0 && (clk->flags & RATE_PROPAGATES))
		propagate_rate(clk);

	return ret;
}

/*
 * Converts encoded control register address into a full address
 * On error, *src_addr will be returned as 0.
 */
static u32 omap2_clksel_get_src_field(void __iomem **src_addr,
				      struct clk *src_clk, u32 *field_mask,
				      struct clk *clk, u32 *parent_div)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;

	*parent_div = 0;
	*src_addr = NULL;

	clks = omap2_get_clksel_by_parent(clk, src_clk);
	if (!clks)
		return 0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if (clkr->flags & (cpu_mask | DEFAULT_RATE))
			break; /* Found the default rate for this platform */
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find default rate for "
		       "clock %s parent %s\n", clk->name,
		       src_clk->parent->name);
		return 0;
	}

	/* Should never happen.  Add a clksel mask to the struct clk. */
	WARN_ON(clk->clksel_mask == 0);

	*field_mask = clk->clksel_mask;
	*src_addr = clk->clksel_reg;
	*parent_div = clkr->div;

	return clkr->val;
}

int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent)
{
	void __iomem *src_addr;
	u32 field_val, field_mask, reg_val, parent_div;

	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	if (!clk->clksel)
		return -EINVAL;

	field_val = omap2_clksel_get_src_field(&src_addr, new_parent,
					       &field_mask, clk, &parent_div);
	if (!src_addr)
		return -EINVAL;

	if (clk->usecount > 0)
		_omap2_clk_disable(clk);

	/* Set new source value (previous dividers if any in effect) */
	reg_val = __raw_readl(src_addr) & ~field_mask;
	reg_val |= (field_val << __ffs(field_mask));
	__raw_writel(reg_val, src_addr);
	wmb();

	if (clk->flags & DELAYED_APP && cpu_is_omap24xx()) {
		prm_write_mod_reg(OMAP24XX_VALID_CONFIG,
			OMAP24XX_GR_MOD, OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET);
		wmb();
	}

	if (clk->usecount > 0)
		_omap2_clk_enable(clk);

	clk->parent = new_parent;

	/* CLKSEL clocks follow their parents' rates, divided by a divisor */
	clk->rate = new_parent->rate;

	if (parent_div > 0)
		clk->rate /= parent_div;

	pr_debug("clock: set parent of %s to %s (new rate %ld)\n",
		 clk->name, clk->parent->name, clk->rate);

	if (clk->flags & RATE_PROPAGATES)
		propagate_rate(clk);

	return 0;
}

/* DPLL rate rounding code */

/**
 * omap2_dpll_set_rate_tolerance: set the error tolerance during rate rounding
 * @clk: struct clk * of the DPLL
 * @tolerance: maximum rate error tolerance
 *
 * Set the maximum DPLL rate error tolerance for the rate rounding
 * algorithm.  The rate tolerance is an attempt to balance DPLL power
 * saving (the least divider value "n") vs. rate fidelity (the least
 * difference between the desired DPLL target rate and the rounded
 * rate out of the algorithm).  So, increasing the tolerance is likely
 * to decrease DPLL power consumption and increase DPLL rate error.
 * Returns -EINVAL if provided a null clock ptr or a clk that is not a
 * DPLL; or 0 upon success.
 */
int omap2_dpll_set_rate_tolerance(struct clk *clk, unsigned int tolerance)
{
	if (!clk || !clk->dpll_data)
		return -EINVAL;

	clk->dpll_data->rate_tolerance = tolerance;

	return 0;
}

static unsigned long _dpll_compute_new_rate(unsigned long parent_rate,
					    unsigned int m, unsigned int n)
{
	unsigned long long num;

	num = (unsigned long long)parent_rate * m;
	do_div(num, n);
	return num;
}

/*
 * _dpll_test_mult - test a DPLL multiplier value
 * @m: pointer to the DPLL m (multiplier) value under test
 * @n: current DPLL n (divider) value under test
 * @new_rate: pointer to storage for the resulting rounded rate
 * @target_rate: the desired DPLL rate
 * @parent_rate: the DPLL's parent clock rate
 *
 * This code tests a DPLL multiplier value, ensuring that the
 * resulting rate will not be higher than the target_rate, and that
 * the multiplier value itself is valid for the DPLL.  Initially, the
 * integer pointed to by the m argument should be prescaled by
 * multiplying by DPLL_SCALE_FACTOR.  The code will replace this with
 * a non-scaled m upon return.  This non-scaled m will result in a
 * new_rate as close as possible to target_rate (but not greater than
 * target_rate) given the current (parent_rate, n, prescaled m)
 * triple. Returns DPLL_MULT_UNDERFLOW in the event that the
 * non-scaled m attempted to underflow, which can allow the calling
 * function to bail out early; or 0 upon success.
 */
static int _dpll_test_mult(int *m, int n, unsigned long *new_rate,
			   unsigned long target_rate,
			   unsigned long parent_rate)
{
	int flags = 0, carry = 0;

	/* Unscale m and round if necessary */
	if (*m % DPLL_SCALE_FACTOR >= DPLL_ROUNDING_VAL)
		carry = 1;
	*m = (*m / DPLL_SCALE_FACTOR) + carry;

	/*
	 * The new rate must be <= the target rate to avoid programming
	 * a rate that is impossible for the hardware to handle
	 */
	*new_rate = _dpll_compute_new_rate(parent_rate, *m, n);
	if (*new_rate > target_rate) {
		(*m)--;
		*new_rate = 0;
	}

	/* Guard against m underflow */
	if (*m < DPLL_MIN_MULTIPLIER) {
		*m = DPLL_MIN_MULTIPLIER;
		*new_rate = 0;
		flags = DPLL_MULT_UNDERFLOW;
	}

	if (*new_rate == 0)
		*new_rate = _dpll_compute_new_rate(parent_rate, *m, n);

	return flags;
}

/**
 * omap2_dpll_round_rate - round a target rate for an OMAP DPLL
 * @clk: struct clk * for a DPLL
 * @target_rate: desired DPLL clock rate
 *
 * Given a DPLL, a desired target rate, and a rate tolerance, round
 * the target rate to a possible, programmable rate for this DPLL.
 * Rate tolerance is assumed to be set by the caller before this
 * function is called.  Attempts to select the minimum possible n
 * within the tolerance to reduce power consumption.  Stores the
 * computed (m, n) in the DPLL's dpll_data structure so set_rate()
 * will not need to call this (expensive) function again.  Returns ~0
 * if the target rate cannot be rounded, either because the rate is
 * too low or because the rate tolerance is set too tightly; or the
 * rounded rate upon success.
 */
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate)
{
	int m, n, r, e, scaled_max_m;
	unsigned long scaled_rt_rp, new_rate;
	int min_e = -1, min_e_m = -1, min_e_n = -1;

	if (!clk || !clk->dpll_data)
		return ~0;

	pr_debug("clock: starting DPLL round_rate for clock %s, target rate "
		 "%ld\n", clk->name, target_rate);

	scaled_rt_rp = target_rate / (clk->parent->rate / DPLL_SCALE_FACTOR);
	scaled_max_m = clk->dpll_data->max_multiplier * DPLL_SCALE_FACTOR;

	clk->dpll_data->last_rounded_rate = 0;

	for (n = clk->dpll_data->max_divider; n >= DPLL_MIN_DIVIDER; n--) {

		/* Compute the scaled DPLL multiplier, based on the divider */
		m = scaled_rt_rp * n;

		/*
		 * Since we're counting n down, a m overflow means we can
		 * can immediately skip to the next n
		 */
		if (m > scaled_max_m)
			continue;

		r = _dpll_test_mult(&m, n, &new_rate, target_rate,
				    clk->parent->rate);

		e = target_rate - new_rate;
		pr_debug("clock: n = %d: m = %d: rate error is %d "
			 "(new_rate = %ld)\n", n, m, e, new_rate);

		if (min_e == -1 ||
		    min_e >= (int)(abs(e) - clk->dpll_data->rate_tolerance)) {
			min_e = e;
			min_e_m = m;
			min_e_n = n;

			pr_debug("clock: found new least error %d\n", min_e);
		}

		/*
		 * Since we're counting n down, a m underflow means we
		 * can bail out completely (since as n decreases in
		 * the next iteration, there's no way that m can
		 * increase beyond the current m)
		 */
		if (r & DPLL_MULT_UNDERFLOW)
			break;
	}

	if (min_e < 0) {
		pr_debug("clock: error: target rate or tolerance too low\n");
		return ~0;
	}

	clk->dpll_data->last_rounded_m = min_e_m;
	clk->dpll_data->last_rounded_n = min_e_n;
	clk->dpll_data->last_rounded_rate =
		_dpll_compute_new_rate(clk->parent->rate, min_e_m,  min_e_n);

	pr_debug("clock: final least error: e = %d, m = %d, n = %d\n",
		 min_e, min_e_m, min_e_n);
	pr_debug("clock: final rate: %ld  (target rate: %ld)\n",
		 clk->dpll_data->last_rounded_rate, target_rate);

	return clk->dpll_data->last_rounded_rate;
}

/*-------------------------------------------------------------------------
 * Omap2 clock reset and init functions
 *-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS
void omap2_clk_disable_unused(struct clk *clk)
{
	u32 regval32, v;

	v = (clk->flags & INVERT_ENABLE) ? (1 << clk->enable_bit) : 0;

	regval32 = __raw_readl(clk->enable_reg);
	if ((regval32 & (1 << clk->enable_bit)) == v)
		return;

	printk(KERN_INFO "Disabling unused clock \"%s\"\n", clk->name);
	_omap2_clk_disable(clk);
}
#endif
